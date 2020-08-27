// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEMS_SETUP_CONFIGURATION_H_
#define MEMS_SETUP_CONFIGURATION_H_

#include <memory>
#include <string>

#include <base/macros.h>

#include <libmems/iio_device.h>
#include "mems_setup/delegate.h"
#include "mems_setup/sensor_kind.h"

namespace mems_setup {

class Configuration {
 public:
  Configuration(libmems::IioContext* context,
                libmems::IioDevice* sensor,
                SensorKind kind,
                Delegate* delegate);

  bool Configure();

 private:
  bool ConfigGyro();
  bool ConfigAccelerometer();
  bool ConfigIlluminance();

  bool CopyImuCalibationFromVpd(int max_value);
  bool CopyImuCalibationFromVpd(int max_value, const std::string& location);

  bool CopyLightCalibrationFromVpd();

  bool AddSysfsTrigger(int sysfs_trigger_id);

  bool EnableAccelScanElements();

  bool EnableBuffer();

  bool EnableKeyboardAngle();

  bool EnableCalibration(bool enable);

  Delegate* delegate_;  // non-owned
  SensorKind kind_;
  libmems::IioDevice* sensor_;    // non-owned
  libmems::IioContext* context_;  // non-owned

  DISALLOW_COPY_AND_ASSIGN(Configuration);
};

}  // namespace mems_setup

#endif  // MEMS_SETUP_CONFIGURATION_H_
