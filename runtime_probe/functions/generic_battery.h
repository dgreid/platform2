// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_GENERIC_BATTERY_H_
#define RUNTIME_PROBE_FUNCTIONS_GENERIC_BATTERY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Read battery information from sysfs.
//
// Those keys are expected to present no matter what types of battery is:
//   'manufacturer', 'model_name', 'technology', 'type'
// Those keys are optional:
//   'capacity', 'capacity_level', 'charge_full', 'charge_full_design',
//   'charge_now', 'current_now', 'cycle_count', 'present', 'serial_number',
//   'status', voltage_min_design', 'voltage_now'
class GenericBattery : public ProbeFunction {
 public:
  NAME_PROBE_FUNCTION("generic_battery");

  static constexpr auto FromKwargsValue = FromEmptyKwargsValue<GenericBattery>;

  DataType Eval() const override;

  int EvalInHelper(std::string*) const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_GENERIC_BATTERY_H_
