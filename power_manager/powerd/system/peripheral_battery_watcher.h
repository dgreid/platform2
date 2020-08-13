// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_PERIPHERAL_BATTERY_WATCHER_H_
#define POWER_MANAGER_POWERD_SYSTEM_PERIPHERAL_BATTERY_WATCHER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/observer_list.h>
#include <base/timer/timer.h>

#include "power_manager/powerd/system/async_file_reader.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

namespace power_manager {
namespace system {

class DBusWrapperInterface;

class PeripheralBatteryWatcher : public UdevSubsystemObserver {
 public:
  // sysfs file containing a battery's scope.
  static const char kScopeFile[];
  // kScopeFile value used for peripheral batteries.
  static const char kScopeValueDevice[];

  // sysfs file containing a battery's status.
  static const char kStatusFile[];
  // kStatusFile value used to report an unknown status.
  static const char kStatusValueUnknown[];

  // sysfs file containing a battery's model name.
  static const char kModelNameFile[];
  // sysfs file containing a battery's capacity.
  static const char kCapacityFile[];
  // udev subsystem to listen to for peripheral battery events.
  static const char kUdevSubsystem[];

  PeripheralBatteryWatcher();
  ~PeripheralBatteryWatcher();

  void set_battery_path_for_testing(const base::FilePath& path) {
    peripheral_battery_path_ = path;
  }

  // Starts polling.
  void Init(DBusWrapperInterface* dbus_wrapper, UdevInterface* udev);

  // UdevSubsystemObserver implementation:
  void OnUdevEvent(const UdevEvent& event) override;

 private:
  // Reads battery status of a single peripheral device and send out a signal.
  void ReadBatteryStatus(const base::FilePath& path);

  // Handler for a periodic event that reads the peripheral batteries'
  // level.
  void ReadBatteryStatuses();

  // Detects if |device_path| in /sys/class/power_supply is a peripheral device.
  bool IsPeripheralDevice(const base::FilePath& device_path) const;

  // Fills |battery_list| with paths containing information about
  // peripheral batteries.
  void GetBatteryList(std::vector<base::FilePath>* battery_list);

  // Sends the battery status through D-Bus.
  void SendBatteryStatus(const base::FilePath& path,
                         const std::string& model_name,
                         int level);

  // Asynchronous I/O success and error handlers, respectively.
  void ReadCallback(const base::FilePath& path,
                    const std::string& model_name,
                    const std::string& data);
  void ErrorCallback(const base::FilePath& path, const std::string& model_name);

  DBusWrapperInterface* dbus_wrapper_;  // weak

  UdevInterface* udev_ = nullptr;  // non-owned

  // Path containing battery info for peripheral devices.
  base::FilePath peripheral_battery_path_;

  // Calls ReadBatteryStatuses().
  base::OneShotTimer poll_timer_;

  // Time between polls of the peripheral battery reading, in milliseconds.
  int poll_interval_ms_;

  // AsyncFileReaders for different peripheral batteries.
  std::vector<std::unique_ptr<AsyncFileReader>> battery_readers_;

  DISALLOW_COPY_AND_ASSIGN(PeripheralBatteryWatcher);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_PERIPHERAL_BATTERY_WATCHER_H_
