// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_INPUT_DEVICE_H_
#define RUNTIME_PROBE_FUNCTIONS_INPUT_DEVICE_H_

#include <memory>
#include <string>

#include <base/values.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class InputDeviceFunction : public ProbeFunction {
 public:
  NAME_PROBE_FUNCTION("input_device");

  static constexpr auto FromKwargsValue =
      FromEmptyKwargsValue<InputDeviceFunction>;

  DataType Eval() const override;

  int EvalInHelper(std::string* output) const override;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_INPUT_DEVICE_H_
