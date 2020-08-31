// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/daemon.h"

#include <chromeos/dbus/service_constants.h>

#include "cros-disks/quote.h"

namespace cros_disks {
namespace {

const char kArchiveMountRootDirectory[] = "/media/archive";
const char kDiskMountRootDirectory[] = "/media/removable";
const char kFUSEMountRootDirectory[] = "/media/fuse";

// A temporary directory where every FUSE invocation will have some
// writable subdirectory.
const char kFUSEWritableRootDirectory[] = "/run/fuse";

const char kNonPrivilegedMountUser[] = "chronos";

}  // namespace

Daemon::Daemon(bool has_session_manager)
    : brillo::DBusServiceDaemon(kCrosDisksServiceName),
      has_session_manager_(has_session_manager),
      device_ejector_(&process_reaper_),
      rar_manager_(
          kArchiveMountRootDirectory, &platform_, &metrics_, &process_reaper_),
      zip_manager_(
          kArchiveMountRootDirectory, &platform_, &metrics_, &process_reaper_),
      disk_monitor_(),
      disk_manager_(kDiskMountRootDirectory,
                    &platform_,
                    &metrics_,
                    &process_reaper_,
                    &disk_monitor_,
                    &device_ejector_),
      format_manager_(&process_reaper_),
      partition_manager_(&process_reaper_, &disk_monitor_),
      rename_manager_(&platform_, &process_reaper_),
      fuse_manager_(kFUSEMountRootDirectory,
                    kFUSEWritableRootDirectory,
                    &platform_,
                    &metrics_,
                    &process_reaper_) {
  CHECK(platform_.SetMountUser(kNonPrivilegedMountUser))
      << quote(kNonPrivilegedMountUser)
      << " is not available for non-privileged mount operations";
  CHECK(rar_manager_.Initialize()) << "Failed to initialize the RAR manager";
  CHECK(zip_manager_.Initialize()) << "Failed to initialize the ZIP manager";
  CHECK(disk_manager_.Initialize()) << "Failed to initialize the disk manager";
  CHECK(fuse_manager_.Initialize()) << "Failed to initialize the FUSE manager";
  process_reaper_.Register(this);
}

Daemon::~Daemon() = default;

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  server_ = std::make_unique<CrosDisksServer>(
      bus_, &platform_, &disk_monitor_, &format_manager_, &partition_manager_,
      &rename_manager_);

  // Register mount managers with the commonly used ones come first.
  server_->RegisterMountManager(&disk_manager_);
  server_->RegisterMountManager(&rar_manager_);
  server_->RegisterMountManager(&zip_manager_);
  server_->RegisterMountManager(&fuse_manager_);

  event_moderator_ = std::make_unique<DeviceEventModerator>(
      server_.get(), &disk_monitor_, has_session_manager_);

  if (has_session_manager_) {
    session_manager_proxy_ = std::make_unique<SessionManagerProxy>(bus_);
    session_manager_proxy_->AddObserver(server_.get());
    session_manager_proxy_->AddObserver(event_moderator_.get());
  }

  device_event_watcher_ = base::FileDescriptorWatcher::WatchReadable(
      disk_monitor_.udev_monitor_fd(),
      base::BindRepeating(&Daemon::OnDeviceEvents, base::Unretained(this)));

  server_->RegisterAsync(
      sequencer->GetHandler("Failed to export cros-disks service.", false));
}

void Daemon::OnDeviceEvents() {
  event_moderator_->ProcessDeviceEvents();
}

}  // namespace cros_disks
