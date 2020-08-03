// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOD_METRICS_H_
#define BIOD_BIOD_METRICS_H_

#include <memory>
#include <utility>

#include <base/macros.h>
#include <metrics/metrics_library.h>

#include "biod/fp_mode.h"
#include "biod/update_reason.h"

namespace biod {

namespace metrics {

extern const char kFpMatchDurationCapture[];
extern const char kFpMatchDurationMatcher[];
extern const char kFpMatchDurationOverall[];
extern const char kFpNoMatchDurationCapture[];
extern const char kFpNoMatchDurationMatcher[];
extern const char kFpNoMatchDurationOverall[];
extern const char kResetContextMode[];
extern const char kSetContextMode[];
extern const char kSetContextSuccess[];
extern const char kUpdaterStatus[];
extern const char kUpdaterReason[];
extern const char kUpdaterDurationNoUpdate[];
extern const char kUpdaterDurationUpdate[];

}  // namespace metrics

class BiodMetricsInterface {
 public:
  virtual ~BiodMetricsInterface() = default;

  // This is the tools/bio_fw_updater overall status,
  // which encapsulates an UpdateStatus.
  enum class FwUpdaterStatus : int {
    kUnnecessary = 0,
    kSuccessful = 1,
    kFailureFirmwareFileMultiple = 2,
    kFailureFirmwareFileNotFound = 3,
    kFailureFirmwareFileOpen = 4,
    kFailureFirmwareFileFmap = 5,
    kFailurePreUpdateVersionCheck = 6,
    kFailurePostUpdateVersionCheck = 7,
    kFailureUpdateVersionCheck = 8,
    kFailureUpdateFlashProtect = 9,
    kFailureUpdateRO = 10,
    kFailureUpdateRW = 11,

    kMaxValue = kFailureUpdateRW,
  };

  virtual bool SendEnrolledFingerCount(int finger_count) = 0;
  virtual bool SendFpUnlockEnabled(bool enabled) = 0;
  virtual bool SendFpLatencyStats(bool matched,
                                  int capture_ms,
                                  int match_ms,
                                  int overall_ms) = 0;
  virtual bool SendFwUpdaterStatus(FwUpdaterStatus status,
                                   updater::UpdateReason reason,
                                   int overall_ms) = 0;
  virtual bool SendIgnoreMatchEventOnPowerButtonPress(bool is_ignored) = 0;
  virtual bool SendResetContextMode(const FpMode& mode) = 0;
  virtual bool SendSetContextMode(const FpMode& mode) = 0;
  virtual bool SendSetContextSuccess(bool success) = 0;
  virtual bool SendReadPositiveMatchSecretSuccess(bool success) = 0;
  virtual bool SendPositiveMatchSecretCorrect(bool correct) = 0;
  virtual bool SendRecordFormatVersion(int version) = 0;
  virtual bool SendMigrationForPositiveMatchSecretResult(bool success) = 0;
};

class BiodMetrics : public BiodMetricsInterface {
 public:
  BiodMetrics();
  ~BiodMetrics() override = default;

  // Send number of fingers enrolled.
  bool SendEnrolledFingerCount(int finger_count) override;

  // Is unlocking with FP enabled or not?
  bool SendFpUnlockEnabled(bool enabled) override;

  // Send matching/capture latency metrics.
  bool SendFpLatencyStats(bool matched,
                          int capture_ms,
                          int match_ms,
                          int overall_ms) override;

  bool SendFwUpdaterStatus(FwUpdaterStatus status,
                           updater::UpdateReason reason,
                           int overall_ms) override;

  // Is fingerprint ignored due to parallel power button press?
  bool SendIgnoreMatchEventOnPowerButtonPress(bool is_ignored) override;

  // Was CrosFpDevice::ResetContext called while the FPMCU was in correct mode?
  bool SendResetContextMode(const FpMode& mode) override;

  // What mode was FPMCU in when we set context?
  bool SendSetContextMode(const FpMode& mode) override;

  // Did setting context succeed?
  bool SendSetContextSuccess(bool success) override;

  // Reading positive match secret succeeded or not?
  bool SendReadPositiveMatchSecretSuccess(bool success) override;

  // Positive match secret is as expected or not?
  bool SendPositiveMatchSecretCorrect(bool correct) override;

  // Template record file format version.
  bool SendRecordFormatVersion(int version) override;

  // Migration of templates to have positive match secret succeeded or not?
  bool SendMigrationForPositiveMatchSecretResult(bool success) override;

  void SetMetricsLibraryForTesting(
      std::unique_ptr<MetricsLibraryInterface> metrics_lib);

  MetricsLibraryInterface* metrics_library_for_testing() {
    return metrics_lib_.get();
  }

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;

  DISALLOW_COPY_AND_ASSIGN(BiodMetrics);
};

}  // namespace biod

#endif  // BIOD_BIOD_METRICS_H_
