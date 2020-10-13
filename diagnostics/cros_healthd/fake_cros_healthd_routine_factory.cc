// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake_cros_healthd_routine_factory.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "diagnostics/common/mojo_utils.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

// When any of a FakeDiagnosticRoutine's |num_expected_start_calls_|,
// |num_expected_resume_calls_| or |num_expected_cancel_calls_| is this value,
// then calls to the corresponding function will not be tracked.
constexpr int kNumCallsNotTracked = -1;

class FakeDiagnosticRoutine : public DiagnosticRoutine {
 public:
  FakeDiagnosticRoutine(mojo_ipc::DiagnosticRoutineStatusEnum status,
                        uint32_t progress_percent,
                        const std::string& output,
                        int num_expected_start_calls,
                        int num_expected_resume_calls,
                        int num_expected_cancel_calls);
  // DiagnosticRoutine overrides:
  ~FakeDiagnosticRoutine() override;
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(mojo_ipc::RoutineUpdate* response,
                            bool include_output) override;
  mojo_ipc::DiagnosticRoutineStatusEnum GetStatus() override;

 private:
  // Value returned by GetStatus().
  const mojo_ipc::DiagnosticRoutineStatusEnum status_;
  // Values used in PopulateStatusUpdate(). Common to both interactive and
  // noninteractive routines.
  const uint32_t progress_percent_;
  const std::string output_;
  // Number of times that Start() is expected to be called throughout the life
  // of this routine.
  const int num_expected_start_calls_;
  // Number of times that Resume() is expected to be called throughout the life
  // of this routine.
  const int num_expected_resume_calls_;
  // Number of times that Cancel() is expected to be called throughout the life
  // of this routine.
  const int num_expected_cancel_calls_;
  // Number of times that Start() was called throughout the life of this
  // routine.
  int num_actual_start_calls_ = 0;
  // Number of times that Resume() was called throughout the life of this
  // routine.
  int num_actual_resume_calls_ = 0;
  // Number of times that Cancel() was called throughout the life of this
  // routine.
  int num_actual_cancel_calls_ = 0;
};

FakeDiagnosticRoutine::FakeDiagnosticRoutine(
    mojo_ipc::DiagnosticRoutineStatusEnum status,
    uint32_t progress_percent,
    const std::string& output,
    int num_expected_start_calls,
    int num_expected_resume_calls,
    int num_expected_cancel_calls)
    : status_(status),
      progress_percent_(progress_percent),
      output_(output),
      num_expected_start_calls_(num_expected_start_calls),
      num_expected_resume_calls_(num_expected_resume_calls),
      num_expected_cancel_calls_(num_expected_cancel_calls) {}

FakeDiagnosticRoutine::~FakeDiagnosticRoutine() {
  if (num_expected_start_calls_ != kNumCallsNotTracked)
    EXPECT_EQ(num_expected_start_calls_, num_actual_start_calls_);
  if (num_expected_resume_calls_ != kNumCallsNotTracked)
    EXPECT_EQ(num_expected_resume_calls_, num_actual_resume_calls_);
  if (num_expected_cancel_calls_ != kNumCallsNotTracked)
    EXPECT_EQ(num_expected_cancel_calls_, num_actual_cancel_calls_);
}

void FakeDiagnosticRoutine::Start() {
  num_actual_start_calls_++;
}

void FakeDiagnosticRoutine::Resume() {
  num_actual_resume_calls_++;
}

void FakeDiagnosticRoutine::Cancel() {
  num_actual_cancel_calls_++;
}

void FakeDiagnosticRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  DCHECK(response);

  response->progress_percent = progress_percent_;
  response->output = CreateReadOnlySharedMemoryRegionMojoHandle(output_);
}

mojo_ipc::DiagnosticRoutineStatusEnum FakeDiagnosticRoutine::GetStatus() {
  return status_;
}

class FakeNonInteractiveDiagnosticRoutine final : public FakeDiagnosticRoutine {
 public:
  FakeNonInteractiveDiagnosticRoutine(
      mojo_ipc::DiagnosticRoutineStatusEnum status,
      const std::string& status_message,
      uint32_t progress_percent,
      const std::string& output,
      int num_expected_start_calls,
      int num_expected_resume_calls,
      int num_expected_cancel_calls);
  FakeNonInteractiveDiagnosticRoutine(
      const FakeNonInteractiveDiagnosticRoutine&) = delete;
  FakeNonInteractiveDiagnosticRoutine& operator=(
      const FakeNonInteractiveDiagnosticRoutine&) = delete;
  ~FakeNonInteractiveDiagnosticRoutine() override;

  // FakeDiagnosticRoutine overrides:
  void PopulateStatusUpdate(mojo_ipc::RoutineUpdate* response,
                            bool include_output) override;

 private:
  // Used to populate the noninteractive_routine_update for calls to
  // PopulateStatusUpdate.
  const std::string status_message_;
};

FakeNonInteractiveDiagnosticRoutine::FakeNonInteractiveDiagnosticRoutine(
    mojo_ipc::DiagnosticRoutineStatusEnum status,
    const std::string& status_message,
    uint32_t progress_percent,
    const std::string& output,
    int num_expected_start_calls,
    int num_expected_resume_calls,
    int num_expected_cancel_calls)
    : FakeDiagnosticRoutine(status,
                            progress_percent,
                            output,
                            num_expected_start_calls,
                            num_expected_resume_calls,
                            num_expected_cancel_calls),
      status_message_(status_message) {}

FakeNonInteractiveDiagnosticRoutine::~FakeNonInteractiveDiagnosticRoutine() =
    default;

void FakeNonInteractiveDiagnosticRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  FakeDiagnosticRoutine::PopulateStatusUpdate(response, include_output);
  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status = GetStatus();
  update.status_message = status_message_;
  response->routine_update_union->set_noninteractive_update(update.Clone());
}

}  // namespace

FakeCrosHealthdRoutineFactory::FakeCrosHealthdRoutineFactory()
    : num_expected_start_calls_(kNumCallsNotTracked),
      num_expected_resume_calls_(kNumCallsNotTracked),
      num_expected_cancel_calls_(kNumCallsNotTracked) {}
FakeCrosHealthdRoutineFactory::~FakeCrosHealthdRoutineFactory() = default;

void FakeCrosHealthdRoutineFactory::SetRoutineExpectations(
    int num_expected_start_calls,
    int num_expected_resume_calls,
    int num_expected_cancel_calls) {
  num_expected_start_calls_ = num_expected_start_calls;
  num_expected_resume_calls_ = num_expected_resume_calls;
  num_expected_cancel_calls_ = num_expected_cancel_calls;
}

void FakeCrosHealthdRoutineFactory::SetNonInteractiveStatus(
    mojo_ipc::DiagnosticRoutineStatusEnum status,
    const std::string& status_message,
    uint32_t progress_percent,
    const std::string& output) {
  next_routine_ = std::make_unique<FakeNonInteractiveDiagnosticRoutine>(
      status, status_message, progress_percent, output,
      num_expected_start_calls_, num_expected_resume_calls_,
      num_expected_cancel_calls_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeUrandomRoutine(uint32_t length_seconds) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeBatteryCapacityRoutine(uint32_t low_mah,
                                                          uint32_t high_mah) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeBatteryHealthRoutine() {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeSmartctlCheckRoutine() {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeAcPowerRoutine(
    chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeCpuCacheRoutine(
    base::TimeDelta exec_duration) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeCpuStressRoutine(
    base::TimeDelta exec_duration) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeFloatingPointAccuracyRoutine(
    base::TimeDelta exec_duration) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeNvmeWearLevelRoutine(
    DebugdAdapter* debugd_adapter, uint32_t wear_level_threshold) {
  DCHECK(debugd_adapter);
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeNvmeSelfTestRoutine(
    DebugdAdapter* debugd_adapter,
    chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type) {
  DCHECK(debugd_adapter);
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakePrimeSearchRoutine(
    base::TimeDelta exec_duration, uint64_t max_num) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeBatteryDischargeRoutine(
    base::TimeDelta exec_duration, uint32_t maximum_discharge_percent_allowed) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeBatteryChargeRoutine(
    base::TimeDelta exec_duration, uint32_t minimum_charge_percent_required) {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeMemoryRoutine() {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeLanConnectivityRoutine() {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeSignalStrengthRoutine() {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeGatewayCanBePingedRoutine() {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeHasSecureWiFiConnectionRoutine() {
  return std::move(next_routine_);
}

std::unique_ptr<DiagnosticRoutine>
FakeCrosHealthdRoutineFactory::MakeDnsResolverPresentRoutine() {
  return std::move(next_routine_);
}

}  // namespace diagnostics
