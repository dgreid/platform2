// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/storage/default_device_adapter.h"

#include <string>

#include "diagnostics/common/file_utils.h"

namespace diagnostics {

namespace {

constexpr char kModelFile[] = "device/model";
constexpr char kAltModelFile[] = "device/name";

}  // namespace

DefaultDeviceAdapter::DefaultDeviceAdapter(const base::FilePath& dev_sys_path)
    : dev_sys_path_(dev_sys_path) {}

std::string DefaultDeviceAdapter::GetDeviceName() const {
  return dev_sys_path_.BaseName().value();
}

std::string DefaultDeviceAdapter::GetModel() const {
  // This piece is for compatibility and will be replaced with a simple
  // return ""; when all the devices are covered properly.
  std::string model;
  if (!ReadAndTrimString(dev_sys_path_, kModelFile, &model)) {
    if (!ReadAndTrimString(dev_sys_path_, kAltModelFile, &model)) {
      return "";
    }
  }
  return model;
}

}  // namespace diagnostics
