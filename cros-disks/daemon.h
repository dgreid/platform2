// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_DAEMON_H_
#define CROS_DISKS_DAEMON_H_

#include <memory>

#include <base/files/file_descriptor_watcher_posix.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/process/process_reaper.h>

#include "cros-disks/cros_disks_server.h"
#include "cros-disks/device_ejector.h"
#include "cros-disks/device_event_moderator.h"
#include "cros-disks/disk_manager.h"
#include "cros-disks/disk_monitor.h"
#include "cros-disks/format_manager.h"
#include "cros-disks/fuse_mount_manager.h"
#include "cros-disks/metrics.h"
#include "cros-disks/partition_manager.h"
#include "cros-disks/platform.h"
#include "cros-disks/rar_manager.h"
#include "cros-disks/rename_manager.h"
#include "cros-disks/session_manager_proxy.h"
#include "cros-disks/zip_manager.h"

namespace cros_disks {

class Daemon : public brillo::DBusServiceDaemon {
 public:
  // |has_session_manager| indicates whether the presence of a SessionManager is
  // expected.
  explicit Daemon(bool has_session_manager);
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

  ~Daemon() override;

 private:
  // brillo::DBusServiceDaemon overrides:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  void OnDeviceEvents();

  const bool has_session_manager_;
  Metrics metrics_;
  Platform platform_;
  brillo::ProcessReaper process_reaper_;
  DeviceEjector device_ejector_;
  RarManager rar_manager_;
  ZipManager zip_manager_;
  DiskMonitor disk_monitor_;
  DiskManager disk_manager_;
  FormatManager format_manager_;
  PartitionManager partition_manager_;
  RenameManager rename_manager_;
  FUSEMountManager fuse_manager_;
  std::unique_ptr<DeviceEventModerator> event_moderator_;
  std::unique_ptr<SessionManagerProxy> session_manager_proxy_;
  std::unique_ptr<CrosDisksServer> server_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      device_event_watcher_;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_DAEMON_H_
