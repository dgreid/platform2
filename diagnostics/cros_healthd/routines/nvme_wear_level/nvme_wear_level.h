// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NVME_WEAR_LEVEL_NVME_WEAR_LEVEL_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NVME_WEAR_LEVEL_NVME_WEAR_LEVEL_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/values.h>
#include <brillo/errors/error.h>

#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// The NvmeWearLevelRoutine routine to examine wear level against input
// threshold.
class NvmeWearLevelRoutine final : public DiagnosticRoutine {
 public:
  static const char kNvmeWearLevelRoutineThresholdError[];
  static const char kNvmeWearLevelRoutineGetInfoError[];
  static const char kNvmeWearLevelRoutineFailed[];
  static const char kNvmeWearLevelRoutineSuccess[];
  static const uint32_t kNvmeLogPageId;
  static const uint32_t kNvmeLogDataLength;
  static const bool kNvmeLogRawBinary;

  NvmeWearLevelRoutine(DebugdAdapter* debugd_adapter,
                       uint32_t wear_level_threshold);
  NvmeWearLevelRoutine(const NvmeWearLevelRoutine&) = delete;
  NvmeWearLevelRoutine& operator=(const NvmeWearLevelRoutine&) = delete;
  ~NvmeWearLevelRoutine() override;

  // DiagnosticRoutine overrides:
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(
      chromeos::cros_healthd::mojom::RoutineUpdate* response,
      bool include_output) override;
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus()
      override;

 private:
  void OnDebugdResultCallback(const std::string& result, brillo::Error* error);
  // Updates status_, percent_, status_message_ at the same moment to ensure
  // each of them corresponds with the others.
  void UpdateStatus(
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status,
      uint32_t percent,
      std::string msg);

  DebugdAdapter* const debugd_adapter_ = nullptr;
  const uint32_t wear_level_threshold_ = 0;

  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_ =
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kReady;
  uint32_t percent_ = 0;
  base::Value output_dict_{base::Value::Type::DICTIONARY};
  std::string status_message_;

  base::WeakPtrFactory<NvmeWearLevelRoutine> weak_ptr_routine_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NVME_WEAR_LEVEL_NVME_WEAR_LEVEL_H_
