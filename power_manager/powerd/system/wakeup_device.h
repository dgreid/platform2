// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_WAKEUP_DEVICE_H_
#define POWER_MANAGER_POWERD_SYSTEM_WAKEUP_DEVICE_H_

#include <base/files/file_path.h>

#include <memory>

#include "power_manager/powerd/system/wakeup_device_interface.h"

namespace power_manager {
namespace system {

class WakeupDevice : public WakeupDeviceInterface {
 public:
  static std::unique_ptr<WakeupDeviceInterface> CreateWakeupDevice(
      const base::FilePath& path);

  // Relative path to device specific wakeup directory from the device sys
  // path.
  static const char kWakeupDir[];
  // Relative path to device specific event_count from the wakeup/wakeupN
  // directory under device sysfs path (wakeup/wakeupN/event_count).
  static const char kPowerEventCountPath[];

  ~WakeupDevice() override;

  // Implementation of WakeupDeviceInterface.
  void PrepareForSuspend() override;
  void HandleResume() override;
  bool CausedLastWake() const override;

 private:
  explicit WakeupDevice(const base::FilePath& path);
  WakeupDevice(const WakeupDevice&) = delete;
  WakeupDevice& operator=(const WakeupDevice&) = delete;

  // Reads the |kPowerEventCountPath|. |event_count_out| is set to the read
  // value if the read is successful. Returns true on success, false otherwise.
  bool ReadEventCount(uint64_t* event_count_out);

  // Sysfs path of the device. Can be overridden by tests.
  base::FilePath sys_path_;

  // Did this device cause last wake?
  bool caused_last_wake_ = false;

  // Event count of the device before the last suspend.
  uint64_t event_count_before_suspend_ = 0;

  // Was event_count read before suspend successful?
  bool was_pre_suspend_read_successful_ = false;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_WAKEUP_DEVICE_H_
