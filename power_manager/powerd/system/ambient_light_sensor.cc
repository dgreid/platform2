// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>

namespace power_manager {
namespace system {

void AmbientLightSensorDelegate::SetLuxCallback(
    base::RepeatingCallback<void(base::Optional<int>, base::Optional<int>)>
        set_lux_callback) {
  set_lux_callback_ = std::move(set_lux_callback);
}

void AmbientLightSensor::SetDelegate(
    std::unique_ptr<AmbientLightSensorDelegate> delegate) {
  delegate_ = std::move(delegate);
  delegate_->SetLuxCallback(base::BindRepeating(
      &AmbientLightSensor::SetLuxAndColorTemperature, base::Unretained(this)));
}

void AmbientLightSensor::SetLuxAndColorTemperature(
    base::Optional<int> lux, base::Optional<int> color_temperature) {
  if (lux.has_value())
    lux_value_ = lux.value();

  if (color_temperature.has_value()) {
    DCHECK(IsColorSensor());
    color_temperature_ = color_temperature.value();
  }

  lux_value_ = std::max(lux_value_, -1);
  color_temperature_ = std::max(color_temperature_, -1);

  for (AmbientLightObserver& observer : observers_)
    observer.OnAmbientLightUpdated(this);
}

base::FilePath AmbientLightSensor::GetIlluminancePath() const {
  if (!delegate_)
    return base::FilePath();

  return delegate_->GetIlluminancePath();
}

void AmbientLightSensor::AddObserver(AmbientLightObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void AmbientLightSensor::RemoveObserver(AmbientLightObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

bool AmbientLightSensor::IsColorSensor() const {
  if (!delegate_)
    return false;

  return delegate_->IsColorSensor();
}

int AmbientLightSensor::GetAmbientLightLux() {
  return lux_value_;
}

int AmbientLightSensor::GetColorTemperature() {
  return color_temperature_;
}

}  // namespace system
}  // namespace power_manager
