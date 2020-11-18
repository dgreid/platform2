// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge.h"

#include <inttypes.h>

#include <cstdint>
#include <string>
#include <utility>

#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/threading/thread_task_runner_handle.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge_constants.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

BatteryDischargeRoutine::BatteryDischargeRoutine(
    Context* const context,
    base::TimeDelta exec_duration,
    uint32_t maximum_discharge_percent_allowed,
    const base::TickClock* tick_clock)
    : context_(context),
      status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady),
      exec_duration_(exec_duration),
      maximum_discharge_percent_allowed_(maximum_discharge_percent_allowed) {
  if (tick_clock) {
    tick_clock_ = tick_clock;
  } else {
    default_tick_clock_ = std::make_unique<base::DefaultTickClock>();
    tick_clock_ = default_tick_clock_.get();
  }
  DCHECK(context_);
  DCHECK(tick_clock_);
}

BatteryDischargeRoutine::~BatteryDischargeRoutine() = default;

void BatteryDischargeRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  // Transition to waiting so the user can unplug the charger if necessary.
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  CalculateProgressPercent();
}

void BatteryDischargeRoutine::Resume() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting);
  status_ = RunBatteryDischargeRoutine();
  if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kRunning)
    LOG(ERROR) << "Routine failed: " << status_message_;
}

void BatteryDischargeRoutine::Cancel() {
  // Cancel the routine if it hasn't already finished.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kError) {
    return;
  }

  CalculateProgressPercent();

  callback_.Cancel();
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled;
  status_message_ = kBatteryDischargeRoutineCancelledMessage;
}

void BatteryDischargeRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting) {
    mojo_ipc::InteractiveRoutineUpdate interactive_update;
    interactive_update.user_message =
        mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnplugACPower;
    response->routine_update_union->set_interactive_update(
        interactive_update.Clone());
  } else {
    mojo_ipc::NonInteractiveRoutineUpdate noninteractive_update;
    noninteractive_update.status = status_;
    noninteractive_update.status_message = status_message_;

    response->routine_update_union->set_noninteractive_update(
        noninteractive_update.Clone());
  }

  CalculateProgressPercent();
  response->progress_percent = progress_percent_;
  if (include_output && !output_dict_.DictEmpty()) {
    std::string json;
    base::JSONWriter::WriteWithOptions(
        output_dict_, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(json));
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum BatteryDischargeRoutine::GetStatus() {
  return status_;
}

void BatteryDischargeRoutine::CalculateProgressPercent() {
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed) {
    // The routine has finished, so report 100.
    progress_percent_ = 100;
  } else if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kError &&
             status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled &&
             start_ticks_.has_value()) {
    progress_percent_ =
        100 * (tick_clock_->NowTicks() - start_ticks_.value()) / exec_duration_;
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum
BatteryDischargeRoutine::RunBatteryDischargeRoutine() {
  if (maximum_discharge_percent_allowed_ > 100) {
    status_message_ = kBatteryDischargeRoutineInvalidParametersMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  base::Optional<power_manager::PowerSupplyProperties> response =
      context_->powerd_adapter()->GetPowerSupplyProperties();
  if (!response.has_value()) {
    status_message_ = kPowerdPowerSupplyPropertiesFailedMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }
  auto power_supply_proto = response.value();

  if (power_supply_proto.battery_state() !=
      power_manager::PowerSupplyProperties_BatteryState_DISCHARGING) {
    status_message_ = kBatteryDischargeRoutineNotDischargingMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  double beginning_charge_percent = power_supply_proto.battery_percent();

  start_ticks_ = tick_clock_->NowTicks();

  callback_.Reset(base::Bind(&BatteryDischargeRoutine::DetermineRoutineResult,
                             weak_ptr_factory_.GetWeakPtr(),
                             beginning_charge_percent));
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, callback_.callback(), exec_duration_);

  status_message_ = kBatteryDischargeRoutineRunningMessage;
  return mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
}

void BatteryDischargeRoutine::DetermineRoutineResult(
    double beginning_charge_percent) {
  base::Optional<power_manager::PowerSupplyProperties> response =
      context_->powerd_adapter()->GetPowerSupplyProperties();
  if (!response.has_value()) {
    status_message_ = kPowerdPowerSupplyPropertiesFailedMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    LOG(ERROR) << kPowerdPowerSupplyPropertiesFailedMessage;
    return;
  }
  auto power_supply_proto = response.value();
  double ending_charge_percent = power_supply_proto.battery_percent();

  if (beginning_charge_percent < ending_charge_percent) {
    status_message_ = kBatteryDischargeRoutineNotDischargingMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    LOG(ERROR) << kBatteryDischargeRoutineNotDischargingMessage;
    return;
  }

  double discharge_percent = beginning_charge_percent - ending_charge_percent;
  base::Value result_dict(base::Value::Type::DICTIONARY);
  result_dict.SetDoubleKey("dischargePercent", discharge_percent);
  output_dict_.SetKey("resultDetails", std::move(result_dict));
  if (discharge_percent > maximum_discharge_percent_allowed_) {
    status_message_ = kBatteryDischargeRoutineFailedExcessiveDischargeMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return;
  }

  status_message_ = kBatteryDischargeRoutineSucceededMessage;
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
}

}  // namespace diagnostics
