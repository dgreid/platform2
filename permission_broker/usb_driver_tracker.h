// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_USB_DRIVER_TRACKER_H_
#define PERMISSION_BROKER_USB_DRIVER_TRACKER_H_

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>

namespace permission_broker {

constexpr const int kInvalidLifelineFD = -1;

class UsbDriverTracker {
 public:
  UsbDriverTracker();
  UsbDriverTracker(const UsbDriverTracker&) = delete;
  UsbDriverTracker& operator=(const UsbDriverTracker&) = delete;

  ~UsbDriverTracker();

  // Detach all the interfaces of the USB device at |path| from their
  // kernel drivers using the |fd| file descriptor pointing to the devfs node.
  // Monitor |lifeline_fd| to reattach kernel drivers on close.
  bool DetachPathFromKernel(int fd, int lifeline_fd, const std::string& path);

  // Try to attach kernel drivers to the interface numbers in  |ifaces|
  // of the USB device at |path|.
  bool ReAttachPathToKernel(const std::string& path,
                            const std::vector<uint8_t>& ifaces);

 private:
  struct UsbInterfaces;

  void HandleClosedFd(int fd);
  void WatchLifelineFd(int lifeline_fd,
                       std::string path,
                       std::vector<uint8_t> ifaces);

  // File descriptors watcher callback.
  static void OnFdEvent(UsbDriverTracker* obj, int fd);

  std::map<int, UsbInterfaces> dev_fds_;

  base::WeakPtrFactory<UsbDriverTracker> weak_ptr_factory_{this};
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_USB_DRIVER_TRACKER_H_
