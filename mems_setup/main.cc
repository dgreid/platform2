// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <libmems/iio_context_impl.h>
#include <libmems/iio_device.h>
#include "mems_setup/configuration.h"
#include "mems_setup/delegate_impl.h"
#include "mems_setup/sensor_kind.h"

int main(int argc, char** argv) {
  DEFINE_string(sensor_kind, "",
                "Kind of sensor being initialized. "
                "One of anglvel, accel.");
  DEFINE_int32(device_id, -1,
               "The IIO device id for the sensor being "
               "initialized, such as iio:device0.");
  DEFINE_string(device_name, "",
                "The IIO device path for the sensor being "
                "initialized, such as cros-ec-accel.");

  brillo::OpenLog("mems_setup", true /*log_pid*/);

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader |
                  brillo::kLogToStderrIfTty);

  brillo::FlagHelper::Init(argc, argv, "Chromium OS MEMS Setup");

  if (FLAGS_sensor_kind.empty() ||
      (FLAGS_device_id == -1 && FLAGS_device_name.empty())) {
    LOG(ERROR) << "mems_setup must be called with sensor and type";
    exit(1);
  }

  if (FLAGS_device_name.empty()) {
    LOG(INFO) << "Starting mems_setup [id=" << FLAGS_device_id
              << ", kind=" << FLAGS_sensor_kind << "]";
  } else {
    LOG(INFO) << "Starting mems_setup [name=" << FLAGS_device_name
              << ", kind=" << FLAGS_sensor_kind << "]";
  }

  mems_setup::SensorKind kind;
  if (auto sk = mems_setup::SensorKindFromString(FLAGS_sensor_kind)) {
    kind = sk.value();
  } else {
    LOG(ERROR) << FLAGS_sensor_kind << " is not a known type of sensor";
    exit(1);
  }

  std::unique_ptr<mems_setup::Delegate> delegate(
      new mems_setup::DelegateImpl());

  base::FilePath iio_trig_sysfs_path("/sys/bus/iio/devices/iio_sysfs_trigger");
  if (!delegate->Exists(iio_trig_sysfs_path)) {
    if (!delegate->ProbeKernelModule("iio_trig_sysfs")) {
      LOG(ERROR) << "cannot load iio_trig_sysfs module";
      exit(1);
    }
    if (!delegate->Exists(iio_trig_sysfs_path)) {
      LOG(ERROR) << "cannot find iio_sysfs_trigger device";
      exit(1);
    }
  }

  std::unique_ptr<libmems::IioContext> context(new libmems::IioContextImpl());

  libmems::IioDevice* device = nullptr;
  if (FLAGS_device_id != -1) {
    device = context->GetDeviceById(FLAGS_device_id);

    if (device == nullptr) {
      LOG(ERROR) << "device with id: " << FLAGS_device_id << " not found";
      exit(1);
    }
  } else {
    auto devices = context->GetDevicesByName(FLAGS_device_name);
    if (devices.size() != 1) {
      LOG(ERROR) << devices.size() << " possible devices with name "
                 << FLAGS_device_name << " found";
      exit(1);
    }
    device = devices[0];
  }

  std::unique_ptr<mems_setup::Configuration> config(
      new mems_setup::Configuration(context.get(), device, kind,
                                    delegate.get()));

  return config->Configure() ? 0 : 1;
}
