// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_INTERFACE_H_

namespace power_manager {
namespace system {

class AmbientLightSensorManagerInterface {
 public:
  AmbientLightSensorManagerInterface() {}
  AmbientLightSensorManagerInterface(
      const AmbientLightSensorManagerInterface&) = delete;
  AmbientLightSensorManagerInterface& operator=(
      const AmbientLightSensorManagerInterface&) = delete;
  virtual ~AmbientLightSensorManagerInterface() {}

  virtual AmbientLightSensorInterface* GetSensorForInternalBacklight() = 0;
  virtual AmbientLightSensorInterface* GetSensorForKeyboardBacklight() = 0;

  virtual bool HasColorSensor() = 0;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_INTERFACE_H_
