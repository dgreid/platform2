// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_TPM_MANAGER_METRICS_H_
#define TPM_MANAGER_SERVER_TPM_MANAGER_METRICS_H_

#include <metrics/metrics_library.h>

#include "tpm_manager/server/dictionary_attack_reset_status.h"

namespace tpm_manager {

// This class provides wrapping functions for callers to report DA-related
// metrics without bothering to know all the constant declarations.
class TpmManagerMetrics : private MetricsLibrary {
 public:
  TpmManagerMetrics() = default;
  virtual ~TpmManagerMetrics() = default;

  virtual void ReportDictionaryAttackResetStatus(
      DictionaryAttackResetStatus status);

  virtual void ReportDictionaryAttackCounter(int counter);

  // Reports the TPM version fingerprint to the
  // "Platform.TPM.VersionFingerprint" histogram.
  virtual void ReportVersionFingerprint(int fingerprint);

  void set_metrics_library_for_testing(
      MetricsLibraryInterface* metrics_library) {
    metrics_library_ = metrics_library;
  }

 private:
  MetricsLibraryInterface* metrics_library_{this};
  DISALLOW_COPY_AND_ASSIGN(TpmManagerMetrics);
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_TPM_MANAGER_METRICS_H_
