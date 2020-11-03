// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_INTERFACE_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_INTERFACE_H_

#include <base/files/file_path.h>

#include "power_manager/powerd/system/ambient_light_observer.h"

namespace power_manager {
namespace system {

class AmbientLightSensorInterface {
 public:
  AmbientLightSensorInterface() {}
  AmbientLightSensorInterface(const AmbientLightSensorInterface&) = delete;
  AmbientLightSensorInterface& operator=(const AmbientLightSensorInterface&) =
      delete;
  virtual ~AmbientLightSensorInterface() {}

  // Adds or removes observers for sensor readings.
  virtual void AddObserver(AmbientLightObserver* observer) = 0;
  virtual void RemoveObserver(AmbientLightObserver* observer) = 0;

  // Whether or not this ALS supports color readings.
  virtual bool IsColorSensor() const = 0;

  // Used by observers in their callback to get the raw reading from the sensor
  // for the ambient light level. -1 is considered an error value.
  virtual int GetAmbientLightLux() = 0;

  // Latest color temperature measured if supported. -1 is considered an error
  // value.
  virtual int GetColorTemperature() = 0;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_INTERFACE_H_
