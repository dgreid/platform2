// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_UDEV_MONITOR_H_
#define TYPECD_UDEV_MONITOR_H_

#include <libudev.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <brillo/udev/mock_udev.h>
#include <gtest/gtest_prod.h>

namespace typecd {

// Class to monitor udev events on the Type C subsystem and inform other
// objects / classes of these events.
class UdevMonitor {
 public:
  UdevMonitor() = default;

  // Create a Udev device for enumeration and monitoring.
  bool InitUdev();

  // Enumerate all existing events/devices, and send the appropriate
  // notifications to other classes.
  bool ScanDevices();

 private:
  friend class UdevMonitorTest;
  FRIEND_TEST(UdevMonitorTest, TestBasic);

  // Set the |udev_| pointer to a MockUdev device. *Only* used by unit tests.
  void SetUdev(std::unique_ptr<brillo::MockUdev> udev) {
    udev_ = std::move(udev);
  }

  // Handle a udev event which causes a Type C device to be added.
  bool HandleDeviceAdded(const std::string& path);

  std::unique_ptr<brillo::Udev> udev_;
};

}  // namespace typecd

#endif  // TYPECD_UDEV_MONITOR_H_
