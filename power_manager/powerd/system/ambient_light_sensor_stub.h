// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_STUB_H_

#include <base/observer_list.h>
#include <base/optional.h>

#include "power_manager/powerd/system/ambient_light_sensor_interface.h"

namespace power_manager {
namespace system {

// Stub implementation of AmbientLightSensorInterface for use by tests.
class AmbientLightSensorStub : public AmbientLightSensorInterface {
 public:
  explicit AmbientLightSensorStub(int lux);
  AmbientLightSensorStub(const AmbientLightSensorStub&) = delete;
  AmbientLightSensorStub& operator=(const AmbientLightSensorStub&) = delete;

  ~AmbientLightSensorStub() override;

  void set_lux(int lux) { lux_ = lux; }
  void set_color_temperature(int color_temperature) {
    color_temperature_ = color_temperature;
  }

  // Notifies |observers_| that the ambient light has changed.
  void NotifyObservers();

  // AmbientLightSensorInterface implementation:
  void AddObserver(AmbientLightObserver* observer) override;
  void RemoveObserver(AmbientLightObserver* observer) override;
  bool IsColorSensor() const override;
  int GetAmbientLightLux() override;
  int GetColorTemperature() override;

 private:
  base::ObserverList<AmbientLightObserver> observers_;

  // Value returned by GetAmbientLightLux().
  int lux_;

  // If this is nullopt, IsColorSensor returns false and GetColorTemperature
  // returns -1. Otherwise, IsColorSensor returns true and GetColorTemperature
  // returns this value.
  base::Optional<int> color_temperature_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_STUB_H_
