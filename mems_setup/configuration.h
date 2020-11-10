// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEMS_SETUP_CONFIGURATION_H_
#define MEMS_SETUP_CONFIGURATION_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>

#include <libmems/iio_device.h>
#include "mems_setup/delegate.h"
#include "mems_setup/sensor_kind.h"

namespace mems_setup {

class Configuration {
 public:
  static const char* GetGroupNameForSysfs();

  Configuration(libmems::IioContext* context,
                libmems::IioDevice* sensor,
                SensorKind kind,
                Delegate* delegate);
  Configuration(const Configuration&) = delete;
  Configuration& operator=(const Configuration&) = delete;

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

  bool SetupPermissions();
  std::vector<base::FilePath> EnumerateAllFiles(base::FilePath file_path);
  bool SetReadPermissionAndOwnership(base::FilePath file_path);
  bool SetWritePermissionAndOwnership(base::FilePath file_path);

  Delegate* delegate_;  // non-owned
  SensorKind kind_;
  libmems::IioDevice* sensor_;    // non-owned
  libmems::IioContext* context_;  // non-owned

  base::Optional<gid_t> iioservice_gid_ = base::nullopt;
};

}  // namespace mems_setup

#endif  // MEMS_SETUP_CONFIGURATION_H_
