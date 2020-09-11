// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <base/macros.h>

#include "mems_setup/sensor_kind.h"

namespace mems_setup {

namespace {
constexpr char kAccelName[] = "accel";
constexpr char kGyroName[] = "anglvel";
constexpr char kLightName[] = "illuminance";
constexpr char kSyncName[] = "count";
constexpr char kMagnName[] = "magn";
constexpr char kBaroName[] = "baro";
}  // namespace

std::string SensorKindToString(SensorKind kind) {
  switch (kind) {
    case SensorKind::ACCELEROMETER:
      return kAccelName;
    case SensorKind::GYROSCOPE:
      return kGyroName;
    case SensorKind::LIGHT:
      return kLightName;
    case SensorKind::SYNC:
      return kSyncName;
    case SensorKind::MAGNETOMETER:
      return kAccelName;
    case SensorKind::BAROMETER:
      return kBaroName;
  }

  NOTREACHED();
}

base::Optional<SensorKind> SensorKindFromString(const std::string& name) {
  if (name == kAccelName)
    return SensorKind::ACCELEROMETER;
  if (name == kGyroName)
    return SensorKind::GYROSCOPE;
  if (name == kLightName)
    return SensorKind::LIGHT;
  if (name == kSyncName)
    return SensorKind::SYNC;
  if (name == kMagnName)
    return SensorKind::MAGNETOMETER;
  if (name == kBaroName)
    return SensorKind::BAROMETER;

  return base::nullopt;
}

}  // namespace mems_setup
