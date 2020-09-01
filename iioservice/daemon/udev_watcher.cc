// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/udev_watcher.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>
#include <brillo/udev/udev_monitor.h>
#include <libmems/iio_device_impl.h>

#include "iioservice/include/common.h"

namespace iioservice {

namespace {

constexpr char kSubsystemString[] = "iio";
constexpr char kDeviceTypeString[] = "iio_device";

}  // namespace

// static
std::unique_ptr<UdevWatcher> UdevWatcher::Create(
    UdevWatcher::Observer* observer, std::unique_ptr<brillo::Udev> udev) {
  if (!observer)
    return nullptr;

  std::unique_ptr<UdevWatcher> watcher(
      new UdevWatcher(observer, std::move(udev)));
  if (!watcher->Start())
    return nullptr;

  return watcher;
}

UdevWatcher::UdevWatcher(UdevWatcher::Observer* observer,
                         std::unique_ptr<brillo::Udev> udev)
    : observer_(observer), udev_(std::move(udev)) {
  DCHECK(observer_);
}

bool UdevWatcher::Start() {
  if (!udev_.get()) {
    LOGF(ERROR) << "udev_new failed";
    return false;
  }

  udev_monitor_ = udev_->CreateMonitorFromNetlink("udev");
  if (!udev_monitor_.get()) {
    LOGF(ERROR) << "udev_monitor_filter_add_match_subsystem_devtype failed";
    return false;
  }

  if (!udev_monitor_->FilterAddMatchSubsystemDeviceType(kSubsystemString,
                                                        kDeviceTypeString)) {
    LOGF(ERROR) << "udev_monitor_filter_add_match_subsystem_devtype failed";
    return false;
  }

  if (!udev_monitor_->EnableReceiving()) {
    LOGF(ERROR) << "udev_monitor_enable_receiving failed";
    return false;
  }

  int fd = udev_monitor_->GetFileDescriptor();
  if (fd < 0) {
    LOGF(ERROR) << "udev_monitor_get_fd failed";
    return false;
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd, base::BindRepeating(&UdevWatcher::OnReadable,
                              weak_factory_.GetWeakPtr()));

  if (!watcher_) {
    LOGF(ERROR) << "Failed to start watching a file descriptor";
    return false;
  }

  return true;
}

void UdevWatcher::OnReadable() {
  std::unique_ptr<brillo::UdevDevice> udev_device =
      udev_monitor_->ReceiveDevice();
  if (!udev_device.get()) {
    LOGF(ERROR) << "udev_monitor_receive_device failed";
    return;
  }

  const char* action = udev_device->GetAction();
  if (!action) {
    LOGF(ERROR) << "udev_device_get_action failed";
    return;
  }

  if (strcmp(action, "add") == 0) {
    auto id_opt =
        libmems::IioDeviceImpl::GetIdFromString(udev_device->GetSysName());
    if (id_opt.has_value())
      observer_->OnDeviceAdded(id_opt.value());
  }
}

}  // namespace iioservice
