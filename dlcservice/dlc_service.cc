// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc_service.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_util.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>

#include "dlcservice/dlc.h"
#include "dlcservice/error.h"
#include "dlcservice/utils.h"

using base::Callback;
using brillo::ErrorPtr;
using brillo::MessageLoop;
using std::string;
using std::vector;
using update_engine::Operation;
using update_engine::StatusResult;

namespace dlcservice {

DlcService::DlcService()
    : scheduled_period_ue_check_id_(MessageLoop::kTaskIdNull),
      weak_ptr_factory_(this) {
  const auto prefs_dir = SystemState::Get()->dlc_prefs_dir();
  if (!base::PathExists(prefs_dir)) {
    CHECK(CreateDir(prefs_dir))
        << "Failed to create dlc prefs directory: " << prefs_dir;
  }

  dlc_manager_ = std::make_unique<DlcManager>();

  // Register D-Bus signal callbacks.
  update_engine_proxy_ = SystemState::Get()->update_engine();
  update_engine_proxy_->RegisterStatusUpdateAdvancedSignalHandler(
      base::Bind(&DlcService::OnStatusUpdateAdvancedSignal,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&DlcService::OnStatusUpdateAdvancedSignalConnected,
                 weak_ptr_factory_.GetWeakPtr()));
}

DlcService::~DlcService() {
  if (scheduled_period_ue_check_id_ != MessageLoop::kTaskIdNull &&
      !brillo::MessageLoop::current()->CancelTask(
          scheduled_period_ue_check_id_))
    LOG(ERROR)
        << "Failed to cancel delayed update_engine check during cleanup.";
}

void DlcService::Initialize() {
  dlc_manager_->Initialize();
}

const DlcBase* DlcService::GetDlc(const DlcId& id) {
  return dlc_manager_->GetDlc(id);
}

bool DlcService::Install(const DlcIdList& dlcs,
                         const string& omaha_url,
                         ErrorPtr* err) {
  // If an install is already in progress, dlcservice is busy.
  if (dlc_manager_->IsInstalling()) {
    *err = Error::Create(kErrorBusy, "Another install is already in progress.");
    LOG(ERROR) << Error::ToString(*err);
    return false;
  }

  // Check what state update_engine is in.
  Operation update_engine_op;
  if (!GetUpdateEngineStatus(&update_engine_op)) {
    *err = Error::Create(kErrorInternal,
                         "Failed to get the status of Update Engine.");
    LOG(ERROR) << Error::ToString(*err);
    return false;
  }
  switch (update_engine_op) {
    case update_engine::UPDATED_NEED_REBOOT:
      *err =
          Error::Create(kErrorNeedReboot,
                        "Update Engine applied update, device needs a reboot.");
      LOG(ERROR) << Error::ToString(*err);
      return false;
    case update_engine::IDLE:
      break;
    default:
      *err =
          Error::Create(kErrorBusy, "Update Engine is performing operations.");
      LOG(ERROR) << Error::ToString(*err);
      return false;
  }

  if (!dlc_manager_->InitInstall(dlcs, err)) {
    LOG(ERROR) << Error::ToString(*err);
    return false;
  }

  // This is the unique DLC(s) that actually need to be installed.
  DlcIdList unique_dlcs_to_install = dlc_manager_->GetMissingInstalls();
  // Check if there is nothing to install.
  if (unique_dlcs_to_install.size() == 0) {
    SendOnInstallStatusSignal(Status::COMPLETED, kErrorNone,
                              dlc_manager_->GetSupported(), 1.);
    return true;
  }

  LOG(INFO) << "Sending request to update_engine to install DLCs="
            << base::JoinString(unique_dlcs_to_install, ",");

  // Invokes update_engine to install the DLC.
  ErrorPtr tmp_err;
  if (!update_engine_proxy_->AttemptInstall(omaha_url, unique_dlcs_to_install,
                                            &tmp_err)) {
    // TODO(kimjae): need update engine to propagate correct error message by
    // passing in |ErrorPtr| and being set within update engine, current default
    // is to indicate that update engine is updating because there is no way an
    // install should have taken place if not through dlcservice. (could also be
    // the case that an update applied between the time of the last status check
    // above, but just return |kErrorBusy| because the next time around if an
    // update has been applied and is in a reboot needed state, it will indicate
    // correctly then).
    LOG(ERROR) << "Update Engine failed to install requested DLCs: "
               << (tmp_err ? Error::ToString(tmp_err)
                           : "Missing error from update engine proxy.");
    *err = Error::Create(
        kErrorBusy, "Update Engine failed to schedule install operations.");
    LOG(ERROR) << Error::ToString(*err);
    // dlcservice must cancel the install by communicating to dlc_manager who
    // manages the DLC(s), as update_engine won't be able to install the
    // initialized DLC(s) for installation.
    if (!dlc_manager_->CancelInstall(&tmp_err))
      LOG(ERROR) << Error::ToString(tmp_err);
    return false;
  }

  SchedulePeriodicInstallCheck(true);
  return true;
}

bool DlcService::Uninstall(const string& id_in, brillo::ErrorPtr* err) {
  // TODO(crbug.com/1069162): Uninstall should remove based on ref-counting
  // logic.
  return Purge(id_in, err);
}

bool DlcService::Purge(const string& id_in, brillo::ErrorPtr* err) {
  // Check that an update isn't in progress.
  if (!dlc_manager_->IsInstalling()) {
    Operation op;
    if (!GetUpdateEngineStatus(&op)) {
      *err = Error::Create(kErrorInternal,
                           "Failed to get the status of Update Engine");
      LOG(ERROR) << Error::ToString(*err);
      return false;
    }
    switch (op) {
      case update_engine::IDLE:
      case update_engine::UPDATED_NEED_REBOOT:
        break;
      default:
        *err = Error::Create(kErrorBusy, "Install or update is in progress.");
        LOG(ERROR) << Error::ToString(*err);
        return false;
    }
  }
  bool ret = dlc_manager_->Delete(id_in, err);
  if (!ret)
    LOG(ERROR) << Error::ToString(*err);
  return ret;
}

bool DlcService::GetDlcState(const std::string& id_in,
                             DlcState* dlc_state,
                             ErrorPtr* err) {
  bool ret = dlc_manager_->GetDlcState(id_in, dlc_state, err);
  if (!ret)
    LOG(ERROR) << Error::ToString(*err);
  return ret;
}

DlcIdList DlcService::GetInstalled() {
  return dlc_manager_->GetInstalled();
}

DlcIdList DlcService::GetExistingDlcs() {
  return dlc_manager_->GetExistingDlcs();
}

DlcIdList DlcService::GetDlcsToUpdate() {
  return dlc_manager_->GetDlcsToUpdate();
}

bool DlcService::InstallCompleted(const DlcIdList& ids_in, ErrorPtr* err) {
  bool ret = dlc_manager_->InstallCompleted(ids_in, err);
  if (!ret)
    LOG(ERROR) << Error::ToString(*err);
  return ret;
}

bool DlcService::UpdateCompleted(const DlcIdList& ids_in, ErrorPtr* err) {
  bool ret = dlc_manager_->UpdateCompleted(ids_in, err);
  if (!ret)
    LOG(ERROR) << Error::ToString(*err);
  return ret;
}

void DlcService::SendFailedSignalAndCleanup() {
  ErrorPtr tmp_err;
  if (!dlc_manager_->CancelInstall(&tmp_err))
    LOG(ERROR) << Error::ToString(tmp_err);
  SendOnInstallStatusSignal(Status::FAILED, kErrorInternal,
                            dlc_manager_->GetSupported(), 0.);
}

void DlcService::PeriodicInstallCheck() {
  if (scheduled_period_ue_check_id_ == MessageLoop::kTaskIdNull) {
    LOG(ERROR) << "Should not have been called unless scheduled.";
    return;
  }

  scheduled_period_ue_check_id_ = MessageLoop::kTaskIdNull;

  if (!dlc_manager_->IsInstalling()) {
    LOG(ERROR) << "Should not have to check update_engine status while not "
                  "performing an install.";
    return;
  }

  Operation update_engine_op;
  if (!GetUpdateEngineStatus(&update_engine_op)) {
    LOG(ERROR)
        << "Failed to get the status of update_engine, it is most likely down.";
    SendFailedSignalAndCleanup();
    return;
  }
  switch (update_engine_op) {
    case update_engine::UPDATED_NEED_REBOOT:
      LOG(ERROR) << "Thought to be installing DLC(s), but update_engine is not "
                    "installing and actually performed an update.";
      SendFailedSignalAndCleanup();
      break;
    case update_engine::IDLE:
      if (scheduled_period_ue_check_retry_) {
        LOG(INFO) << "Going to retry periodic check to check install signal.";
        SchedulePeriodicInstallCheck(false);
        return;
      }
      SendFailedSignalAndCleanup();
      break;
    default:
      SchedulePeriodicInstallCheck(true);
  }
}

void DlcService::SchedulePeriodicInstallCheck(bool retry) {
  if (scheduled_period_ue_check_id_ != MessageLoop::kTaskIdNull) {
    LOG(ERROR) << "Scheduling logic is internally not handled correctly, this "
               << "requires a scheduling logic update.";
    if (!brillo::MessageLoop::current()->CancelTask(
            scheduled_period_ue_check_id_)) {
      LOG(ERROR) << "Failed to cancel previous delayed update_engine check "
                 << "when scheduling.";
    }
  }
  scheduled_period_ue_check_id_ =
      brillo::MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&DlcService::PeriodicInstallCheck,
                     weak_ptr_factory_.GetWeakPtr()),
          base::TimeDelta::FromSeconds(kUECheckTimeout));
  scheduled_period_ue_check_retry_ = retry;
}

bool DlcService::HandleStatusResult(const StatusResult& status_result) {
  // If we are not installing any DLC(s), no need to even handle status result.
  if (!dlc_manager_->IsInstalling())
    return false;

  // When a signal is received from update_engine, it is more efficient to
  // cancel the periodic check that's scheduled by re-posting a delayed task
  // after cancelling the currently set periodic check. If the cancelling of the
  // periodic check fails, let it run as it will be rescheduled correctly within
  // the periodic check itself again.
  if (!brillo::MessageLoop::current()->CancelTask(
          scheduled_period_ue_check_id_)) {
    LOG(ERROR) << "Failed to cancel delayed update_engine check when signal "
                  "was received from update_engine, so letting it run.";
  } else {
    scheduled_period_ue_check_id_ = MessageLoop::kTaskIdNull;
  }

  if (!status_result.is_install()) {
    LOG(ERROR) << "Signal from update_engine indicates that it's not for an "
                  "install, but dlcservice was waiting for an install.";
    SendFailedSignalAndCleanup();
    return false;
  }

  switch (status_result.current_operation()) {
    case Operation::IDLE:
      LOG(INFO)
          << "Signal from update_engine, proceeding to complete installation.";
      return true;
    case Operation::REPORTING_ERROR_EVENT:
      LOG(ERROR) << "Signal from update_engine indicates reporting failure.";
      SendFailedSignalAndCleanup();
      return false;
    // Only when update_engine's |Operation::DOWNLOADING| should dlcservice send
    // a signal out for |InstallStatus| for |Status::RUNNING|. Majority of the
    // install process for DLC(s) is during |Operation::DOWNLOADING|, this also
    // means that only a single growth from 0.0 to 1.0 for progress reporting
    // will happen.
    case Operation::DOWNLOADING:
      SendOnInstallStatusSignal(Status::RUNNING, kErrorNone,
                                dlc_manager_->GetSupported(),
                                status_result.progress());
      FALLTHROUGH;
    default:
      SchedulePeriodicInstallCheck(true);
      return false;
  }
}

bool DlcService::GetUpdateEngineStatus(Operation* operation) {
  StatusResult status_result;
  if (!update_engine_proxy_->GetStatusAdvanced(&status_result, nullptr)) {
    return false;
  }
  *operation = status_result.current_operation();
  return true;
}

void DlcService::AddObserver(DlcService::Observer* observer) {
  observers_.push_back(observer);
}

void DlcService::SendOnInstallStatusSignal(const dlcservice::Status& status,
                                           const std::string& error_code,
                                           const DlcIdList& ids,
                                           double progress) {
  InstallStatus install_status;
  install_status.set_status(status);
  switch (status) {
    case COMPLETED:
    case FAILED:
      install_status.set_state(InstallStatus::IDLE);
      break;
    case RUNNING:
      install_status.set_state(InstallStatus::INSTALLING);
      break;
    default:
      NOTREACHED();
  }

  install_status.set_error_code(error_code);
  DlcModuleList* dlc_list = install_status.mutable_dlc_module_list();
  for (const auto& id : ids) {
    const auto* dlc = dlc_manager_->GetDlc(id);
    dlc_list->add_dlc_module_infos()->set_dlc_id(id);
    dlc_list->add_dlc_module_infos()->set_dlc_root(dlc->GetRoot().value());
  }
  install_status.set_progress(progress);

  for (const auto& observer : observers_) {
    observer->SendInstallStatus(install_status);
  }
}

void DlcService::OnStatusUpdateAdvancedSignal(
    const StatusResult& status_result) {
  if (!HandleStatusResult(status_result))
    return;

  ErrorPtr tmp_err;
  if (!dlc_manager_->FinishInstall(&tmp_err)) {
    LOG(ERROR) << Error::ToString(tmp_err);
    SendOnInstallStatusSignal(Status::FAILED, kErrorInternal,
                              dlc_manager_->GetSupported(), 0.);
    return;
  }

  SendOnInstallStatusSignal(Status::COMPLETED, kErrorNone,
                            dlc_manager_->GetSupported(), 1.);
}

void DlcService::OnStatusUpdateAdvancedSignalConnected(
    const string& interface_name, const string& signal_name, bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to update_engine's StatusUpdate signal.";
  }
}

}  // namespace dlcservice
