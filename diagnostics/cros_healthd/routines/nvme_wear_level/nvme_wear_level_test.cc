// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/common/system/mock_debugd_adapter.h"
#include "diagnostics/cros_healthd/routines/nvme_wear_level/nvme_wear_level.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;
using testing::WithArg;

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {
constexpr uint32_t kThreshold50 = 50;

constexpr uint8_t kWearLevel4[] = {0, 0, 0, 0, 0, 4, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kWearLevel70[] = {0, 0, 0, 0, 0, 70, 0, 0,
                                    0, 0, 0, 0, 0, 0,  0, 0};

// 8-byte data with wear level 4.
constexpr uint8_t kEightByteWearLevel4[] = {0, 0, 0, 0, 0, 4, 0, 0};

// Invalid base64 encoded data. Length of encoded data must divide by 4.
constexpr char kInvaildWearLevel[] = "AAAAAAAAAAAAAAAAAAA";
}  // namespace

class NvmeWearLevelRoutineTest : public testing::Test {
 protected:
  NvmeWearLevelRoutineTest() = default;
  NvmeWearLevelRoutineTest(const NvmeWearLevelRoutineTest&) = delete;
  NvmeWearLevelRoutineTest& operator=(const NvmeWearLevelRoutineTest&) = delete;

  DiagnosticRoutine* routine() { return routine_.get(); }

  void CreateWearLevelRoutine(uint32_t wear_level_threshold) {
    routine_ = std::make_unique<NvmeWearLevelRoutine>(&debugd_adapter_,
                                                      wear_level_threshold);
  }

  mojo_ipc::RoutineUpdatePtr RunRoutineAndWaitForExit() {
    DCHECK(routine_);
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};

    routine_->Start();
    routine_->PopulateStatusUpdate(&update, true);
    return chromeos::cros_healthd::mojom::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union));
  }

  StrictMock<MockDebugdAdapter> debugd_adapter_;

 private:
  std::unique_ptr<NvmeWearLevelRoutine> routine_;
};

// Tests that the NvmeWearLevel routine passes if wear level less than
// threshold.
TEST_F(NvmeWearLevelRoutineTest, Pass) {
  const std::string kNvmeRawOutput(std::begin(kWearLevel4),
                                   std::end(kWearLevel4));
  std::string nvme_encoded_output;
  base::Base64Encode(kNvmeRawOutput, &nvme_encoded_output);

  CreateWearLevelRoutine(kThreshold50);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeWearLevelRoutine::kNvmeLogPageId,
                         NvmeWearLevelRoutine::kNvmeLogDataLength,
                         NvmeWearLevelRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));

  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kReady);

  VerifyNonInteractiveUpdate(
      RunRoutineAndWaitForExit()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
      NvmeWearLevelRoutine::kNvmeWearLevelRoutineSuccess);
}

// Tests that the NvmeWearLevel routine fails if wear level larger than or equal
// to threshold.
TEST_F(NvmeWearLevelRoutineTest, HighWearLevel) {
  const std::string kNvmeRawOutput(std::begin(kWearLevel70),
                                   std::end(kWearLevel70));
  std::string nvme_encoded_output;
  base::Base64Encode(kNvmeRawOutput, &nvme_encoded_output);

  CreateWearLevelRoutine(kThreshold50);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeWearLevelRoutine::kNvmeLogPageId,
                         NvmeWearLevelRoutine::kNvmeLogDataLength,
                         NvmeWearLevelRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(RunRoutineAndWaitForExit()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             NvmeWearLevelRoutine::kNvmeWearLevelRoutineFailed);
}

// Tests that the NvmeWearLevel routine fails if threshold exceeds 100.
TEST_F(NvmeWearLevelRoutineTest, InvalidThreshold) {
  const uint32_t kThreshold105 = 105;
  CreateWearLevelRoutine(kThreshold105);
  VerifyNonInteractiveUpdate(
      RunRoutineAndWaitForExit()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      NvmeWearLevelRoutine::kNvmeWearLevelRoutineThresholdError);
}

// Tests that the NvmeWearLevel routine fails if wear level is invalid.
TEST_F(NvmeWearLevelRoutineTest, InvalidWearLevel) {
  CreateWearLevelRoutine(kThreshold50);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeWearLevelRoutine::kNvmeLogPageId,
                         NvmeWearLevelRoutine::kNvmeLogDataLength,
                         NvmeWearLevelRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kInvaildWearLevel, nullptr); }));
  VerifyNonInteractiveUpdate(
      RunRoutineAndWaitForExit()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      NvmeWearLevelRoutine::kNvmeWearLevelRoutineGetInfoError);
}

// Tests that the NvmeWearLevel routine fails if size of return data is not
// equal to required length.
TEST_F(NvmeWearLevelRoutineTest, InvalidLength) {
  const std::string kNvmeRawOutput(std::begin(kEightByteWearLevel4),
                                   std::end(kEightByteWearLevel4));
  std::string nvme_encoded_output;
  base::Base64Encode(kNvmeRawOutput, &nvme_encoded_output);

  CreateWearLevelRoutine(kThreshold50);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeWearLevelRoutine::kNvmeLogPageId,
                         NvmeWearLevelRoutine::kNvmeLogDataLength,
                         NvmeWearLevelRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(
      RunRoutineAndWaitForExit()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      NvmeWearLevelRoutine::kNvmeWearLevelRoutineGetInfoError);
}

// Tests that the NvmeWearLevel routine fails if debugd returns with an error.
TEST_F(NvmeWearLevelRoutineTest, DebugdError) {
  const char kDebugdErrorMessage[] = "Debugd mock error for testing";
  const brillo::ErrorPtr kError =
      brillo::Error::Create(FROM_HERE, "", "", kDebugdErrorMessage);
  CreateWearLevelRoutine(kThreshold50);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeWearLevelRoutine::kNvmeLogPageId,
                         NvmeWearLevelRoutine::kNvmeLogDataLength,
                         NvmeWearLevelRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run("", kError.get()); }));
  VerifyNonInteractiveUpdate(RunRoutineAndWaitForExit()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kDebugdErrorMessage);
}

}  // namespace diagnostics
