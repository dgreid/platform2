// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_METRICS_H_
#define DLCSERVICE_METRICS_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <metrics/metrics_library.h>

#include "dlcservice/error.h"

namespace dlcservice {

namespace metrics {
extern const char kMetricInstallResult[];

// Never change existing numerical values, because the same numbering is used in
// the UMA website. If you don't need a value, comment out the value that is no
// longer needed, and remove it from the map in metrics.cc; this will let the
// error fall into the |kUnknownError| bucket.
enum class InstallResult {
  kUnknownError = 0,
  kSuccessNewInstall = 1,
  kSuccessAlreadyInstalled = 2,
  kFailedToCreateDirectory = 3,
  kFailedInstallInUpdateEngine = 4,
  kFailedInvalidDlc = 5,
  kFailedNeedReboot = 6,
  kFailedUpdateEngineBusy = 7,
  kFailedToVerifyImage = 8,
  kFailedToMountImage = 9,
  kNumConstants
};
}  // namespace metrics

// Performs UMA metrics logging for the dlcservice daemon.
class Metrics {
 public:
  explicit Metrics(std::unique_ptr<MetricsLibraryInterface> metrics_library)
      : metrics_library_(std::move(metrics_library)) {}

  virtual ~Metrics() = default;

  // Initializes the class.
  void Init();

  // Sends the |InstallResult| value for a successful installation. There are
  // two success scenarios, |kSuccessNewInstall| and |kSuccessAlreadyInstalled|.
  void SendInstallResultSuccess(const bool& installed_by_ue);

  // Sends the |InstallResult| value for when the installation was not
  // successful.
  void SendInstallResultFailure(brillo::ErrorPtr* err);

 protected:
  // For testing.
  Metrics() = default;
  // Sends the value for |InstallResult|.
  virtual void SendInstallResult(metrics::InstallResult install_result);

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;
  // Map DBus error codes and |dlcservice::error|s to |InstallResult| values.
  typedef std::map<std::string, metrics::InstallResult> InstallResultMap;
  static InstallResultMap install_result_;

  // Not copyable or movable.
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_METRICS_H_
