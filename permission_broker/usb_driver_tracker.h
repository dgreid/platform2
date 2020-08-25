// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_USB_DRIVER_TRACKER_H_
#define PERMISSION_BROKER_USB_DRIVER_TRACKER_H_

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/sequenced_task_runner.h>

namespace permission_broker {

class UsbDriverTracker {
 public:
  UsbDriverTracker();
  ~UsbDriverTracker();

  // Detach all the interfaces of the USB device at |path| from their
  // kernel drivers using the |fd| file descriptor pointing to the devfs node.
  bool DetachPathFromKernel(int fd, const std::string& path);

  // Try to attach a kernel driver to the interface number |iface|
  // of the USB device at |path|.
  bool ReAttachPathToKernel(const std::string& path,
                            const std::vector<uint8_t>& ifaces);

 private:
  struct UsbInterfaces;

  void ScanClosedFd(int fd);

  // File descriptors watcher callback.
  static void OnFdEvent(UsbDriverTracker* obj, int fd);

  std::map<int, UsbInterfaces> dev_fds_;

  DISALLOW_COPY_AND_ASSIGN(UsbDriverTracker);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_USB_DRIVER_TRACKER_H_
