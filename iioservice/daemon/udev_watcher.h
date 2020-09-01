// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_UDEV_WATCHER_H_
#define IIOSERVICE_DAEMON_UDEV_WATCHER_H_

#include <memory>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/weak_ptr.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_monitor.h>

namespace iioservice {

// UdevWatcher should be created and destroyed on the same sequence.
class UdevWatcher {
 public:
  class Observer {
   public:
    virtual void OnDeviceAdded(int iio_device_id) = 0;
  };

  // The observer must outlive this watcher.
  static std::unique_ptr<UdevWatcher> Create(
      Observer* observer, std::unique_ptr<brillo::Udev> udev);

  ~UdevWatcher() = default;

  // Disallow copy constructor and assign operator.
  UdevWatcher(const UdevWatcher&) = delete;
  UdevWatcher& operator=(const UdevWatcher&) = delete;

 private:
  UdevWatcher(Observer* observer, std::unique_ptr<brillo::Udev> udev);

  bool Start();
  void OnReadable();

  Observer* observer_;

  std::unique_ptr<brillo::Udev> udev_;
  std::unique_ptr<brillo::UdevMonitor> udev_monitor_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

  base::WeakPtrFactory<UdevWatcher> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_UDEV_WATCHER_H_
