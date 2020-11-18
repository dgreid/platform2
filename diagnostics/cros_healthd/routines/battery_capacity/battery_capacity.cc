// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_capacity/battery_capacity.h"

#include <cstdint>
#include <string>

#include <base/callback.h>
#include <base/logging.h>
#include <base/values.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Conversion factor from Ah to mAh.
constexpr uint32_t kAhTomAhMultiplier = 1000;

// We include |output_dict| here to satisfy SimpleRoutine - the battery capacity
// routine never includes an output.
void RunBatteryCapacityRoutine(Context* const context,
                               uint32_t low_mah,
                               uint32_t high_mah,
                               mojo_ipc::DiagnosticRoutineStatusEnum* status,
                               std::string* status_message,
                               base::Value* output_dict) {
  DCHECK(context);
  DCHECK(status);
  DCHECK(status_message);

  if (low_mah > high_mah) {
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    *status_message = kBatteryCapacityRoutineParametersInvalidMessage;
    return;
  }

  base::Optional<power_manager::PowerSupplyProperties> response =
      context->powerd_adapter()->GetPowerSupplyProperties();
  if (!response.has_value()) {
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    *status_message = kPowerdPowerSupplyPropertiesFailedMessage;
    return;
  }

  auto power_supply_proto = response.value();
  double charge_full_design_ah =
      power_supply_proto.battery_charge_full_design();

  // Conversion is necessary because the inputs are given in mAh, whereas the
  // design capacity is reported in Ah.
  uint32_t charge_full_design_mah = charge_full_design_ah * kAhTomAhMultiplier;
  if (!(charge_full_design_mah >= low_mah) ||
      !(charge_full_design_mah <= high_mah)) {
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    *status_message = kBatteryCapacityRoutineFailedMessage;
    return;
  }

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
  *status_message = kBatteryCapacityRoutineSucceededMessage;
  return;
}

}  // namespace

const char kBatteryCapacityRoutineParametersInvalidMessage[] =
    "Invalid BatteryCapacityRoutineParameters.";
const char kBatteryCapacityRoutineSucceededMessage[] =
    "Battery design capacity within given limits.";
const char kBatteryCapacityRoutineFailedMessage[] =
    "Battery design capacity not within given limits.";

const uint32_t kBatteryCapacityDefaultLowMah = 1000;
const uint32_t kBatteryCapacityDefaultHighMah = 10000;

std::unique_ptr<DiagnosticRoutine> CreateBatteryCapacityRoutine(
    Context* const context,
    const base::Optional<uint32_t>& low_mah,
    const base::Optional<uint32_t>& high_mah) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunBatteryCapacityRoutine, context,
                     low_mah.value_or(kBatteryCapacityDefaultLowMah),
                     high_mah.value_or(kBatteryCapacityDefaultHighMah)));
}

}  // namespace diagnostics
