// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_charge/battery_charge.h"

#include <inttypes.h>

#include <cstdint>
#include <string>
#include <utility>

#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/threading/thread_task_runner_handle.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/battery_charge/battery_charge_constants.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

BatteryChargeRoutine::BatteryChargeRoutine(
    Context* const context,
    base::TimeDelta exec_duration,
    uint32_t minimum_charge_percent_required,
    const base::TickClock* tick_clock)
    : context_(context),
      status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady),
      exec_duration_(exec_duration),
      minimum_charge_percent_required_(minimum_charge_percent_required) {
  if (tick_clock) {
    tick_clock_ = tick_clock;
  } else {
    default_tick_clock_ = std::make_unique<base::DefaultTickClock>();
    tick_clock_ = default_tick_clock_.get();
  }
  DCHECK(context_);
  DCHECK(tick_clock_);
}

BatteryChargeRoutine::~BatteryChargeRoutine() = default;

void BatteryChargeRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  // Transition to waiting so the user can plug in the charger if necessary.
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  CalculateProgressPercent();
}

void BatteryChargeRoutine::Resume() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting);
  status_ = RunBatteryChargeRoutine();
  if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kRunning)
    LOG(ERROR) << "Routine failed: " << status_message_;
}

void BatteryChargeRoutine::Cancel() {
  // Cancel the routine if it hasn't already finished.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kError) {
    return;
  }

  CalculateProgressPercent();

  callback_.Cancel();
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled;
  status_message_ = kBatteryChargeRoutineCancelledMessage;
}

void BatteryChargeRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting) {
    mojo_ipc::InteractiveRoutineUpdate interactive_update;
    interactive_update.user_message =
        mojo_ipc::DiagnosticRoutineUserMessageEnum::kPlugInACPower;
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
  if (include_output && !output_.DictEmpty()) {
    std::string json;
    base::JSONWriter::WriteWithOptions(
        output_, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(json));
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum BatteryChargeRoutine::GetStatus() {
  return status_;
}

void BatteryChargeRoutine::CalculateProgressPercent() {
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
BatteryChargeRoutine::RunBatteryChargeRoutine() {
  base::Optional<power_manager::PowerSupplyProperties> response =
      context_->powerd_adapter()->GetPowerSupplyProperties();
  if (!response.has_value()) {
    status_message_ = kPowerdPowerSupplyPropertiesFailedMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }
  auto power_supply_proto = response.value();

  if (power_supply_proto.battery_state() !=
      power_manager::PowerSupplyProperties_BatteryState_CHARGING) {
    status_message_ = kBatteryChargeRoutineNotChargingMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  double beginning_charge_percent = power_supply_proto.battery_percent();

  if (beginning_charge_percent + minimum_charge_percent_required_ > 100) {
    status_message_ = kBatteryChargeRoutineInvalidParametersMessage;
    base::Value error_dict(base::Value::Type::DICTIONARY);
    error_dict.SetKey("startingBatteryChargePercent",
                      base::Value(beginning_charge_percent));
    error_dict.SetKey(
        "chargePercentRequested",
        base::Value(static_cast<int>(minimum_charge_percent_required_)));
    output_.SetKey("errorDetails", std::move(error_dict));
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  start_ticks_ = tick_clock_->NowTicks();

  callback_.Reset(base::Bind(&BatteryChargeRoutine::DetermineRoutineResult,
                             weak_ptr_factory_.GetWeakPtr(),
                             beginning_charge_percent));
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, callback_.callback(), exec_duration_);

  status_message_ = kBatteryChargeRoutineRunningMessage;
  return mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
}

void BatteryChargeRoutine::DetermineRoutineResult(
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

  if (ending_charge_percent < beginning_charge_percent) {
    status_message_ = kBatteryChargeRoutineNotChargingMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    LOG(ERROR) << kBatteryChargeRoutineNotChargingMessage;
    return;
  }

  double charge_percent = ending_charge_percent - beginning_charge_percent;
  base::Value result_dict(base::Value::Type::DICTIONARY);
  result_dict.SetKey("chargePercent", base::Value(charge_percent));
  output_.SetKey("resultDetails", std::move(result_dict));
  if (charge_percent < minimum_charge_percent_required_) {
    status_message_ = kBatteryChargeRoutineFailedInsufficientChargeMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return;
  }

  status_message_ = kBatteryChargeRoutineSucceededMessage;
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
}

}  // namespace diagnostics
