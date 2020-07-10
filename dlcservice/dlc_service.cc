// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc_service.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>

#include "dlcservice/dlc.h"
#include "dlcservice/error.h"
#include "dlcservice/ref_count.h"
#include "dlcservice/utils.h"

using base::Callback;
using brillo::ErrorPtr;
using brillo::MessageLoop;
using std::string;
using update_engine::Operation;
using update_engine::StatusResult;

namespace dlcservice {

DlcService::DlcService()
    : periodic_install_check_id_(MessageLoop::kTaskIdNull),
      weak_ptr_factory_(this) {}

DlcService::~DlcService() {
  if (periodic_install_check_id_ != MessageLoop::kTaskIdNull &&
      !brillo::MessageLoop::current()->CancelTask(periodic_install_check_id_))
    LOG(ERROR)
        << "Failed to cancel delayed update_engine check during cleanup.";
}

void DlcService::Initialize() {
  auto* system_state = SystemState::Get();
  const auto prefs_dir = system_state->dlc_prefs_dir();
  if (!base::PathExists(prefs_dir)) {
    CHECK(CreateDir(prefs_dir))
        << "Failed to create dlc prefs directory: " << prefs_dir;
  }

  dlc_manager_ = std::make_unique<DlcManager>();

  // Register D-Bus signal callbacks.
  system_state->update_engine()->RegisterStatusUpdateAdvancedSignalHandler(
      base::Bind(&DlcService::OnStatusUpdateAdvancedSignal,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&DlcService::OnStatusUpdateAdvancedSignalConnected,
                 weak_ptr_factory_.GetWeakPtr()));

  system_state->session_manager()->RegisterSessionStateChangedSignalHandler(
      base::Bind(&DlcService::OnSessionStateChangedSignal,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&DlcService::OnSessionStateChangedSignalConnected,
                 weak_ptr_factory_.GetWeakPtr()));

  dlc_manager_->Initialize();
}

bool DlcService::Install(const DlcId& id,
                         const string& omaha_url,
                         ErrorPtr* err) {
  bool result = InstallInternal(id, omaha_url, err);
  // Only send error metrics in here. Install success metrics is sent in
  // |DlcBase|.
  if (!result) {
    SystemState::Get()->metrics()->SendInstallResultFailure(err);
    Error::ConvertToDbusError(err);
  }
  return result;
}

bool DlcService::InstallInternal(const DlcId& id,
                                 const string& omaha_url,
                                 ErrorPtr* err) {
  // TODO(ahassani): Currently, we create the DLC images even if later we find
  // out the update_engine is busy and we have to delete the images. It would be
  // better to know the update_engine status beforehand so we can tell the DLC
  // to not create the images, just load them if it can. We can do this more
  // reliably by caching the last status we saw from update_engine, rather than
  // pulling for it on every install request. That would also allows us to
  // properly queue the incoming install requests.

  // Try to install and figure out if install through update_engine is needed.
  bool external_install_needed = false;
  if (!dlc_manager_->Install(id, &external_install_needed, err)) {
    LOG(ERROR) << "Failed to install DLC=" << id;
    return false;
  }

  // Install through update_engine only if needed.
  if (!external_install_needed)
    return true;

  if (!InstallWithUpdateEngine(id, omaha_url, err)) {
    // dlcservice must cancel the install as update_engine won't be able to
    // install the initialized DLC.
    ErrorPtr tmp_err;
    if (!dlc_manager_->CancelInstall(id, *err, &tmp_err))
      LOG(ERROR) << "Failed to cancel install.";

    return false;
  }

  // By now the update_engine is installing the DLC, so schedule a periodic
  // install checker in case we miss update_engine signals.
  SchedulePeriodicInstallCheck();

  return true;
}

bool DlcService::InstallWithUpdateEngine(const DlcId& id,
                                         const string& omaha_url,
                                         ErrorPtr* err) {
  // Check what state update_engine is in.
  if (SystemState::Get()->update_engine_status().current_operation() ==
      update_engine::UPDATED_NEED_REBOOT) {
    *err =
        Error::Create(FROM_HERE, kErrorNeedReboot,
                      "Update Engine applied update, device needs a reboot.");
    return false;
  }

  LOG(INFO) << "Sending request to update_engine to install DLC=" << id;
  // Invokes update_engine to install the DLC.
  ErrorPtr tmp_err;
  if (!SystemState::Get()->update_engine()->AttemptInstall(omaha_url, {id},
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
    *err =
        Error::Create(FROM_HERE, kErrorBusy,
                      "Update Engine failed to schedule install operations.");
    return false;
  }

  return true;
}

bool DlcService::Uninstall(const string& id, brillo::ErrorPtr* err) {
  bool result = dlc_manager_->Uninstall(id, err);
  SystemState::Get()->metrics()->SendUninstallResult(err);
  if (!result)
    Error::ConvertToDbusError(err);

  return result;
}

bool DlcService::Purge(const string& id, brillo::ErrorPtr* err) {
  return dlc_manager_->Purge(id, err);
}

const DlcBase* DlcService::GetDlc(const DlcId& id, brillo::ErrorPtr* err) {
  return dlc_manager_->GetDlc(id, err);
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

bool DlcService::InstallCompleted(const DlcIdList& ids, ErrorPtr* err) {
  return dlc_manager_->InstallCompleted(ids, err);
}

bool DlcService::UpdateCompleted(const DlcIdList& ids, ErrorPtr* err) {
  return dlc_manager_->UpdateCompleted(ids, err);
}

void DlcService::CancelInstall(const ErrorPtr& err_in) {
  ErrorPtr tmp_err;
  if (!dlc_manager_->CancelInstall(err_in, &tmp_err))
    LOG(ERROR) << "Failed to cancel install.";
}

void DlcService::PeriodicInstallCheck() {
  periodic_install_check_id_ = MessageLoop::kTaskIdNull;

  // If we're not installing anything anymore, no need to schedule again.
  if (!dlc_manager_->IsInstalling())
    return;

  const int kNotSeenStatusDelay = 10;
  auto* system_state = SystemState::Get();
  if ((system_state->clock()->Now() -
       system_state->update_engine_status_timestamp()) >
      base::TimeDelta::FromSeconds(kNotSeenStatusDelay)) {
    if (GetUpdateEngineStatus()) {
      ErrorPtr tmp_error;
      if (!HandleStatusResult(&tmp_error)) {
        return;
      }
    }
  }

  SchedulePeriodicInstallCheck();
}

void DlcService::SchedulePeriodicInstallCheck() {
  if (periodic_install_check_id_ != MessageLoop::kTaskIdNull) {
    LOG(INFO) << "Another periodic install check already scheduled.";
    return;
  }

  periodic_install_check_id_ = brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DlcService::PeriodicInstallCheck,
                 weak_ptr_factory_.GetWeakPtr()),
      base::TimeDelta::FromSeconds(kUECheckTimeout));
}

bool DlcService::HandleStatusResult(brillo::ErrorPtr* err) {
  // If we are not installing any DLC(s), no need to even handle status result.
  if (!dlc_manager_->IsInstalling())
    return true;

  const StatusResult& status = SystemState::Get()->update_engine_status();
  if (!status.is_install()) {
    *err = Error::CreateInternal(
        FROM_HERE, error::kFailedInstallInUpdateEngine,
        "Signal from update_engine indicates that it's not for an install, but "
        "dlcservice was waiting for an install.");
    CancelInstall(*err);
    SystemState::Get()->metrics()->SendInstallResultFailure(err);
    return false;
  }

  switch (status.current_operation()) {
    case update_engine::UPDATED_NEED_REBOOT:
      *err =
          Error::Create(FROM_HERE, kErrorNeedReboot,
                        "Update Engine applied update, device needs a reboot.");
      break;
    case Operation::IDLE:
      LOG(INFO)
          << "Signal from update_engine, proceeding to complete installation.";
      // Send metrics in |DlcBase::FinishInstall| and not here since we might
      // be executing this call for multiple DLCs.
      if (!dlc_manager_->FinishInstall(err)) {
        LOG(ERROR) << "Failed to finish install.";
        return false;
      }
      return true;
    case Operation::REPORTING_ERROR_EVENT:
      *err =
          Error::CreateInternal(FROM_HERE, error::kFailedInstallInUpdateEngine,
                                "update_engine indicates reporting failure.");
      break;
    // Only when update_engine's |Operation::DOWNLOADING| should the DLC send
    // |DlcState::INSTALLING|. Majority of the install process for DLC(s) is
    // during |Operation::DOWNLOADING|, this also means that only a single
    // growth from 0.0 to 1.0 for progress reporting will happen.
    case Operation::DOWNLOADING:
      // TODO(ahassani): Add unittest for this.
      dlc_manager_->ChangeProgress(status.progress());

      FALLTHROUGH;
    default:
      return true;
  }

  CancelInstall(*err);
  SystemState::Get()->metrics()->SendInstallResultFailure(err);
  return false;
}

bool DlcService::GetUpdateEngineStatus() {
  StatusResult status_result;
  if (!SystemState::Get()->update_engine()->GetStatusAdvanced(&status_result,
                                                              nullptr)) {
    LOG(ERROR) << "Failed to get update_engine status, will try again later.";
    return false;
  }
  SystemState::Get()->set_update_engine_status(status_result);
  LOG(INFO) << "Got update_engine status: "
            << status_result.current_operation();
  return true;
}

void DlcService::OnStatusUpdateAdvancedSignal(
    const StatusResult& status_result) {
  // Always set the status.
  SystemState::Get()->set_update_engine_status(status_result);

  ErrorPtr err;
  if (!HandleStatusResult(&err))
    DCHECK(err.get());
}

void DlcService::OnStatusUpdateAdvancedSignalConnected(
    const string& interface_name, const string& signal_name, bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to update_engine's StatusUpdate signal.";
  }
  if (!GetUpdateEngineStatus()) {
    // As a last resort, if we couldn't get the status, just set the status to
    // IDLE, so things can move forward. This is mostly the case because when
    // update_engine comes up its first status is IDLE and it will stay that way
    // for quite a while.
    StatusResult status;
    status.set_current_operation(Operation::IDLE);
    status.set_is_install(false);
  }
}

void DlcService::OnSessionStateChangedSignalConnected(
    const string& interface_name, const string& signal_name, bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to session_manager's SessionStateChanged "
               << "signal.";
  }
}

void DlcService::OnSessionStateChangedSignal(const std::string& state) {
  UserRefCount::SessionChanged(state);
}

}  // namespace dlcservice
