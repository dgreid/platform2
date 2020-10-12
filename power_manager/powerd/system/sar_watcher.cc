// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/sar_watcher.h"

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

const char SarWatcher::kIioUdevSubsystem[] = "iio";

const char SarWatcher::kIioUdevDevice[] = "iio_device";

void SarWatcher::set_open_iio_events_func_for_testing(OpenIioEventsFunc f) {
  open_iio_events_func_ = f;
}

SarWatcher::SarWatcher() : open_iio_events_func_(base::Bind(&OpenIioFd)) {}

SarWatcher::~SarWatcher() {
  if (udev_)
    udev_->RemoveSubsystemObserver(kIioUdevSubsystem, this);
}

bool SarWatcher::Init(PrefsInterface* prefs, UdevInterface* udev) {
  prefs->GetBool(kSetCellularTransmitPowerForProximityPref,
                 &use_proximity_for_cellular_);

  prefs->GetBool(kSetWifiTransmitPowerForProximityPref,
                 &use_proximity_for_wifi_);

  udev_ = udev;
  udev_->AddSubsystemObserver(kIioUdevSubsystem, this);

  std::vector<UdevDeviceInfo> iio_devices;
  if (!udev_->GetSubsystemDevices(kIioUdevSubsystem, &iio_devices)) {
    LOG(ERROR) << "Enumeration of existing proximity devices failed.";
    return false;
  }

  for (auto const& iio_dev : iio_devices) {
    std::string devlink;
    if (!IsIioProximitySensor(iio_dev, &devlink))
      continue;
    if (!OnSensorDetected(iio_dev.syspath, devlink)) {
      LOG(ERROR) << "Unable to set up proximity sensor " << iio_dev.syspath;
    }
  }

  return true;
}

void SarWatcher::AddObserver(UserProximityObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void SarWatcher::RemoveObserver(UserProximityObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void SarWatcher::OnUdevEvent(const UdevEvent& event) {
  if (event.action != UdevEvent::Action::ADD)
    return;

  std::string devlink;
  if (!IsIioProximitySensor(event.device_info, &devlink))
    return;

  if (!OnSensorDetected(event.device_info.syspath, devlink))
    LOG(ERROR) << "Unable to setup proximity sensor "
               << event.device_info.syspath;
}

void SarWatcher::OnFileCanReadWithoutBlocking(int fd) {
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

  for (auto& observer : observers_) {
    observer.OnProximityEvent(fd, proximity);
  }
}

bool SarWatcher::IsIioProximitySensor(const UdevDeviceInfo& dev,
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

uint32_t SarWatcher::GetUsableSensorRoles(const std::string& path) {
  uint32_t responsibility = UserProximityObserver::SensorRole::SENSOR_ROLE_NONE;

  const auto proximity_index = path.find("proximity-");
  if (proximity_index == std::string::npos)
    return responsibility;

  if (use_proximity_for_cellular_ &&
      path.find("-lte", proximity_index) != std::string::npos)
    responsibility |= UserProximityObserver::SensorRole::SENSOR_ROLE_LTE;

  if (use_proximity_for_wifi_ &&
      path.find("-wifi", proximity_index) != std::string::npos)
    responsibility |= UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI;

  return responsibility;
}

bool SarWatcher::OnSensorDetected(const std::string& syspath,
                                  const std::string& devlink) {
  uint32_t role = GetUsableSensorRoles(devlink);

  if (role == UserProximityObserver::SensorRole::SENSOR_ROLE_NONE) {
    LOG(INFO) << "Sensor at " << devlink << " not usable for any subsystem";
    return true;
  }

  int event_fd = open_iio_events_func_.Run(base::FilePath(devlink));
  if (event_fd == -1) {
    LOG(WARNING) << "Unable to open event descriptor for file " << devlink;
    return false;
  }

  SensorInfo info;
  info.syspath = syspath;
  info.devlink = devlink;
  info.event_fd = event_fd;
  info.role = role;
  info.controller = base::FileDescriptorWatcher::WatchReadable(
      event_fd, base::BindRepeating(&SarWatcher::OnFileCanReadWithoutBlocking,
                                    base::Unretained(this), event_fd));
  sensors_.emplace(event_fd, std::move(info));

  for (auto& observer : observers_) {
    observer.OnNewSensor(event_fd, role);
  }

  return true;
}

}  // namespace system
}  // namespace power_manager
