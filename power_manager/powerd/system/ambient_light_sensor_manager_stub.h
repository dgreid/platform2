// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_STUB_H_

#include <memory>

#include "power_manager/powerd/system/ambient_light_sensor_interface.h"
#include "power_manager/powerd/system/ambient_light_sensor_manager_interface.h"
#include "power_manager/powerd/system/ambient_light_sensor_stub.h"

namespace power_manager {
namespace system {

// Stub implementation of AmbientLightSensorManagerInterface for use by tests.
class AmbientLightSensorManagerStub
    : public AmbientLightSensorManagerInterface {
 public:
  AmbientLightSensorManagerStub();
  explicit AmbientLightSensorManagerStub(int lux);
  ~AmbientLightSensorManagerStub() override;

  // AmbientLightSensorManagerInterface implementation:
  bool HasColorSensor() override;
  AmbientLightSensorInterface* GetSensorForInternalBacklight() override;
  AmbientLightSensorInterface* GetSensorForKeyboardBacklight() override;

 private:
  std::unique_ptr<system::AmbientLightSensorStub> internal_backlight_sensor_;
  std::unique_ptr<system::AmbientLightSensorStub> keyboard_backlight_sensor_;

  DISALLOW_COPY_AND_ASSIGN(AmbientLightSensorManagerStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_MANAGER_STUB_H_
