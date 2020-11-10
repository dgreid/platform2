// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_METRICS_H_
#define OOBE_CONFIG_METRICS_H_

#include <base/macros.h>
#include <metrics/metrics_library.h>

namespace oobe_config {

class Metrics {
 public:
  enum class OobeRestoreResult {
    kSuccess = 0,
    kStage1Failure = 1,
    kStage2Failure = 2,
    kStage3Failure = 3,
    kCount,
  };

  enum class RollbackSaveResult {
    kSuccess = 0,
    kStage1Failure = 1,
    kStage2Failure = 2,
    kCount,
  };

  Metrics();
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  void RecordRestoreResult(OobeRestoreResult result);

  void RecordSaveResult(RollbackSaveResult result);

 private:
  MetricsLibrary metrics_library_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_METRICS_H_
