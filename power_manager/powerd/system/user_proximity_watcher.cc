// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/user_proximity_watcher.h"

#include <fcntl.h>
#include <linux/iio/events.h>
#include <linux/iio/types.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/logging.h>

#include <cros_config/cros_config.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/powerd/system/udev.h"
#include "power_manager/powerd/system/user_proximity_observer.h"

namespace power_manager {
namespace system {

namespace {

int OpenIioFd(const base::FilePath& path) {
  const std::string file(path.value());
  int fd = open(file.c_str(), O_RDONLY);
  if (fd == -1) {
    PLOG(WARNING) << "Unable to open file " << file;
    return -1;
  }

  int event_fd = -1;
  int ret = ioctl(fd, IIO_GET_EVENT_FD_IOCTL, &event_fd);
  close(fd);

  if (ret < 0 || event_fd == -1) {
    PLOG(WARNING) << "Unable to open event descriptor for file " << file;
    return -1;
  }

  return event_fd;
}

}  // namespace

const char UserProximityWatcher::kIioUdevSubsystem[] = "iio";

const char UserProximityWatcher::kIioUdevDevice[] = "iio_device";

void UserProximityWatcher::set_open_iio_events_func_for_testing(
    OpenIioEventsFunc f) {
  open_iio_events_func_ = f;
}

UserProximityWatcher::UserProximityWatcher()
    : open_iio_events_func_(base::Bind(&OpenIioFd)) {}

UserProximityWatcher::~UserProximityWatcher() {
  if (udev_)
    udev_->RemoveSubsystemObserver(kIioUdevSubsystem, this);
}

bool UserProximityWatcher::Init(PrefsInterface* prefs, UdevInterface* udev) {
  prefs->GetBool(kSetCellularTransmitPowerForProximityPref,
                 &use_proximity_for_cellular_);
  prefs->GetBool(kSetWifiTransmitPowerForProximityPref,
                 &use_proximity_for_wifi_);

  prefs->GetBool(kSetCellularTransmitPowerForActivityProximityPref,
                 &use_activity_proximity_for_cellular_);
  prefs->GetBool(kSetWifiTransmitPowerForActivityProximityPref,
                 &use_activity_proximity_for_wifi_);

  udev_ = udev;
  udev_->AddSubsystemObserver(kIioUdevSubsystem, this);

  std::vector<UdevDeviceInfo> iio_devices;
  if (!udev_->GetSubsystemDevices(kIioUdevSubsystem, &iio_devices)) {
    LOG(ERROR) << "Enumeration of existing proximity devices failed.";
    return false;
  }

  for (auto const& iio_dev : iio_devices)
    OnNewUdevDevice(iio_dev);
  return true;
}

void UserProximityWatcher::AddObserver(UserProximityObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
#if USE_TROGDOR_SAR_HACK
  // Add existing sensor to observer
  for (auto const& sensor : sensors_) {
    observer->OnNewSensor(sensor.first, sensor.second.role);
  }
#endif  // USE_TROGDOR_SAR_HACK
}

void UserProximityWatcher::RemoveObserver(UserProximityObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void UserProximityWatcher::OnUdevEvent(const UdevEvent& event) {
  if (event.action != UdevEvent::Action::ADD)
    return;
  OnNewUdevDevice(event.device_info);
}

void UserProximityWatcher::OnFileCanReadWithoutBlocking(int fd) {
  if (sensors_.find(fd) == sensors_.end()) {
    LOG(WARNING) << "Notified about FD " << fd << "which is not a sensor";
    return;
  }

  struct iio_event_data iio_event_buf = {0};
  if (read(fd, &iio_event_buf, sizeof(iio_event_buf)) == -1) {
    PLOG(ERROR) << "Failed to read from FD " << fd;
  }

  UserProximity proximity = UserProximity::UNKNOWN;
  auto dir = IIO_EVENT_CODE_EXTRACT_DIR(iio_event_buf.id);
  switch (dir) {
    case IIO_EV_DIR_RISING:
      proximity = UserProximity::FAR;
      break;
    case IIO_EV_DIR_FALLING:
      proximity = UserProximity::NEAR;
      break;
    default:
      LOG(ERROR) << "Unknown proximity value " << dir;
      return;
  }

  // This log is also used for tast-test: hardware.SensorActivity
  LOG(INFO) << "User proximity: "
            << (proximity == UserProximity::FAR ? "Far" : "Near");
  for (auto& observer : observers_)
    observer.OnProximityEvent(fd, proximity);
}

bool UserProximityWatcher::IsIioSarSensor(const UdevDeviceInfo& dev,
                                          std::string* devlink_out) {
  DCHECK(udev_);
  if (dev.subsystem != kIioUdevSubsystem || dev.devtype != kIioUdevDevice)
    return false;

  std::vector<std::string> devlinks;
  const bool found_devlinks = udev_->GetDevlinks(dev.syspath, &devlinks);
  if (!found_devlinks) {
    LOG(WARNING) << "udev unable to discover devlinks for " << dev.syspath;
    return false;
  }

  for (const auto& dl : devlinks) {
    if (dl.find("proximity-") != std::string::npos) {
      *devlink_out = dl;
      return true;
    }
  }

  return false;
}

bool UserProximityWatcher::IsIioActivitySensor(const UdevDeviceInfo& dev,
                                               std::string* devlink_out) {
  if (dev.subsystem != kIioUdevSubsystem || dev.devtype != kIioUdevDevice)
    return false;
  if (dev.syspath.find("-activity") == std::string::npos)
    return false;

  *devlink_out = "/dev/" + dev.sysname;
  return true;
}

uint32_t UserProximityWatcher::GetUsableSensorRoles(const SensorType type,
                                                    const std::string& path) {
  uint32_t responsibility = UserProximityObserver::SensorRole::SENSOR_ROLE_NONE;

  switch (type) {
    case SensorType::ACTIVITY: {
      if (use_activity_proximity_for_cellular_)
        responsibility |= UserProximityObserver::SensorRole::SENSOR_ROLE_LTE;
      if (use_activity_proximity_for_wifi_)
        responsibility |= UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI;
      break;
    }
    case SensorType::SAR: {
      const auto proximity_index = path.find("proximity-");
      if (proximity_index == std::string::npos)
        return responsibility;

      if (use_proximity_for_cellular_ &&
          path.find("-lte", proximity_index) != std::string::npos)
        responsibility |= UserProximityObserver::SensorRole::SENSOR_ROLE_LTE;

      if (use_proximity_for_wifi_ &&
          path.find("-wifi", proximity_index) != std::string::npos)
        responsibility |= UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI;
      break;
    }
    default: {
      LOG(WARNING) << "Unknown type of proximity sensor at " << path;
    }
  }

  return responsibility;
}

bool UserProximityWatcher::SetIioRisingFallingValue(
    const std::string& syspath,
    brillo::CrosConfigInterface* config,
    const std::string& config_path,
    const std::string& config_name,
    const std::string& path_prefix,
    const std::string& postfix) {
  std::string rising_value, falling_value;
  std::string rising_config = "thresh-rising" + config_name;
  std::string falling_config = "thresh-falling" + config_name;
  bool set_rising =
      config->GetString(config_path, rising_config, &rising_value);
  bool set_falling =
      config->GetString(config_path, falling_config, &falling_value);

  if (!set_rising && !set_falling)
    return true;

  std::string prefix = path_prefix + "thresh_";
  std::string falling_path = prefix + "falling" + postfix;
  std::string rising_path = prefix + "rising" + postfix;
  std::string either_path = prefix + "either" + postfix;
  bool try_either = falling_value == rising_value;

  if (!try_either || !udev_->SetSysattr(syspath, either_path, rising_value)) {
    if (set_rising && !udev_->SetSysattr(syspath, rising_path, rising_value)) {
      LOG(ERROR) << "Could not set proximity sensor " << rising_path << " to "
                 << rising_value;
      return false;
    }
    if (set_falling &&
        !udev_->SetSysattr(syspath, falling_path, falling_value)) {
      LOG(ERROR) << "Could not set proximity sensor " << falling_path << " to "
                 << falling_value;
      return false;
    }
  } else if (!try_either) {
    LOG(ERROR) << "Could not set proximity sensor " << either_path << " to "
               << rising_value;
    return false;
  }

  return true;
}

bool UserProximityWatcher::ConfigureSarSensor(const std::string& syspath,
                                              uint32_t role) {
  auto config = std::make_unique<brillo::CrosConfig>();
  if (!config->Init()) {
    /* Ignore on non-unibuild boards */
    LOG(INFO)
        << "cros config not found. Skipping proximity sensor configuration";
    return true;
  }

  std::string config_path = "/proximity-sensor/";
  if (role == UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI) {
    config_path += "wifi";
  } else if (role == UserProximityObserver::SensorRole::SENSOR_ROLE_LTE) {
    config_path += "lte";
  } else if (role == (UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI |
                      UserProximityObserver::SensorRole::SENSOR_ROLE_LTE)) {
    config_path += "wifi-lte";
  } else {
    LOG(ERROR) << "Unknown sensor role for configuration";
    return false;
  }

  std::string channel;

  if (!config->GetString(config_path, "channel", &channel)) {
    LOG(INFO)
        << "Could not get proximity sensor channel from cros_config. Ignoring";
    return true;
  }

  std::string sampling_frequency;
  if (config->GetString(config_path, "sampling-frequency",
                        &sampling_frequency)) {
    if (!udev_->SetSysattr(syspath, "sampling_frequency", sampling_frequency)) {
      LOG(ERROR) << "Could not set proximity sensor sampling frequency";
      return false;
    }
  }

  std::string gain;
  if (config->GetString(config_path, "hardwaregain", &gain)) {
    std::string gain_path = "in_proximity" + channel + "_hardwaregain";
    if (!udev_->SetSysattr(syspath, gain_path, gain)) {
      LOG(ERROR) << "Could not set proximity sensor hardware gain";
      return false;
    }
  }

  if (!SetIioRisingFallingValue(syspath, config.get(), config_path, "",
                                "events/in_proximity" + channel + "_",
                                "_value")) {
    return false;
  }

  if (!SetIioRisingFallingValue(
          syspath, config.get(), config_path, "-hysteresis",
          "events/in_proximity" + channel + "_", "_hysteresis")) {
    return false;
  }

  if (!SetIioRisingFallingValue(syspath, config.get(), config_path, "-period",
                                "events/", "_period")) {
    return false;
  }

  std::string enable_falling_path =
      "events/in_proximity" + channel + "_thresh_falling_en";
  std::string enable_rising_path =
      "events/in_proximity" + channel + "_thresh_rising_en";
  std::string enable_path =
      "events/in_proximity" + channel + "_thresh_either_en";

  if (!udev_->SetSysattr(syspath, enable_path, "1") &&
      (!udev_->SetSysattr(syspath, enable_rising_path, "1") ||
       !udev_->SetSysattr(syspath, enable_falling_path, "1"))) {
    LOG(ERROR) << "Could not enable proximity sensor";
    return false;
  }

  return true;
}

bool UserProximityWatcher::ConfigureActivitySensor(const std::string& syspath,
                                                   uint32_t role) {
  std::string enable_path = "events/in_proximity_change_either_en";
  if (!udev_->SetSysattr(syspath, enable_path, "1")) {
    LOG(ERROR) << "Could not enable proximity sensor";
    return false;
  }
  return true;
}

bool UserProximityWatcher::OnSensorDetected(const SensorType type,
                                            const std::string& syspath,
                                            const std::string& devlink) {
  DCHECK(type != SensorType::UNKNOWN);
  uint32_t role = GetUsableSensorRoles(type, devlink);

  if (role == UserProximityObserver::SensorRole::SENSOR_ROLE_NONE) {
    LOG(INFO) << "Sensor at " << devlink << " not usable for any subsystem";
    return true;
  }

  switch (type) {
    case SensorType::SAR:
      if (!ConfigureSarSensor(syspath, role)) {
        LOG(WARNING) << "Unable to configure sar sensor at " << devlink;
        return false;
      }
      break;
    case SensorType::ACTIVITY:
      if (!ConfigureActivitySensor(syspath, role)) {
        LOG(WARNING) << "Unable to configure activity sensor at " << devlink;
        return false;
      }
      break;
    default:
      LOG(WARNING) << "Unknown type of proximity sensor at " << devlink;
      return false;
  }

  int event_fd = open_iio_events_func_.Run(base::FilePath(devlink));
  if (event_fd == -1) {
    LOG(WARNING) << "Unable to open event descriptor for file " << devlink;
    return false;
  }

  SensorInfo info;
  info.type = type;
  info.syspath = syspath;
  info.devlink = devlink;
  info.event_fd = event_fd;
  info.role = role;
  info.controller = base::FileDescriptorWatcher::WatchReadable(
      event_fd,
      base::BindRepeating(&UserProximityWatcher::OnFileCanReadWithoutBlocking,
                          base::Unretained(this), event_fd));
  sensors_.emplace(event_fd, std::move(info));

  for (auto& observer : observers_) {
    observer.OnNewSensor(event_fd, role);
  }

  return true;
}

void UserProximityWatcher::OnNewUdevDevice(const UdevDeviceInfo& device_info) {
  std::string devlink;
  SensorType type = SensorType::UNKNOWN;
  if (IsIioSarSensor(device_info, &devlink))
    type = SensorType::SAR;
  else if (IsIioActivitySensor(device_info, &devlink))
    type = SensorType::ACTIVITY;
  else
    return;

  if (!OnSensorDetected(type, device_info.syspath, devlink))
    LOG(ERROR) << "Unable to setup proximity sensor " << device_info.syspath;
}

}  // namespace system
}  // namespace power_manager
