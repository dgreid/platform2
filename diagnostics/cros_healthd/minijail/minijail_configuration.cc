// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/minijail/minijail_configuration.h"

#include <sys/mount.h>

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <libminijail.h>
#include <scoped_minijail.h>

namespace diagnostics {

namespace {

// User and group to run as.
constexpr char kCrosHealthdUserName[] = "cros_healthd";
constexpr char kCrosHealthdGroupName[] = "cros_healthd";

// Path to the SECCOMP filter to apply.
constexpr char kSeccompFilterPath[] =
    "/usr/share/policy/cros_healthd-seccomp.policy";

// Checks to see if |file_path| exists on the device. If it does, it will be
// bind-mounted inside |jail| at the same path it exists outside the minijail,
// and it will not be writeable from inside |jail|.
void BindMountIfPathExists(struct minijail* jail,
                           const base::FilePath& file_path) {
  if (!base::PathExists(file_path))
    return;

  const char* path_string = file_path.value().c_str();
  minijail_bind(jail, path_string, path_string, 0);
}

}  // namespace

void ConfigureAndEnterMinijail() {
  ScopedMinijail jail(minijail_new());
  minijail_no_new_privs(jail.get());           // The no_new_privs bit.
  minijail_remount_proc_readonly(jail.get());  // Remount /proc readonly.
  minijail_namespace_ipc(jail.get());          // New IPC namespace.
  minijail_namespace_net(jail.get());          // New network namespace.
  minijail_namespace_uts(jail.get());          // New UTS namespace.
  minijail_namespace_vfs(jail.get());          // New VFS namespace.
  minijail_mount_tmp(jail.get());              // Mount new tmpfs.
  minijail_enter_pivot_root(jail.get(),
                            "/mnt/empty");  // Set /mnt/empty as rootfs.

  // Bind-mount /, /dev and /proc. /dev is necessary to send ioctls to the
  // system's block devices.
  minijail_bind(jail.get(), "/", "/", 0);
  minijail_bind(jail.get(), "/dev", "/dev", 0);
  minijail_bind(jail.get(), "/proc", "/proc", 0);

  // Create a new tmpfs filesystem for /run and mount necessary files.
  minijail_mount_with_data(jail.get(), "tmpfs", "/run", "tmpfs", 0, "");
  minijail_bind(jail.get(), "/run/dbus", "/run/dbus",
                0);  // Shared socket file for talking to the D-Bus daemon.
  minijail_bind(jail.get(), "/run/systemd/journal", "/run/systemd/journal",
                0);  // Needed for logging.
  minijail_bind(jail.get(), "/run/chromeos-config/v1",
                "/run/chromeos-config/v1",
                0);  // Needed for access to chromeos-config.

  // Create a new tmpfs filesystem for /sys and mount necessary files.
  minijail_mount_with_data(jail.get(), "tmpfs", "/sys", "tmpfs", 0, "");
  minijail_bind(jail.get(), "/sys/block", "/sys/block",
                0);  // Files related to the system's block devices.
  minijail_bind(jail.get(), "/sys/devices", "/sys/devices",
                0);  // Needed to get the names of the block device dev nodes.
  minijail_bind(
      jail.get(), "/sys/devices/system/cpu", "/sys/devices/system/cpu",
      0);  // Used by the stressapptest diagnostic. TODO: Do we need this?
  // The following sysfs paths don't exist on every device, so test for their
  // existence and bind-mount them if they do exist.
  BindMountIfPathExists(
      jail.get(),
      base::FilePath("/sys/class/backlight"));  // Files related to the system's
                                                // backlights.
  BindMountIfPathExists(
      jail.get(),
      base::FilePath("/sys/class/chromeos"));  // Files related to Chrome OS
                                               // hardware devices.
  BindMountIfPathExists(
      jail.get(),
      base::FilePath("/sys/class/power_supply"));  // Files related to the
                                                   // system's power supplies.
  BindMountIfPathExists(
      jail.get(),
      base::FilePath("/sys/firmware/vpd/ro"));  // Files with R/O cached VPD.

  BindMountIfPathExists(
      jail.get(),
      base::FilePath("/sys/firmware/vpd/rw"));  // Files with R/W cached VPD.

  BindMountIfPathExists(
      jail.get(),
      base::FilePath("/sys/class/dmi/id"));  // Files related to the
                                             // system's DMI information.

  // Create a new tmpfs filesystem for /var and mount necessary files.
  minijail_mount_with_data(jail.get(), "tmpfs", "/var", "tmpfs", 0, "");
  minijail_bind(jail.get(), "/var/lib/timezone", "/var/lib/timezone",
                0);  // Symlink for reading the timezone file.
  minijail_bind(jail.get(), "/var/cache/diagnostics", "/var/cache/diagnostics",
                1);  // Diagnostics can create test files in this directory.

  // Bind-mount other necessary files.
  minijail_bind(
      jail.get(), "/dev/shm", "/dev/shm",
      1);  // Allows creation of shared memory files that are used to set up
           // mojo::ScopedHandles which can be returned by GetRoutineUpdate.
  minijail_bind(jail.get(), "/mnt/stateful_partition",
                "/mnt/stateful_partition",
                0);  // Needed by the StatefulPartition probe.
  minijail_bind(jail.get(), "/usr/share/zoneinfo", "/usr/share/zoneinfo",
                0);  // Directory holding timezone files.

  // Run as the cros_healthd user and group. Inherit supplementary groups to
  // allow cros_healthd access to disk files.
  CHECK_EQ(0, minijail_change_user(jail.get(), kCrosHealthdUserName));
  CHECK_EQ(0, minijail_change_group(jail.get(), kCrosHealthdGroupName));
  minijail_inherit_usergroups(jail.get());

  // Apply SECCOMP filtering.
  minijail_use_seccomp_filter(jail.get());
  minijail_parse_seccomp_filters(jail.get(), kSeccompFilterPath);

  minijail_enter(jail.get());
}

void NewMountNamespace() {
  ScopedMinijail j(minijail_new());

  // Create a minimalistic mount namespace with just the bare minimum required.
  minijail_namespace_vfs(j.get());
  if (minijail_enter_pivot_root(j.get(), "/mnt/empty"))
    LOG(FATAL) << "minijail_enter_pivot_root() failed";

  minijail_bind(j.get(), "/", "/", 0);

  if (minijail_mount_with_data(j.get(), "tmpfs", "/run", "tmpfs",
                               MS_NOSUID | MS_NOEXEC | MS_NODEV, nullptr)) {
    LOG(FATAL) << "minijail_mount_with_data(\"/run\") failed";
  }

  // Mount /run/systemd/journal to be able to log to journald.
  if (minijail_bind(j.get(), "/run/systemd/journal", "/run/systemd/journal", 0))
    LOG(FATAL) << "minijail_bind(\"/run/systemd/journal\") failed";

  if (minijail_mount_with_data(j.get(), "/dev", "/dev", "bind",
                               MS_BIND | MS_REC, nullptr)) {
    LOG(FATAL) << "minijail_mount_with_data(\"/dev\") failed";
  }

  minijail_enter(j.get());
}

}  // namespace diagnostics
