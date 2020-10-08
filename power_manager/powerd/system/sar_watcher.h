// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_SAR_WATCHER_H_
#define POWER_MANAGER_POWERD_SYSTEM_SAR_WATCHER_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <base/callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <base/observer_list.h>

#include <cros_config/cros_config.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"
#include "power_manager/powerd/system/user_proximity_watcher_interface.h"

namespace power_manager {

class PrefsInterface;

namespace system {

class UserProximityObserver;
struct UdevEvent;
class UdevInterface;

// Concrete implementation of UserProximityWatcherInterface: detects proximity
// sensors and reports proximity events.
class SarWatcher : public UserProximityWatcherInterface,
                   public UdevSubsystemObserver {
 public:
  // udev subsystem to watch.
  static const char kIioUdevSubsystem[];

  // udev device type.
  static const char kIioUdevDevice[];

  // Mechanism to obtain a file handle suitable for observing IIO events
  using OpenIioEventsFunc = base::Callback<int(const base::FilePath&)>;

  void set_open_iio_events_func_for_testing(OpenIioEventsFunc f);

  SarWatcher();
  ~SarWatcher() override;

  // Returns true on success.
  bool Init(PrefsInterface* prefs, UdevInterface* udev);

  // UserProximityWatcherInterface implementation:
  void AddObserver(UserProximityObserver* observer) override;
  void RemoveObserver(UserProximityObserver* observer) override;

  // UdevSubsystemObserver implementation:
  void OnUdevEvent(const UdevEvent& event) override;

  // Watcher implementation:
  void OnFileCanReadWithoutBlocking(int fd);

 private:
  struct SensorInfo {
    std::string syspath;
    std::string devlink;
    int event_fd;
    // Bitwise combination of UserProximityObserver::SensorRole values
    uint32_t role;
    std::unique_ptr<base::FileDescriptorWatcher::Controller> controller;
  };

  // Returns which subsystems the sensor at |path| should provide proximity
  // data for. The allowed roles are filtered based on whether the preferences
  // allow using proximity sensor as an input for a given subsystem. The
  // return value is a bitwise combination of SensorRole values.
  uint32_t GetUsableSensorRoles(const std::string& path);

  // Determines whether |dev| represents a proximity sensor connected via
  // the IIO subsystem. If so, |devlink_out| is the path to the file to be used
  // to read proximity events from this device.
  bool IsIioProximitySensor(const UdevDeviceInfo& dev,
                            std::string* devlink_out);

  // Sets proximity IIO attributes for rising, falling, or either direction
  bool SetIioRisingFallingValue(const std::string& syspath,
                                brillo::CrosConfigInterface* config,
                                const std::string& config_path,
                                const std::string& config_name,
                                const std::string& path_prefix,
                                const std::string& postfix);

  // Configures the proximity sensor for usage based on values read from
  // cros_config
  bool ConfigureSensor(const std::string& syspath, uint32_t role);

  // Opens a file descriptor suitable for listening to proximity events for
  // the sensor at |devlink|, and notifies registered observers that a new
  // valid proximity sensor exists.
  bool OnSensorDetected(const std::string& syspath, const std::string& devlink);

  OpenIioEventsFunc open_iio_events_func_;

  UdevInterface* udev_ = nullptr;  // non-owned
  base::ObserverList<UserProximityObserver> observers_;

  // Mapping between IIO event file descriptors and sensor details.
  std::unordered_map<int, SensorInfo> sensors_;

  bool use_proximity_for_cellular_ = false;
  bool use_proximity_for_wifi_ = false;

  DISALLOW_COPY_AND_ASSIGN(SarWatcher);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_SAR_WATCHER_H_
