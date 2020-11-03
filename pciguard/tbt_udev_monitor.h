// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PCIGUARD_TBT_UDEV_MONITOR_H_
#define PCIGUARD_TBT_UDEV_MONITOR_H_

#include <libudev.h>

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/file_path.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>
#include <brillo/udev/udev_monitor.h>

#include "pciguard/event_handler.h"

namespace pciguard {

// Class to monitor thunderbolt udev events
class TbtUdevMonitor {
 public:
  explicit TbtUdevMonitor(std::shared_ptr<EventHandler> ev_handler);
  TbtUdevMonitor(const TbtUdevMonitor&) = delete;
  TbtUdevMonitor& operator=(const TbtUdevMonitor&) = delete;
  ~TbtUdevMonitor() = default;

 private:
  // Handle Udev events emanating from |udev_monitor_watcher_|.
  void OnThunderboltUdevEvent();

  std::unique_ptr<brillo::Udev> udev_;
  std::unique_ptr<brillo::UdevMonitor> udev_monitor_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      udev_monitor_watcher_;
  std::shared_ptr<EventHandler> event_handler_;
};

}  // namespace pciguard

#endif  // PCIGUARD_TBT_UDEV_MONITOR_H_
