// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_THERMAL_COOLING_DEVICE_H_
#define POWER_MANAGER_POWERD_SYSTEM_THERMAL_COOLING_DEVICE_H_

#include <base/macros.h>

#include "power_manager/powerd/system/thermal/device_thermal_state.h"
#include "power_manager/powerd/system/thermal/thermal_device.h"

namespace power_manager {
namespace system {

class CoolingDevice : public ThermalDevice {
 public:
  using ThermalDevice::ThermalDevice;
  // Read sysfs to determine the scaling for nominal/fair/serious/critcal state.
  bool InitSysfsFile() override;

 protected:
  // ThermalDevice override.
  DeviceThermalState CalculateThermalState(int sysfs_data) override;

 private:
  // Value of max_state in cooling device sysfs.
  int max_state_;

  // Threshold of cur_state in cooling device sysfs for each DeviceThermalState.
  int threshold_fair_;
  int threshold_serious_;
  int threshold_critical_;

  DISALLOW_COPY_AND_ASSIGN(CoolingDevice);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_THERMAL_COOLING_DEVICE_H_
