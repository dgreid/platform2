/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef RUNTIME_PROBE_FUNCTIONS_INPUT_DEVICE_H_
#define RUNTIME_PROBE_FUNCTIONS_INPUT_DEVICE_H_

#include <memory>
#include <string>

#include <base/values.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class InputDeviceFunction : public ProbeFunction {
 public:
  static constexpr auto function_name = "input_device";

  std::string GetFunctionName() const override { return function_name; }

  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value);

  DataType Eval() const override;

  int EvalInHelper(std::string* output) const override;

 private:
  static ProbeFunction::Register<InputDeviceFunction> register_;
};

/* Register the InputDeviceFunction */
REGISTER_PROBE_FUNCTION(InputDeviceFunction);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_INPUT_DEVICE_H_
