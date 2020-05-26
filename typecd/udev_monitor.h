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

#include <base/files/file_path.h>
#include <base/observer_list.h>
#include <base/observer_list_types.h>
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

  class Observer : public base::CheckedObserver {
   public:
    virtual ~Observer() {}
    // Callback that is executed when a port is connected or disconnected.
    //
    // The |path| argument refers to the sysfs device path of the port.
    // The |added| argument is set to true if the port was added, and false
    // otherwise.
    virtual void OnPortAddedOrRemoved(const base::FilePath& path,
                                      bool added) = 0;

    // Callback that is executed when a port partner is connected or
    // disconnected.
    //
    // The |path| argument refers to the sysfs device path of the port partner.
    // The |added| argument is set to true if the port was added, and false
    // otherwise.
    virtual void OnPartnerAddedOrRemoved(const base::FilePath& path,
                                         bool added) = 0;
  };

  void AddObserver(Observer* obs);
  void RemoveObserver(Observer* obs);

 private:
  friend class UdevMonitorTest;
  FRIEND_TEST(UdevMonitorTest, TestBasic);

  // Set the |udev_| pointer to a MockUdev device. *Only* used by unit tests.
  void SetUdev(std::unique_ptr<brillo::MockUdev> udev) {
    udev_ = std::move(udev);
  }

  // Handle a udev event which causes a Type C device to be added.
  bool HandleDeviceAdded(const base::FilePath& path);

  std::unique_ptr<brillo::Udev> udev_;
  base::ObserverList<Observer> observer_list_;
};

}  // namespace typecd

#endif  // TYPECD_UDEV_MONITOR_H_
