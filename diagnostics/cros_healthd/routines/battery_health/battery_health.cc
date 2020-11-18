// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_health/battery_health.h"

#include <cstdint>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/values.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

bool TestWearPercentage(
    const power_manager::PowerSupplyProperties& power_supply_proto,
    uint8_t percent_battery_wear_allowed,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* result_dict) {
  DCHECK(status);
  DCHECK(status_message);
  DCHECK(result_dict);
  DCHECK(result_dict->is_dict());

  double capacity = power_supply_proto.battery_charge_full();
  double design_capacity = power_supply_proto.battery_charge_full_design();

  if (percent_battery_wear_allowed > 100) {
    *status_message = kBatteryHealthInvalidParametersMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    return false;
  }

  if (!power_supply_proto.has_battery_charge_full() ||
      !power_supply_proto.has_battery_charge_full_design()) {
    *status_message = kBatteryHealthFailedCalculatingWearPercentageMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    return false;
  }

  // Cap the wear percentage at 0. There are cases where the capacity can be
  // higher than the design capacity, due to variance in batteries or vendors
  // setting conservative design capacities.
  uint32_t wear_percentage =
      capacity > design_capacity ? 0 : 100 - capacity * 100 / design_capacity;

  result_dict->SetIntKey("wearPercentage", static_cast<int>(wear_percentage));
  if (wear_percentage > percent_battery_wear_allowed) {
    *status_message = kBatteryHealthExcessiveWearMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return false;
  }

  return true;
}

bool TestCycleCount(
    const power_manager::PowerSupplyProperties& power_supply_proto,
    uint32_t maximum_cycle_count,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* result_dict) {
  DCHECK(status);
  DCHECK(status_message);
  DCHECK(result_dict);
  DCHECK(result_dict->is_dict());

  google::protobuf::int64 cycle_count =
      power_supply_proto.battery_cycle_count();
  if (!power_supply_proto.has_battery_cycle_count()) {
    *status_message = kBatteryHealthFailedReadingCycleCountMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    return false;
  }

  result_dict->SetIntKey("cycleCount", static_cast<int>(cycle_count));
  if (cycle_count > maximum_cycle_count) {
    *status_message = kBatteryHealthExcessiveCycleCountMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return false;
  }

  return true;
}

void RunBatteryHealthRoutine(Context* const context,
                             uint32_t maximum_cycle_count,
                             uint8_t percent_battery_wear_allowed,
                             mojo_ipc::DiagnosticRoutineStatusEnum* status,
                             std::string* status_message,
                             base::Value* output_dict) {
  DCHECK(context);
  DCHECK(status);
  DCHECK(status_message);
  DCHECK(output_dict);
  DCHECK(output_dict->is_dict());

  base::Value result_dict(base::Value::Type::DICTIONARY);

  base::Optional<power_manager::PowerSupplyProperties> response =
      context->powerd_adapter()->GetPowerSupplyProperties();
  if (!response.has_value()) {
    *status_message = kPowerdPowerSupplyPropertiesFailedMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    LOG(ERROR) << kPowerdPowerSupplyPropertiesFailedMessage;
    return;
  }

  auto power_supply_proto = response.value();
  auto present =
      power_supply_proto.battery_state() ==
              power_manager::PowerSupplyProperties_BatteryState_NOT_PRESENT
          ? 0
          : 1;
  result_dict.SetIntKey("present", present);
  result_dict.SetStringKey("manufacturer", power_supply_proto.battery_vendor());
  result_dict.SetIntKey("currentNowA", power_supply_proto.battery_current());
  result_dict.SetStringKey("status", power_supply_proto.battery_status());
  result_dict.SetIntKey("voltageNowV", power_supply_proto.battery_voltage());
  result_dict.SetIntKey("chargeFullAh",
                        power_supply_proto.battery_charge_full());
  result_dict.SetIntKey("chargeFullDesignAh",
                        power_supply_proto.battery_charge_full_design());
  result_dict.SetIntKey("chargeNowAh", power_supply_proto.battery_charge());

  if (TestWearPercentage(power_supply_proto, percent_battery_wear_allowed,
                         status, status_message, &result_dict) &&
      TestCycleCount(power_supply_proto, maximum_cycle_count, status,
                     status_message, &result_dict)) {
    *status_message = kBatteryHealthRoutinePassedMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
  }

  if (result_dict.DictEmpty())
    return;

  output_dict->SetKey("resultDetails", std::move(result_dict));
}

}  // namespace

const char kBatteryHealthInvalidParametersMessage[] =
    "Invalid battery health routine parameters.";
const char kBatteryHealthFailedCalculatingWearPercentageMessage[] =
    "Could not get wear percentage.";
const char kBatteryHealthExcessiveWearMessage[] = "Battery is over-worn.";
const char kBatteryHealthFailedReadingCycleCountMessage[] =
    "Could not get cycle count.";
const char kBatteryHealthExcessiveCycleCountMessage[] =
    "Battery cycle count is too high.";
const char kBatteryHealthRoutinePassedMessage[] = "Routine passed.";

const uint32_t kBatteryHealthDefaultMaximumCycleCount = 1000;
const uint8_t kBatteryHealthDefaultPercentBatteryWearAllowed = 50;

std::unique_ptr<DiagnosticRoutine> CreateBatteryHealthRoutine(
    Context* const context,
    const base::Optional<uint32_t>& maximum_cycle_count,
    const base::Optional<uint8_t>& percent_battery_wear_allowed) {
  return std::make_unique<SimpleRoutine>(base::BindOnce(
      &RunBatteryHealthRoutine, context,
      maximum_cycle_count.value_or(kBatteryHealthDefaultMaximumCycleCount),
      percent_battery_wear_allowed.value_or(
          kBatteryHealthDefaultPercentBatteryWearAllowed)));
}

}  // namespace diagnostics
