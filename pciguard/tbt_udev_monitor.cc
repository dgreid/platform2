// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pciguard/tbt_udev_monitor.h"

namespace pciguard {

namespace {

const char kUdev[] = "udev";
const char kThunderboltSubsystem[] = "thunderbolt";
const char kThunderboltDevice[] = "thunderbolt_device";

}  // namespace

TbtUdevMonitor::TbtUdevMonitor(std::shared_ptr<EventHandler> ev_handler)
    : event_handler_(ev_handler) {
  udev_ = brillo::Udev::Create();
  if (!udev_) {
    PLOG(ERROR) << "Failed to initialize udev object.";
    exit(EXIT_FAILURE);
  }

  udev_monitor_ = udev_->CreateMonitorFromNetlink(kUdev);
  if (!udev_monitor_) {
    PLOG(ERROR) << "Failed to create udev monitor.";
    exit(EXIT_FAILURE);
  }

  if (!udev_monitor_->FilterAddMatchSubsystemDeviceType(kThunderboltSubsystem,
                                                        kThunderboltDevice)) {
    PLOG(ERROR) << "Failed to add thunderbolt subsystem to udev monitor.";
    exit(EXIT_FAILURE);
  }

  if (!udev_monitor_->EnableReceiving()) {
    PLOG(ERROR) << "Failed to enable receiving for udev monitor.";
    exit(EXIT_FAILURE);
  }

  int fd = udev_monitor_->GetFileDescriptor();
  if (fd == brillo::UdevMonitor::kInvalidFileDescriptor) {
    PLOG(ERROR) << "Failed to get udev monitor fd.";
    exit(EXIT_FAILURE);
  }

  udev_monitor_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd, base::BindRepeating(&TbtUdevMonitor::OnThunderboltUdevEvent,
                              base::Unretained(this)));
  if (!udev_monitor_watcher_) {
    PLOG(ERROR) << "Failed to start watcher for udev monitor fd.";
    exit(EXIT_FAILURE);
  }
}

void TbtUdevMonitor::OnThunderboltUdevEvent() {
  auto device = udev_monitor_->ReceiveDevice();
  if (!device) {
    LOG(ERROR) << "Udev receive device failed.";
    return;
  }

  auto path = base::FilePath(device->GetSysPath());
  if (path.empty()) {
    LOG(ERROR) << "Failed to get device syspath.";
    return;
  }

  auto action = std::string(device->GetAction());
  if (action.empty()) {
    LOG(ERROR) << "Failed to get device action.";
    return;
  }

  if (action == "add") {
    event_handler_->OnNewThunderboltDev(path);
  }
}

}  // namespace pciguard
