// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_MOCK_TPM_MANAGER_METRICS_H_
#define TPM_MANAGER_SERVER_MOCK_TPM_MANAGER_METRICS_H_

#include "tpm_manager/server/tpm_manager_metrics.h"

namespace tpm_manager {

class MockTpmManagerMetrics : public TpmManagerMetrics {
 public:
  MockTpmManagerMetrics() = default;
  virtual ~MockTpmManagerMetrics() = default;

  MOCK_METHOD(void,
              ReportDictionaryAttackResetStatus,
              (DictionaryAttackResetStatus),
              (override));

  MOCK_METHOD(void, ReportDictionaryAttackCounter, (int), (override));
  MOCK_METHOD(void, ReportVersionFingerprint, (int), (override));
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_MOCK_TPM_MANAGER_METRICS_H_
