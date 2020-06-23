/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
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

/* Read battery information from sysfs.
 *
 * Those keys are expected to present no matter what types of battery is:
 *   'manufacturer', 'model_name', 'technology', 'type'
 * Those keys are optional:
 *   'capacity', 'capacity_level', 'charge_full', 'charge_full_design',
 *   'charge_now', 'current_now', 'cycle_count', 'present', 'serial_number',
 *   'status', voltage_min_design', 'voltage_now'
 */
class GenericBattery : public ProbeFunction {
 public:
  static constexpr auto function_name = "generic_battery";

  /* Define a parser for this function.
   *
   * @args dict_value: a JSON dictionary to parse arguments from.
   *
   * @return pointer to new `GenericBattery` instance on success, nullptr
   *   otherwise.
   */
  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value) {
    if (dict_value.DictSize() != 0) {
      LOG(ERROR) << function_name << " doesn't take any argument.";
      return nullptr;
    }
    return std::make_unique<GenericBattery>();
  }

  std::string GetFunctionName() const override { return function_name; }

  DataType Eval() const override;
  int EvalInHelper(std::string*) const override;

 private:
  static ProbeFunction::Register<GenericBattery> register_;
};

/* Register the GenericBattery */
REGISTER_PROBE_FUNCTION(GenericBattery);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_GENERIC_BATTERY_H_
