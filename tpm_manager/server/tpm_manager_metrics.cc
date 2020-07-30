// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_manager_metrics.h"
#include "tpm_manager/server/tpm_manager_metrics_names.h"

namespace {

constexpr int kDictionaryAttackCounterNumBuckets = 100;

}  // namespace

namespace tpm_manager {

void TpmManagerMetrics::ReportDictionaryAttackResetStatus(
    DictionaryAttackResetStatus status) {
  metrics_library_->SendEnumToUMA(kDictionaryAttackResetStatusHistogram, status,
                                  kDictionaryAttackResetStatusNumBuckets);
}

void TpmManagerMetrics::ReportDictionaryAttackCounter(int counter) {
  metrics_library_->SendEnumToUMA(kDictionaryAttackCounterHistogram, counter,
                                  kDictionaryAttackCounterNumBuckets);
}

void TpmManagerMetrics::ReportVersionFingerprint(int fingerprint) {
  metrics_library_->SendSparseToUMA(kTPMVersionFingerprint, fingerprint);
}

}  // namespace tpm_manager
