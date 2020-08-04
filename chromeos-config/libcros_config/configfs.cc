// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos-config/libcros_config/configfs.h"

#include <cerrno>
#include <fcntl.h>
#include <linux/loop.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <unistd.h>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/file_utils.h>
#include "chromeos-config/libcros_config/cros_config_interface.h"

namespace brillo {

const char kConfigFSPrivateDirName[] = "private";
const char kConfigFSV1DirName[] = "v1";
const char kConfigFSIdentityName[] = "identity.json";
const char kConfigFSPrivateFSType[] = "squashfs";

bool SetupMountPath(const base::FilePath& mount_path,
                    base::FilePath* private_path_out,
                    base::FilePath* v1_path_out) {
  if (!base::DirectoryExists(mount_path)) {
    CROS_CONFIG_LOG(ERROR) << "Either " << mount_path.value()
                           << " does not exist, or it is not a directory.";
    return false;
  }

  *private_path_out = mount_path.Append(kConfigFSPrivateDirName);
  *v1_path_out = mount_path.Append(kConfigFSV1DirName);
  for (auto path : {*private_path_out, *v1_path_out}) {
    if (!MkdirRecursively(path, 0755).is_valid()) {
      CROS_CONFIG_LOG(ERROR) << "Unable to create " << path.value() << " ("
                             << logging::SystemErrorCodeToString(errno) << ").";
      return false;
    }
  }
  return true;
}

static bool TrySetupLoopDevice(const base::FilePath& backing_file,
                               base::FilePath* loop_file_out) {
  const char loop_control_file[] = "/dev/loop-control";
  base::ScopedFD loop_control_fd(open(loop_control_file, O_RDWR));
  if (!loop_control_fd.is_valid()) {
    CROS_CONFIG_LOG(ERROR) << "Error opening loop control file "
                           << loop_control_file << ": "
                           << logging::SystemErrorCodeToString(errno);
    return false;
  }

  int device_number = ioctl(loop_control_fd.get(), LOOP_CTL_GET_FREE);
  if (device_number < 0) {
    CROS_CONFIG_LOG(ERROR) << "Error getting free loop device number: "
                           << logging::SystemErrorCodeToString(errno);
    return false;
  }

  const auto loop_file_name = base::StringPrintf("/dev/loop%d", device_number);
  base::ScopedFD loop_file_fd(open(loop_file_name.c_str(), O_RDWR));
  if (!loop_file_fd.is_valid()) {
    CROS_CONFIG_LOG(ERROR) << "Error opening loop file " << loop_file_name
                           << ": " << logging::SystemErrorCodeToString(errno);
    return false;
  }

  // We don't close the loop control device until after we open the
  // loop device with the corresponding number. This is to prevent a
  // race condition when two processes get the same free device
  // number. When we keep the loop control open, other processes will
  // get EBUSY opening /dev/loop-control until we close it.
  loop_control_fd.reset();

  base::ScopedFD backing_file_fd(open(backing_file.value().c_str(), O_RDONLY));
  if (!backing_file_fd.is_valid()) {
    CROS_CONFIG_LOG(ERROR) << "Error opening backing file "
                           << backing_file.value() << ": "
                           << logging::SystemErrorCodeToString(errno);
    return false;
  }

  if (ioctl(loop_file_fd.get(), LOOP_SET_FD, backing_file_fd.get()) < 0) {
    CROS_CONFIG_LOG(ERROR) << "Error setting backing file "
                           << backing_file.value() << " to loop device "
                           << loop_file_name << ": "
                           << logging::SystemErrorCodeToString(errno);
    return false;
  }

  *loop_file_out = base::FilePath(loop_file_name);
  return true;
}

// During early boot, a number of resources can be busy
// (/dev/loop-control or /dev/loopN) due to utilization by other
// processes on the system.  We wrap the setup of the loop device, and
// when we encounter an error that might indicate a device is busy, we
// will retry a number of times.
bool SetupLoopDevice(const base::FilePath& backing_file,
                     base::FilePath* loop_file_out) {
  const int total_retries = 25;
  const int retry_wait_ms = 10;
  int current_try = 0;

  while (true) {
    if (TrySetupLoopDevice(backing_file, loop_file_out)) {
      return true;
    }

    CROS_CONFIG_LOG(ERROR) << "TRY " << current_try << "/" << total_retries
                           << ": Setting up loop device ("
                           << logging::SystemErrorCodeToString(errno) << ")";

    switch (errno) {
      // We may get any of these errors if we need to retry.
      case EBUSY:
      case EACCES:
      case ENOENT:
        if (current_try < total_retries) {
          ++current_try;
          CROS_CONFIG_LOG(ERROR) << "Retrying in " << retry_wait_ms << " ms";
          base::PlatformThread::Sleep(
              base::TimeDelta::FromMilliseconds(retry_wait_ms));
          continue;
        }
        CROS_CONFIG_LOG(ERROR) << "Max retries exceeded";
        return false;
      default:
        CROS_CONFIG_LOG(ERROR)
            << "No more retries, this does not look like a busy resource";
        return false;
    }
  }
}

bool Mount(const base::FilePath& source,
           const base::FilePath& target,
           const char* filesystemtype,
           unsigned long mountflags,
           const std::vector<std::string>& options) {
  // For ConfigFS, there are certain options we always want on for
  // additional security.  There should never be executables or
  // special device files stored in ConfigFS.
  mountflags |= MS_NODEV | MS_NOEXEC | MS_NOSUID;

  const std::string options_str = base::JoinString(options, ",");
  if (mount(source.value().c_str(), target.value().c_str(), filesystemtype,
            mountflags, options_str.c_str()) < 0) {
    CROS_CONFIG_LOG(ERROR) << "Error mounting " << source.value() << " to "
                           << target.value() << ": "
                           << logging::SystemErrorCodeToString(errno);
    return false;
  }
  return true;
}

bool Bind(const base::FilePath& source, const base::FilePath& target) {
  return Mount(source, target, nullptr, MS_BIND);
}

bool Remount(const base::FilePath& target,
             unsigned long mountflags,
             const std::vector<std::string>& options) {
  return Mount(base::FilePath(), target, nullptr, MS_REMOUNT | mountflags,
               options);
}

}  // namespace brillo
