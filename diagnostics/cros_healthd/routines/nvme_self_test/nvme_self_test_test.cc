// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/common/system/mock_debugd_adapter.h"
#include "diagnostics/cros_healthd/routines/nvme_self_test/nvme_self_test.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;
using testing::WithArg;

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {
// Success message from controller if launching is completed without errors.
constexpr char kStartSuccess[] = "Device self-test started";
constexpr char kNvmeError[] = "NVMe Status:Unknown";

}  // namespace

class NvmeSelfTestRoutineTest : public testing::Test {
 protected:
  NvmeSelfTestRoutineTest() = default;
  NvmeSelfTestRoutineTest(const NvmeSelfTestRoutineTest&) = delete;
  NvmeSelfTestRoutineTest& operator=(const NvmeSelfTestRoutineTest&) = delete;

  DiagnosticRoutine* routine() { return routine_.get(); }

  void CreateSelfTestRoutine(const NvmeSelfTestRoutine::SelfTestType& type) {
    routine_ = std::make_unique<NvmeSelfTestRoutine>(&debugd_adapter_, type);
  }

  void RunRoutineStart() {
    DCHECK(routine_);
    routine_->Start();
  }
  void RunRoutineCancel() { routine_->Cancel(); }
  mojo_ipc::RoutineUpdatePtr RunRoutinePopulate() {
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};

    routine_->PopulateStatusUpdate(&update, true);
    return chromeos::cros_healthd::mojom::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union));
  }

  StrictMock<MockDebugdAdapter> debugd_adapter_;

 private:
  std::unique_ptr<NvmeSelfTestRoutine> routine_;
};

// Test that the NvmeSelfTest routine for short-time passes if it starts without
// an error and result from NVMe is passed.
TEST_F(NvmeSelfTestRoutineTest, ShortSelfTestPass) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunShortSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeShortSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kRunning);

  // Progress(byte-0): Bits 3:0, 1 means short-time test is in progress.
  // Percent(byte-1): 0x1e for 30%
  const uint8_t kShortSelfTestRunning[] = {0x1, 0x1e, 0x0, 0x0, 0x0, 0x0,
                                           0x0, 0x0,  0x0, 0x0, 0x0, 0x0,
                                           0x0, 0x0,  0x0, 0x0};
  std::string nvme_encoded_output;
  base::Base64Encode(std::string(std::begin(kShortSelfTestRunning),
                                 std::end(kShortSelfTestRunning)),
                     &nvme_encoded_output);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(RunRoutinePopulate()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             NvmeSelfTestRoutine::kNvmeSelfTestRoutineRunning);

  // Progress(byte-0): Bits 3:0, 0 means test is completed.
  // Status(byte-4): Bits 7:4, 1 for short-time test; Bits 3:0, 0 means passed.
  const uint8_t kShortSelfTestSuccess[] = {0x0, 0x0, 0x0, 0x0, 0x10, 0x0,
                                           0x0, 0x0, 0x0, 0x0, 0x0,  0x0,
                                           0x0, 0x0, 0x0, 0x0};
  nvme_encoded_output.clear();
  base::Base64Encode(std::string(std::begin(kShortSelfTestSuccess),
                                 std::end(kShortSelfTestSuccess)),
                     &nvme_encoded_output);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
      NvmeSelfTestRoutine::kSelfTestRoutineCompleteLog[0x0]);
}

// Test that the NvmeSelfTest routine for short-time fails if it starts with
// an error.
TEST_F(NvmeSelfTestRoutineTest, ShortSelfTestStartError) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunShortSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeShortSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kNvmeError, nullptr); }));
  RunRoutineStart();
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      NvmeSelfTestRoutine::kNvmeSelfTestRoutineStartError);
}

// Test that the NvmeSelfTest routine for short-time fails if result from NVMe
// is failed.
TEST_F(NvmeSelfTestRoutineTest, ShortSelfTestError) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunShortSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeShortSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  // Progress(byte-0): Bits 3:0, 0 means test is completed.
  // Status(byte-4): Bits 7:4, 1 for short-time test; Bits 3:0, 3 means test
  // failed and error index is 3.
  const uint8_t kShortSelfTestError[] = {0x0, 0x0, 0x0, 0x0, 0x13, 0x0,
                                         0x0, 0x0, 0x0, 0x0, 0x0,  0x0,
                                         0x0, 0x0, 0x0, 0x0};
  std::string nvme_encoded_output;
  base::Base64Encode(std::string(std::begin(kShortSelfTestError),
                                 std::end(kShortSelfTestError)),
                     &nvme_encoded_output);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
      NvmeSelfTestRoutine::kSelfTestRoutineCompleteLog[0x3]);
}

// Test that the NvmeSelfTest routinie for short-time fails if result from NVMe
// is an invalid error.
TEST_F(NvmeSelfTestRoutineTest, ShortSelfTestInvalidError) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunShortSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeShortSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  // Progress(byte-0): Bits 3:0, 0 means test is completed.
  // Status(byte-4): Bits 7:4, 1 for short-time test; Bits 3:0, 0xf means test
  // failed but error index is invalid since total types of error is 9.
  const uint8_t kShortSelfTestInvalidError[] = {0x0, 0x0, 0x0, 0x0, 0x1f, 0x0,
                                                0x0, 0x0, 0x0, 0x0, 0x0,  0x0,
                                                0x0, 0x0, 0x0, 0x0};
  std::string nvme_encoded_output;
  base::Base64Encode(std::string(std::begin(kShortSelfTestInvalidError),
                                 std::end(kShortSelfTestInvalidError)),
                     &nvme_encoded_output);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
      NvmeSelfTestRoutine::kSelfTestRoutineCompleteUnknownStatus);
}

// Test that the NvmeSelfTest routinie for short-time fails if the index of
// type is invalid in result of NVMe..
TEST_F(NvmeSelfTestRoutineTest, ShortSelfTestInvalidType) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunShortSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeShortSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  // Progress(byte-0): Bits 3:0, 0 means test is completed.
  // Status(byte-4): Bits 7:4, 0xe for vendor specific but not be supported for
  // NvmeSelfTestRoutine; Bits 3:0, 3 means test failed and error index is 3.
  const uint8_t kShortSelfTestInvalidType[] = {0x0, 0x0, 0x0, 0x0, 0xe3, 0x0,
                                               0x0, 0x0, 0x0, 0x0, 0x0,  0x0,
                                               0x0, 0x0, 0x0, 0x0};
  std::string nvme_encoded_output;
  base::Base64Encode(std::string(std::begin(kShortSelfTestInvalidType),
                                 std::end(kShortSelfTestInvalidType)),
                     &nvme_encoded_output);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      NvmeSelfTestRoutine::kNvmeSelfTestRoutineGetProgressFailed);
}

// Test that the NvmeSelfTest routine for short-time fails if debugd return is
// invalid.
TEST_F(NvmeSelfTestRoutineTest, ShortSelfTestInvalidProgress) {
  // Invalid base64 encoded data. Length of encoded data must divide by 4.
  const char kSelfTestInvalidProgress[] = "AAAAABMEAAAAAAAAAA";

  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunShortSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeShortSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) {
            callback.Run(kSelfTestInvalidProgress, nullptr);
          }));
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      NvmeSelfTestRoutine::kNvmeSelfTestRoutineGetProgressFailed);
}

// Test that the NvmeSelfTest routine for short-time fails if size of return
// data is not equal to required length.
TEST_F(NvmeSelfTestRoutineTest, ShortSelfTestInvalidProgressLength) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunShortSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeShortSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kRunning);

  // 8-byte data with valid progress info.
  // Progress(byte-0): Bits 3:0, 1 means short-time test is in progress.
  // Percent(byte-1): 0x1e for 30%
  const uint8_t kEightByteShortSelfTestRunning[] = {0x1, 0x1e, 0x0, 0x0,
                                                    0x0, 0x0,  0x0, 0x0};
  std::string nvme_encoded_output;
  base::Base64Encode(std::string(std::begin(kEightByteShortSelfTestRunning),
                                 std::end(kEightByteShortSelfTestRunning)),
                     &nvme_encoded_output);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      NvmeSelfTestRoutine::kNvmeSelfTestRoutineGetProgressFailed);
}

// Test that the NvmeSelfTest routine for short-time passes if it is cancelled
// successfully.
TEST_F(NvmeSelfTestRoutineTest, ShortSelfTestCancelPass) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunShortSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeShortSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  // Success message from controller if abortion is completed without an error.
  const char kAbortSuccess[] = "Aborting device self-test operation";
  EXPECT_CALL(debugd_adapter_, StopNvmeSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kAbortSuccess, nullptr); }));
  RunRoutineCancel();
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled,
      NvmeSelfTestRoutine::kNvmeSelfTestRoutineCancelled);
}

// Test that the NvmeSelfTest routine for short-time fails if it is cancelled
// with an error.
TEST_F(NvmeSelfTestRoutineTest, ShortSelfTestCancelError) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunShortSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeShortSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  EXPECT_CALL(debugd_adapter_, StopNvmeSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kNvmeError, nullptr); }));
  RunRoutineCancel();
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      NvmeSelfTestRoutine::kNvmeSelfTestRoutineAbortionError);
}

// Test that the NvmeSelfTest routine for long-time passes if it starts without
// an error and result from NVMe is passed.
TEST_F(NvmeSelfTestRoutineTest, LongSelfTestPass) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunLongSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeLongSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kRunning);

  // Progress(byte-0): Bits 3:0, 2 means long-time test is in progress.
  // Percent(byte-1): 0x0 for 0%
  const uint8_t kLongSelfTestRunning[] = {0x2, 0x0, 0x0, 0x0, 0x0, 0x0,
                                          0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
                                          0x0, 0x0, 0x0, 0x0};
  std::string nvme_encoded_output;
  base::Base64Encode(std::string(std::begin(kLongSelfTestRunning),
                                 std::end(kLongSelfTestRunning)),
                     &nvme_encoded_output);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(RunRoutinePopulate()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             NvmeSelfTestRoutine::kNvmeSelfTestRoutineRunning);

  // Progress(byte-0): Bits 3:0, 0 means test is completed.
  // Status(byte-4): Bits 7:4, 2 for long-time test; Bits 3:0, 0 means passed.
  const uint8_t kLongSelfTestSuccess[] = {0x0, 0x0, 0x0, 0x0, 0x20, 0x0,
                                          0x0, 0x0, 0x0, 0x0, 0x0,  0x0,
                                          0x0, 0x0, 0x0, 0x0};
  nvme_encoded_output.clear();
  base::Base64Encode(std::string(std::begin(kLongSelfTestSuccess),
                                 std::end(kLongSelfTestSuccess)),
                     &nvme_encoded_output);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
      NvmeSelfTestRoutine::kSelfTestRoutineCompleteLog[0x0]);
}

// Test that the NvmeSelfTest routine for long-time fails if result from NVMe
// is failed.
TEST_F(NvmeSelfTestRoutineTest, LongSelfTestError) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunLongSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeLongSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  // Progress(byte-0): Bits 3:0, 0 means test is completed.
  // Status(byte-4): Bits 7:4, 2 for long-time test; Bits 3:0, 4 means test
  // failed and error index is 4.
  const uint8_t kLongSelfTestError[] = {0x0, 0x0, 0x0, 0x0, 0x24, 0x0,
                                        0x0, 0x0, 0x0, 0x0, 0x0,  0x0,
                                        0x0, 0x0, 0x0, 0x0};
  std::string nvme_encoded_output;
  base::Base64Encode(
      std::string(std::begin(kLongSelfTestError), std::end(kLongSelfTestError)),
      &nvme_encoded_output);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(nvme_encoded_output, nullptr); }));
  VerifyNonInteractiveUpdate(
      RunRoutinePopulate()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
      NvmeSelfTestRoutine::kSelfTestRoutineCompleteLog[0x4]);
}

// Tests that the NvmeSelfTest routine fails if debugd returns with an error.
TEST_F(NvmeSelfTestRoutineTest, DebugdError) {
  const char kDebugdErrorMessage[] = "Debugd mock error for testing";
  const brillo::ErrorPtr kError =
      brillo::Error::Create(FROM_HERE, "", "", kDebugdErrorMessage);
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunLongSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeLongSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run("", kError.get()); }));
  RunRoutineStart();
  VerifyNonInteractiveUpdate(RunRoutinePopulate()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kDebugdErrorMessage);
}

// Tests that the NvmeSelfTest routine fails if debugd returns with an error
// while cancelling.
TEST_F(NvmeSelfTestRoutineTest, DebugdErrorForCancelling) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunLongSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeLongSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kRunning);

  const char kDebugdErrorMessage[] = "Debugd mock error for cancelling";
  const brillo::ErrorPtr kError =
      brillo::Error::Create(FROM_HERE, "", "", kDebugdErrorMessage);
  EXPECT_CALL(debugd_adapter_, StopNvmeSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run("", kError.get()); }));
  RunRoutineCancel();
  VerifyNonInteractiveUpdate(RunRoutinePopulate()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kDebugdErrorMessage);
}

// Tests that the NvmeSelfTest routine fails if debugd returns with an error
// while getting progress.
TEST_F(NvmeSelfTestRoutineTest, DebugdErrorForGettingProgress) {
  CreateSelfTestRoutine(NvmeSelfTestRoutine::kRunLongSelfTest);
  EXPECT_CALL(debugd_adapter_, RunNvmeLongSelfTest(_))
      .WillOnce(WithArg<0>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run(kStartSuccess, nullptr); }));
  RunRoutineStart();

  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kRunning);

  const char kDebugdErrorMessage[] = "Debugd mock error for getting progress";
  const brillo::ErrorPtr kError =
      brillo::Error::Create(FROM_HERE, "", "", kDebugdErrorMessage);
  EXPECT_CALL(debugd_adapter_,
              GetNvmeLog(NvmeSelfTestRoutine::kNvmeLogPageId,
                         NvmeSelfTestRoutine::kNvmeLogDataLength,
                         NvmeSelfTestRoutine::kNvmeLogRawBinary, _))
      .WillOnce(WithArg<3>(
          [&](const base::Callback<void(const std::string&, brillo::Error*)>&
                  callback) { callback.Run("", kError.get()); }));
  VerifyNonInteractiveUpdate(RunRoutinePopulate()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kDebugdErrorMessage);
}

}  // namespace diagnostics
