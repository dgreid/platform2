// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_manager_metrics.h"

#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "tpm_manager/server/tpm_manager_metrics_names.h"

namespace tpm_manager {

namespace {

using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;

}  // namespace

class TpmManagerMetricsTest : public ::testing::Test {
 public:
  TpmManagerMetricsTest() {
    tpm_manager_metrics_.set_metrics_library_for_testing(
        &mock_metrics_library_);
  }

 protected:
  StrictMock<MetricsLibraryMock> mock_metrics_library_;
  TpmManagerMetrics tpm_manager_metrics_;
};

TEST_F(TpmManagerMetricsTest, ReportDictionaryAttackResetStatus) {
  // Selectively tests the enums to see if the parameters are correctly passed.
  const DictionaryAttackResetStatus statuses[]{
      kResetNotNecessary,
      kResetAttemptSucceeded,
      kResetAttemptFailed,
  };
  for (auto status : statuses) {
    EXPECT_CALL(mock_metrics_library_,
                SendEnumToUMA(kDictionaryAttackResetStatusHistogram, status,
                              DictionaryAttackResetStatus::
                                  kDictionaryAttackResetStatusNumBuckets))
        .WillOnce(Return(true));
    tpm_manager_metrics_.ReportDictionaryAttackResetStatus(status);
  }
}

TEST_F(TpmManagerMetricsTest, ReportDictionaryAttackCounter) {
  EXPECT_CALL(mock_metrics_library_,
              SendEnumToUMA(kDictionaryAttackCounterHistogram, 0, _))
      .WillOnce(Return(true));
  tpm_manager_metrics_.ReportDictionaryAttackCounter(0);
  EXPECT_CALL(mock_metrics_library_,
              SendEnumToUMA(kDictionaryAttackCounterHistogram, 10, _))
      .WillOnce(Return(true));
  tpm_manager_metrics_.ReportDictionaryAttackCounter(10);
}

}  // namespace tpm_manager
