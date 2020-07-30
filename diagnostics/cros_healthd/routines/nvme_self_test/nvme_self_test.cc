// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/nvme_self_test/nvme_self_test.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/base64.h>
#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>

#include "diagnostics/common/mojo_utils.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {
mojo_ipc::DiagnosticRoutineStatusEnum CheckSelfTestPassed(uint8_t status) {
  // Bits 3:0: 0x0 means pass without an error; otherwise the index of error.
  return (status & 0xf) == 0 ? mojo_ipc::DiagnosticRoutineStatusEnum::kPassed
                             : mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
}

// Check the result and return corresponding complete message.
std::string GetCompleteMessage(uint8_t status) {
  // Bits 3:0: indicates the result of the device self-test operation.
  status &= 0xf;
  // Check if result(complete value) is large than complete message array.
  if (status >= NvmeSelfTestRoutine::kSelfTestRoutineCompleteLogSize) {
    return NvmeSelfTestRoutine::kSelfTestRoutineCompleteUnknownStatus;
  } else {
    return NvmeSelfTestRoutine::kSelfTestRoutineCompleteLog[status];
  }
}

}  // namespace

constexpr char NvmeSelfTestRoutine::kNvmeSelfTestRoutineStarted[] =
    "SelfTest status: self-test started.";
constexpr char NvmeSelfTestRoutine::kNvmeSelfTestRoutineStartError[] =
    "SelfTest status: self-test failed to start.";
constexpr char NvmeSelfTestRoutine::kNvmeSelfTestRoutineAbortionError[] =
    "SelfTest status: ERROR, self-test abortion failed.";
constexpr char NvmeSelfTestRoutine::kNvmeSelfTestRoutineRunning[] =
    "SelfTest status: self-test is running.";
constexpr char NvmeSelfTestRoutine::kNvmeSelfTestRoutineGetProgressFailed[] =
    "SelfTest status: ERROR, cannot get percent info.";
constexpr char NvmeSelfTestRoutine::kNvmeSelfTestRoutineCancelled[] =
    "SelfTest status: self-test is cancelled.";

const char* const NvmeSelfTestRoutine::kSelfTestRoutineCompleteLog[] = {
    "SelfTest status: Test PASS.",
    "SelfTest status: Operation was aborted by Device Self-test command.",
    "SelfTest status: Operation was aborted by a Controller Level Reset.",
    "SelfTest status: Operation was aborted due to a removal of a namespace"
    " from the namespace inventory.",
    "SelfTest Status: Operation was aborted due to the processing of a Format"
    " NVM command.",
    "SelfTest status: A fatal error or unknown test error occurred while the"
    " controller was executing the device self-test operation and the operation"
    " did not complete.",
    "SelfTest status: Operation completed with a segment that failed and the"
    " segment that failed is not known.",
    "SelfTest status: Operation completed with one or more failed segments and"
    " the first segment that failed is indicated in the Segment Number field.",
    "SelfTest status: Operation was aborted for an unknown reason.",
};
constexpr char NvmeSelfTestRoutine::kSelfTestRoutineCompleteUnknownStatus[] =
    "SelfTest status: Unknown complete status.";
constexpr size_t NvmeSelfTestRoutine::kSelfTestRoutineCompleteLogSize =
    base::size(NvmeSelfTestRoutine::kSelfTestRoutineCompleteLog);

// Page ID 6 if for self-test progress info.
constexpr uint32_t NvmeSelfTestRoutine::kNvmeLogPageId = 6;
constexpr uint32_t NvmeSelfTestRoutine::kNvmeLogDataLength = 16;
constexpr bool NvmeSelfTestRoutine::kNvmeLogRawBinary = true;

NvmeSelfTestRoutine::NvmeSelfTestRoutine(DebugdAdapter* debugd_adapter,
                                         SelfTestType self_test_type)
    : debugd_adapter_(debugd_adapter), self_test_type_(self_test_type) {
  DCHECK(debugd_adapter_);
}

NvmeSelfTestRoutine::~NvmeSelfTestRoutine() = default;

void NvmeSelfTestRoutine::Start() {
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  auto result_callback =
      base::Bind(&NvmeSelfTestRoutine::OnDebugdNvmeSelfTestStartCallback,
                 weak_ptr_routine_.GetWeakPtr());

  switch (self_test_type_) {
    case kRunShortSelfTest:
      debugd_adapter_->RunNvmeShortSelfTest(result_callback);
      break;
    case kRunLongSelfTest:
      debugd_adapter_->RunNvmeLongSelfTest(result_callback);
      break;
  }
}

void NvmeSelfTestRoutine::Resume() {}
void NvmeSelfTestRoutine::Cancel() {
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kCancelling;
  auto result_callback =
      base::Bind(&NvmeSelfTestRoutine::OnDebugdNvmeSelfTestCancelCallback,
                 weak_ptr_routine_.GetWeakPtr());
  debugd_adapter_->StopNvmeSelfTest(result_callback);
}

void NvmeSelfTestRoutine::UpdateStatus(
    chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status,
    uint32_t percent,
    std::string msg) {
  status_ = status;
  percent_ = percent;
  status_message_ = std::move(msg);
}

void NvmeSelfTestRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  // Request progress info if routine is running with the percentage below 100.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kRunning &&
      percent_ < 100) {
    auto result_callback =
        base::Bind(&NvmeSelfTestRoutine::OnDebugdResultCallback,
                   weak_ptr_routine_.GetWeakPtr());
    debugd_adapter_->GetNvmeLog(/*page_id=*/kNvmeLogPageId,
                                /*length=*/kNvmeLogDataLength,
                                /*raw_binary=*/kNvmeLogRawBinary,
                                result_callback);
  }

  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status = status_;
  update.status_message = status_message_;

  response->routine_update_union->set_noninteractive_update(update.Clone());
  response->progress_percent = percent_;

  if (include_output) {
    // If routine status is not at completed/cancelled then prints the debugd
    // raw data with output.
    if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kPassed &&
        status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled) {
      response->output =
          CreateReadOnlySharedMemoryMojoHandle("Raw debugd data: " + output_);
    }
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum NvmeSelfTestRoutine::GetStatus() {
  return status_;
}

bool NvmeSelfTestRoutine::CheckDebugdError(brillo::Error* error) {
  if (error) {
    LOG(ERROR) << "Debugd error: " << error->GetMessage();
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, error->GetMessage());
    return true;
  }
  return false;
}

void NvmeSelfTestRoutine::OnDebugdNvmeSelfTestStartCallback(
    const std::string& result, brillo::Error* error) {
  if (CheckDebugdError(error))
    return;

  output_ = result;

  // Checks whether self-test has already been launched.
  if (!base::StartsWith(output_, "Device self-test started",
                        base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "self-test failed to start: " << output_;
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, kNvmeSelfTestRoutineStartError);
    return;
  }
  UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kRunning, /*percent=*/0,
               kNvmeSelfTestRoutineStarted);
}

void NvmeSelfTestRoutine::OnDebugdNvmeSelfTestCancelCallback(
    const std::string& result, brillo::Error* error) {
  if (CheckDebugdError(error))
    return;

  output_ = result;

  // Checks abortion if successful
  if (!base::StartsWith(output_, "Aborting device self-test operation",
                        base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "self-test abortion failed:" << output_;
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, kNvmeSelfTestRoutineAbortionError);
    return;
  }
  UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled,
               /*percent=*/100, kNvmeSelfTestRoutineCancelled);
}

bool NvmeSelfTestRoutine::CheckSelfTestCompleted(uint8_t progress,
                                                 uint8_t status) const {
  // |progress|: Bits 7:4 are reserved; Bits 3:0 indicates the status of
  // the current device self-test operation. |progress| is equal to 0 while
  // self-test has been completed.
  // |status|: Bits 7:4 indicates the type of operation: 0x1 for short-time
  // self-test, 0x2 for long-time self-test; Bits 3:0 indicates the result of
  // self-test.
  return (progress & 0xf) == 0 && (status >> 4) == self_test_type_;
}

void NvmeSelfTestRoutine::OnDebugdResultCallback(const std::string& result,
                                                 brillo::Error* error) {
  if (CheckDebugdError(error))
    return;

  output_ = result;
  std::string decoded_output;

  if (!base::Base64Decode(output_, &decoded_output)) {
    LOG(ERROR) << "Base64 decoding failed. Base64 data: " << output_;
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, kNvmeSelfTestRoutineGetProgressFailed);
    return;
  }

  if (decoded_output.length() != kNvmeLogDataLength) {
    LOG(ERROR) << "String size is not as expected(" << kNvmeLogDataLength
               << "). Size: " << decoded_output.length();
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, kNvmeSelfTestRoutineGetProgressFailed);
    return;
  }

  const uint8_t progress = static_cast<uint8_t>(decoded_output[0]);
  // Bits 6:0: percentage of self-test operation.
  const uint8_t percent = static_cast<uint8_t>(decoded_output[1]) & 0x7f;
  const uint8_t complete_status = static_cast<uint8_t>(decoded_output[4]);

  if (CheckSelfTestCompleted(progress, complete_status)) {
    UpdateStatus(CheckSelfTestPassed(complete_status), /*percent=*/100,
                 GetCompleteMessage(complete_status));
  } else if ((progress & 0xf) == self_test_type_) {
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kRunning, percent,
                 kNvmeSelfTestRoutineRunning);
  } else {
    // It's not readable if uint8_t variables are not cast to uint32_t.
    LOG(ERROR) << "No valid data is retrieved. progress: "
               << static_cast<uint32_t>(progress)
               << ", percent: " << static_cast<uint32_t>(percent)
               << ", status:" << static_cast<uint32_t>(complete_status);
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, kNvmeSelfTestRoutineGetProgressFailed);
  }
}

}  // namespace diagnostics
