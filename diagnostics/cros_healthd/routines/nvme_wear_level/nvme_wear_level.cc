// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/nvme_wear_level/nvme_wear_level.h"

#include <utility>
#include <vector>

#include <base/base64.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/string_split.h>

#include "diagnostics/common/mojo_utils.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char NvmeWearLevelRoutine::kNvmeWearLevelRoutineThresholdError[] =
    "Wear-level status: ERROR, threshold in percentage should be under 100.";
constexpr char NvmeWearLevelRoutine::kNvmeWearLevelRoutineGetInfoError[] =
    "Wear-level status: ERROR, cannot get wear level info.";
constexpr char NvmeWearLevelRoutine::kNvmeWearLevelRoutineFailed[] =
    "Wear-level status: FAILED, exceed the limitation value.";
constexpr char NvmeWearLevelRoutine::kNvmeWearLevelRoutineSuccess[] =
    "Wear-level status: PASS.";

// Page ID 202 is Dell specific for NVMe wear level status.
constexpr uint32_t NvmeWearLevelRoutine::kNvmeLogPageId = 202;
constexpr uint32_t NvmeWearLevelRoutine::kNvmeLogDataLength = 16;
constexpr bool NvmeWearLevelRoutine::kNvmeLogRawBinary = true;

NvmeWearLevelRoutine::NvmeWearLevelRoutine(DebugdAdapter* debugd_adapter,
                                           uint32_t wear_level_threshold)
    : debugd_adapter_(debugd_adapter),
      wear_level_threshold_(wear_level_threshold) {
  DCHECK(debugd_adapter_);
}

NvmeWearLevelRoutine::~NvmeWearLevelRoutine() = default;

void NvmeWearLevelRoutine::Start() {
  if (wear_level_threshold_ >= 100) {
    LOG(ERROR) << "Invalid threshold value (valid: 0-99): "
               << wear_level_threshold_;
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, kNvmeWearLevelRoutineThresholdError);
    return;
  }

  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  auto result_callback =
      base::Bind(&NvmeWearLevelRoutine::OnDebugdResultCallback,
                 weak_ptr_routine_.GetWeakPtr());
  debugd_adapter_->GetNvmeLog(/*page_id=*/kNvmeLogPageId,
                              /*length=*/kNvmeLogDataLength,
                              /*raw_binary=*/kNvmeLogRawBinary,
                              result_callback);
}

// The wear-level check can only be started.
void NvmeWearLevelRoutine::Resume() {}
void NvmeWearLevelRoutine::Cancel() {}

void NvmeWearLevelRoutine::UpdateStatus(
    chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status,
    uint32_t percent,
    std::string msg) {
  status_ = status;
  percent_ = percent;
  status_message_ = std::move(msg);
}

void NvmeWearLevelRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status = status_;
  update.status_message = status_message_;

  response->routine_update_union->set_noninteractive_update(update.Clone());
  response->progress_percent = percent_;

  if (include_output && !output_dict_.DictEmpty()) {
    // If routine status is not at completed/cancelled then prints the debugd
    // raw data with output.
    if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kPassed &&
        status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled) {
      std::string json;
      base::JSONWriter::WriteWithOptions(
          output_dict_, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);
      response->output =
          CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(json));
    }
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum NvmeWearLevelRoutine::GetStatus() {
  return status_;
}

void NvmeWearLevelRoutine::OnDebugdResultCallback(const std::string& result,
                                                  brillo::Error* error) {
  if (error) {
    LOG(ERROR) << "Debugd error: " << error->GetMessage();
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, error->GetMessage());
    return;
  }

  base::Value result_dict(base::Value::Type::DICTIONARY);
  result_dict.SetStringKey("rawData", result);
  output_dict_.SetKey("resultDetails", std::move(result_dict));
  std::string decoded_output;

  if (!base::Base64Decode(result, &decoded_output)) {
    LOG(ERROR) << "Base64 decoding failed. Base64 data: " << result;
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, kNvmeWearLevelRoutineGetInfoError);
    return;
  }

  if (decoded_output.length() != kNvmeLogDataLength) {
    LOG(ERROR) << "String size is not as expected(" << kNvmeLogDataLength
               << "). Size: " << decoded_output.length();
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                 /*percent=*/100, kNvmeWearLevelRoutineGetInfoError);
    return;
  }

  const uint32_t level = static_cast<uint32_t>(decoded_output[5]);

  if (level >= wear_level_threshold_) {
    LOG(INFO) << "Wear level status is higher than threshold. Level: " << level
              << ", threshold: " << wear_level_threshold_;
    UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                 /*percent=*/100, kNvmeWearLevelRoutineFailed);
    return;
  }

  UpdateStatus(mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
               /*percent=*/100, kNvmeWearLevelRoutineSuccess);
}

}  // namespace diagnostics
