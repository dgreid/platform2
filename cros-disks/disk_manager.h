// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_DISK_MANAGER_H_
#define CROS_DISKS_DISK_MANAGER_H_

#include <libudev.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <gtest/gtest_prod.h>

#include "cros-disks/disk.h"
#include "cros-disks/mount_manager.h"

namespace cros_disks {

class DeviceEjector;
class DiskMonitor;
class Mounter;
class Platform;

// The DiskManager is responsible for mounting removable media.
//
// This class is designed to run within a single-threaded GMainLoop application
// and should not be considered thread safe.
class DiskManager : public MountManager {
 public:
  DiskManager(const std::string& mount_root,
              Platform* platform,
              Metrics* metrics,
              brillo::ProcessReaper* process_reaper,
              DiskMonitor* disk_monitor,
              DeviceEjector* device_ejector);
  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  ~DiskManager() override;

  // Initializes the disk manager and registers default filesystems.
  // Returns true on success.
  bool Initialize() override;

  // Returns true if mounting |source_path| is supported.
  bool CanMount(const std::string& source_path) const override;

  // Returns the type of mount sources supported by the manager.
  MountSourceType GetMountSourceType() const override {
    return MOUNT_SOURCE_REMOVABLE_DEVICE;
  }

  // Unmounts all mounted paths.
  bool UnmountAll() override;

  // Registers a set of default filesystems to the disk manager.
  void RegisterDefaultFilesystems();

 protected:
  // Mounts |source_path| to |mount_path| as |filesystem_type| with |options|.
  std::unique_ptr<MountPoint> DoMount(const std::string& source_path,
                                      const std::string& filesystem_type,
                                      const std::vector<std::string>& options,
                                      const base::FilePath& mount_path,
                                      MountOptions* applied_options,
                                      MountErrorType* error) override;

  // Returns a suggested mount path for a source path.
  std::string SuggestMountPath(const std::string& source_path) const override;

  // Returns true to reserve a mount path on errors due to unknown or
  // unsupported filesystems.
  bool ShouldReserveMountPathOnError(MountErrorType error_type) const override;

 private:
  // MountPoint implementation that ejects the device on unmount.
  class EjectingMountPoint;

  // Properties of a filesystem.
  struct Filesystem {
    // Filesystem type.
    std::string type;

    // This variable is set to true if default user and group ID can be
    // specified for mounting the filesystem.
    bool accepts_user_and_group_id = false;

    // Extra mount options to specify when mounting the filesystem.
    std::vector<std::string> extra_mount_options;

    // This variable is set to true if the filesystem should be mounted
    // as read-only.
    bool is_read_only = false;
  };

  // Creates an appropriate mounter object for a given filesystem.
  std::unique_ptr<Mounter> CreateMounter(
      const Disk& disk,
      const Filesystem& filesystem,
      const std::string& target_path,
      const std::vector<std::string>& options) const;

  // Registers a filesystem to the disk manager.
  // Subsequent registrations of the same filesystem type are ignored.
  void RegisterFilesystem(const Filesystem& filesystem);

  // Returns a Filesystem object if a given filesystem type is supported.
  // Otherwise, it returns NULL. This pointer is owned by the DiskManager.
  const Filesystem* GetFilesystem(const std::string& filesystem_type) const;

  // Ejects media for the device |device_file|. Return true if the eject process
  // has started or |eject_device_on_unmount_| is false, or false if the eject
  // process failed.
  bool EjectDevice(const std::string& device_file);

  // If |disk| is an optical disk, wrap |mount_point| in a wrapper that ejects
  // the disk on a successful unmount. If |disk| is not an optical disk, returns
  // |mount_point|. This is exposed as a function to allow ejecting behaviour to
  // be tested.
  std::unique_ptr<MountPoint> MaybeWrapMountPointForEject(
      std::unique_ptr<MountPoint> mount_point, const Disk& disk);

  DiskMonitor* const disk_monitor_;
  DeviceEjector* const device_ejector_;

  // Set to true if devices should be ejected upon unmount.
  bool eject_device_on_unmount_;

  // A mapping from a mount path to the corresponding device that should
  // be ejected on unmount.
  std::map<std::string, Disk> devices_to_eject_on_unmount_;

  // A set of supported filesystems indexed by filesystem type.
  std::map<std::string, Filesystem> filesystems_;

  FRIEND_TEST(DiskManagerTest, CreateExFATMounter);
  FRIEND_TEST(DiskManagerTest, CreateNTFSMounter);
  FRIEND_TEST(DiskManagerTest, CreateVFATSystemMounter);
  FRIEND_TEST(DiskManagerTest, CreateExt4SystemMounter);
  FRIEND_TEST(DiskManagerTest, GetFilesystem);
  FRIEND_TEST(DiskManagerTest, RegisterFilesystem);
  FRIEND_TEST(DiskManagerTest, DoMountDiskWithNonexistentSourcePath);
  FRIEND_TEST(DiskManagerTest, EjectDevice);
  FRIEND_TEST(DiskManagerTest, EjectDeviceWhenUnmountFailed);
  FRIEND_TEST(DiskManagerTest, EjectDeviceWhenExplicitlyDisabled);
  FRIEND_TEST(DiskManagerTest, EjectDeviceWhenReleased);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_DISK_MANAGER_H_
