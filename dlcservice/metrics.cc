// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <dbus/dlcservice/dbus-constants.h>

#include "dlcservice/error.h"
#include "dlcservice/metrics.h"

using dlcservice::metrics::InstallResult;
using dlcservice::metrics::UninstallResult;
using std::string;

namespace dlcservice {

namespace metrics {

const char kMetricInstallResult[] = "Platform.DlcService.InstallResult";
const char kMetricUninstallResult[] = "Platform.DlcService.UninstallResult";
}  // namespace metrics

// IMPORTANT: To obsolete a metric enum value, just remove it from the map
// initialization and comment it out on the Enum.
Metrics::InstallResultMap Metrics::install_result_ = {
    {error::kFailedToCreateDirectory, InstallResult::kFailedToCreateDirectory},
    {error::kFailedInstallInUpdateEngine,
     InstallResult::kFailedInstallInUpdateEngine},
    {kErrorInvalidDlc, InstallResult::kFailedInvalidDlc},  // dbus error
    {kErrorNeedReboot, InstallResult::kFailedNeedReboot},  // dbus error
    {kErrorBusy, InstallResult::kFailedUpdateEngineBusy},  // dbus error
    {error::kFailedToVerifyImage, InstallResult::kFailedToVerifyImage},
    {error::kFailedToMountImage, InstallResult::kFailedToMountImage},
};

Metrics::UninstallResultMap Metrics::uninstall_result_ = {
    {kErrorInvalidDlc, UninstallResult::kFailedInvalidDlc},  // dbus error
    {kErrorBusy, UninstallResult::kFailedUpdateEngineBusy},  // dbus error
};

void Metrics::Init() {
  metrics_library_->Init();
}

void Metrics::SendInstallResultSuccess(const bool& installed_by_ue) {
  if (installed_by_ue) {
    SendInstallResult(InstallResult::kSuccessNewInstall);
  } else {
    SendInstallResult(InstallResult::kSuccessAlreadyInstalled);
  }
}

void Metrics::SendInstallResultFailure(brillo::ErrorPtr* err) {
  DCHECK(err->get());
  InstallResult res = InstallResult::kUnknownError;
  if (err && err->get()) {
    const string error_code = Error::GetRootErrorCode(*err);
    auto it = install_result_.find(error_code);
    if (it != install_result_.end())
      res = it->second;
  }
  SendInstallResult(res);
}

void Metrics::SendInstallResult(InstallResult result) {
  metrics_library_->SendEnumToUMA(
      metrics::kMetricInstallResult, static_cast<int>(result),
      static_cast<int>(InstallResult::kNumConstants));
  // TODO(andrewlassalle): Remove log after 2020-12-25
  LOG(INFO) << "InstallResult metric sent:" << static_cast<int>(result);
}

void Metrics::SendUninstallResult(brillo::ErrorPtr* err) {
  UninstallResult res = UninstallResult::kUnknownError;
  if (err && err->get()) {
    const string error_code = Error::GetRootErrorCode(*err);
    auto it = uninstall_result_.find(error_code);
    if (it != uninstall_result_.end())
      res = it->second;
  } else {
    res = UninstallResult::kSuccess;
  }
  SendUninstallResult(res);
}

void Metrics::SendUninstallResult(UninstallResult result) {
  metrics_library_->SendEnumToUMA(
      metrics::kMetricUninstallResult, static_cast<int>(result),
      static_cast<int>(UninstallResult::kNumConstants));
}

}  // namespace dlcservice
