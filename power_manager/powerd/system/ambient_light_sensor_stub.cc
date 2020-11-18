// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor_stub.h"

#include <string>

namespace power_manager {
namespace system {

AmbientLightSensorStub::AmbientLightSensorStub(int lux) : lux_(lux) {}

AmbientLightSensorStub::~AmbientLightSensorStub() {}

void AmbientLightSensorStub::NotifyObservers() {
  for (AmbientLightObserver& observer : observers_)
    observer.OnAmbientLightUpdated(this);
}

void AmbientLightSensorStub::AddObserver(AmbientLightObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void AmbientLightSensorStub::RemoveObserver(AmbientLightObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

bool AmbientLightSensorStub::IsColorSensor() const {
  return color_temperature_.has_value();
}

int AmbientLightSensorStub::GetAmbientLightLux() {
  return lux_;
}

int AmbientLightSensorStub::GetColorTemperature() {
  return color_temperature_.value_or(-1);
}

base::FilePath AmbientLightSensorStub::GetIlluminancePath() const {
  return path_;
}

}  // namespace system
}  // namespace power_manager
