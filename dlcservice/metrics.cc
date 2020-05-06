// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include <base/logging.h>
#include <dbus/dlcservice/dbus-constants.h>

#include "dlcservice/error.h"
#include "dlcservice/metrics.h"

using dlcservice::metrics::InstallResult;
using std::map;
using std::string;

namespace dlcservice {

namespace metrics {

const char kMetricInstallResult[] = "DlcService.InstallResult";

}

// To obsolete a metric enum value, just remove it from the map initialization.
// Never remove it from |InstallResult| enum.
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

void Metrics::SendInstallResult(InstallResult install_result) {
  metrics_library_->SendEnumToUMA(
      metrics::kMetricInstallResult, static_cast<int>(install_result),
      static_cast<int>(InstallResult::kNumConstants));
  LOG(INFO) << "InstallResult metric sent:" << static_cast<int>(install_result);
}

}  // namespace dlcservice
