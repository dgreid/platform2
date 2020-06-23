/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#ifndef RUNTIME_PROBE_FUNCTIONS_USB_CAMERA_H_
#define RUNTIME_PROBE_FUNCTIONS_USB_CAMERA_H_

#include <memory>
#include <string>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class UsbCameraFunction : public ProbeFunction {
 public:
  static constexpr auto function_name = "usb_camera";
  std::string GetFunctionName() const override { return function_name; }
  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value);
  DataType Eval() const override;
  int EvalInHelper(std::string*) const override;

 private:
  static ProbeFunction::Register<UsbCameraFunction> register_;
};

// Register the UsbCameraFunction.
REGISTER_PROBE_FUNCTION(UsbCameraFunction);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_USB_CAMERA_H_
