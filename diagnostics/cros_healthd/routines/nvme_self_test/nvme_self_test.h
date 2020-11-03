// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NVME_SELF_TEST_NVME_SELF_TEST_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NVME_SELF_TEST_NVME_SELF_TEST_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/values.h>
#include <brillo/errors/error.h>

#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// Launches self-test of NVMe for a short time or long period. This routine
// fetches the progress status by parsing message from NVMe log page ID 6.
// Please refer to https://nvmexpress.org/wp-content/uploads/NVM_Express_
// Revision_1.3.pdf, Figure 98 "Device Self-test log" and Figure 99 "Self-test
// Result Data Structure" from 5.14.1.6.
class NvmeSelfTestRoutine final : public DiagnosticRoutine {
 public:
  static const char kNvmeSelfTestRoutineStarted[];
  static const char kNvmeSelfTestRoutineStartError[];
  static const char kNvmeSelfTestRoutineAbortionError[];
  static const char kNvmeSelfTestRoutineRunning[];
  static const char kNvmeSelfTestRoutineGetProgressFailed[];
  static const char kNvmeSelfTestRoutineCancelled[];

  // The error message captured from NVMe controller.
  // Reference: "Figure 99; Get Log Page - self-test Result Data Structure"
  // from https://nvmexpress.org/wp-content/uploads/NVM-Express-1_3b-2018.05.04
  // -ratified.pdf.
  static const char* const kSelfTestRoutineCompleteLog[];
  static const char kSelfTestRoutineCompleteUnknownStatus[];
  static const size_t kSelfTestRoutineCompleteLogSize;

  static const uint32_t kNvmeLogPageId;
  static const uint32_t kNvmeLogDataLength;
  static const bool kNvmeLogRawBinary;

  enum SelfTestType {
    // In NVMe spec, the referred byte(Log Page 6, byte 4, Bit 7:4) indiecates
    // the self-test type. 0: reserved; 1: short self-test; 2: long self-test.
    kRunShortSelfTest = 1, /* Launch short-time self-test */
    kRunLongSelfTest = 2,  /* Launch long-time self-test */
  };

  NvmeSelfTestRoutine(DebugdAdapter* debugd_adapter,
                      SelfTestType self_test_type);
  NvmeSelfTestRoutine(const NvmeSelfTestRoutine&) = delete;
  NvmeSelfTestRoutine& operator=(const NvmeSelfTestRoutine&) = delete;
  ~NvmeSelfTestRoutine() override;

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
  // Check if an error comes during communication to debugd.
  bool CheckDebugdError(brillo::Error* error);

  bool CheckSelfTestCompleted(uint8_t progress, uint8_t status) const;

  void OnDebugdNvmeSelfTestCancelCallback(const std::string& result,
                                          brillo::Error* error);
  void OnDebugdNvmeSelfTestStartCallback(const std::string& result,
                                         brillo::Error* error);
  void OnDebugdResultCallback(const std::string& result, brillo::Error* error);

  // Resets |output_dict_| to clear any previous input, then adds a new
  // dictionary with the key "rawData" and value |value|.
  void ResetOutputDictToValue(const std::string& value);

  // Update percent_, status_message_, status_ at the same moment in case
  // misinformation occurring.
  void UpdateStatus(
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status,
      uint32_t percent,
      std::string msg);

  // debugd_adapter_ is an unowned pointer and it should outlive this instance.
  DebugdAdapter* const debugd_adapter_ = nullptr;
  const SelfTestType self_test_type_;

  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_ =
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kReady;
  uint32_t percent_ = 0;
  base::Value output_dict_{base::Value::Type::DICTIONARY};
  std::string status_message_;

  base::WeakPtrFactory<NvmeSelfTestRoutine> weak_ptr_routine_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NVME_SELF_TEST_NVME_SELF_TEST_H_
