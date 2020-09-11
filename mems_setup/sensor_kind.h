// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEMS_SETUP_SENSOR_KIND_H_
#define MEMS_SETUP_SENSOR_KIND_H_

#include <string>

#include <base/optional.h>

namespace mems_setup {

enum class SensorKind {
  ACCELEROMETER,
  GYROSCOPE,
  LIGHT,
  SYNC,
  MAGNETOMETER,
  BAROMETER
};

std::string SensorKindToString(SensorKind kind);
base::Optional<SensorKind> SensorKindFromString(const std::string& name);

}  // namespace mems_setup

#endif  // MEMS_SETUP_SENSOR_KIND_H_
