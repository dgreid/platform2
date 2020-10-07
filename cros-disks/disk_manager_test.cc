// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/disk_manager.h"

#include <stdlib.h>
#include <sys/mount.h>
#include <time.h>

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <brillo/process/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/device_ejector.h"
#include "cros-disks/disk.h"
#include "cros-disks/disk_monitor.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/mounter.h"
#include "cros-disks/platform.h"
#include "cros-disks/system_mounter.h"

namespace cros_disks {
namespace {

using testing::_;
using testing::Contains;
using testing::Return;
using testing::StrEq;

const char kMountRootDirectory[] = "/media/removable";

class MockDeviceEjector : public DeviceEjector {
 public:
  MockDeviceEjector() : DeviceEjector(nullptr) {}

  MOCK_METHOD(bool, Eject, (const std::string&), (override));
};

class MockDiskMonitor : public DiskMonitor {
 public:
  MockDiskMonitor() = default;

  bool Initialize() override { return true; }

  MOCK_METHOD(std::vector<Disk>, EnumerateDisks, (), (const, override));
  MOCK_METHOD(bool,
              GetDiskByDevicePath,
              (const base::FilePath&, Disk*),
              (const, override));
};

class MockPlatform : public Platform {
 public:
  MockPlatform() = default;

  MOCK_METHOD(MountErrorType,
              Unmount,
              (const std::string&, int),
              (const, override));
};

class MockMountPoint : public MountPoint {
 public:
  explicit MockMountPoint(const base::FilePath& path) : MountPoint(path) {}
  ~MockMountPoint() override { DestructorUnmount(); }

  MOCK_METHOD(MountErrorType, UnmountImpl, (), (override));
};

}  // namespace

class DiskManagerTest : public ::testing::Test {
 public:
  DiskManagerTest()
      : manager_(kMountRootDirectory,
                 &platform_,
                 &metrics_,
                 &process_reaper_,
                 &monitor_,
                 &ejector_) {}

 protected:
  Metrics metrics_;
  MockPlatform platform_;
  brillo::ProcessReaper process_reaper_;
  MockDeviceEjector ejector_;
  MockDiskMonitor monitor_;
  DiskManager manager_;
};

TEST_F(DiskManagerTest, CreateExFATMounter) {
  Disk disk;
  disk.device_file = "/dev/sda1";

  DiskManager::Filesystem filesystem = {.type = "exfat"};

  std::string target_path = "/media/disk";
  std::vector<std::string> options = {"rw", "nodev", "noexec", "nosuid"};

  auto mounter = manager_.CreateMounter(disk, filesystem, target_path, options);
  EXPECT_NE(nullptr, mounter.get());
  const FUSEMounterLegacy* legacy =
      static_cast<FUSEMounterLegacy*>(mounter.get());
  EXPECT_EQ("rw,nodev,noexec,nosuid", legacy->mount_options().ToString());
}

TEST_F(DiskManagerTest, CreateNTFSMounter) {
  Disk disk;
  disk.device_file = "/dev/sda1";

  DiskManager::Filesystem filesystem = {.type = "ntfs"};

  std::string target_path = "/media/disk";
  std::vector<std::string> options = {"rw", "nodev", "noexec", "nosuid"};

  auto mounter = manager_.CreateMounter(disk, filesystem, target_path, options);
  EXPECT_NE(nullptr, mounter.get());
  const FUSEMounterLegacy* legacy =
      static_cast<FUSEMounterLegacy*>(mounter.get());
  EXPECT_EQ("rw,nodev,noexec,nosuid", legacy->mount_options().ToString());
}

TEST_F(DiskManagerTest, CreateVFATSystemMounter) {
  Disk disk;
  disk.device_file = "/dev/sda1";

  DiskManager::Filesystem filesystem = {.type = "vfat"};
  filesystem.extra_mount_options = {"utf8", "shortname=mixed"};

  std::string target_path = "/media/disk";
  std::vector<std::string> options = {"rw", "nodev", "noexec", "nosuid"};

  // Override the time zone to make this test deterministic.
  // This test uses AWST (Perth, Australia), which is UTC+8, as the test time
  // zone. However, the TZ environment variable is the time to be added to local
  // time to get to UTC, hence the negative.
  setenv("TZ", "UTC-8", 1);

  auto mounter = manager_.CreateMounter(disk, filesystem, target_path, options);
  ASSERT_NE(nullptr, mounter.get());
  const SystemMounter* sysmounter =
      static_cast<const SystemMounter*>(mounter.get());
  EXPECT_FALSE(sysmounter->read_only());
  EXPECT_THAT(sysmounter->options(), Contains(StrEq("time_offset=480")));
}

TEST_F(DiskManagerTest, CreateExt4SystemMounter) {
  Disk disk;
  disk.device_file = "/dev/sda1";

  std::string target_path = "/media/disk";
  // "mand" is not an allowed option, so it should be skipped.
  std::vector<std::string> options = {"rw", "nodev", "noexec", "nosuid",
                                      "mand"};

  DiskManager::Filesystem filesystem = {.type = "ext4"};
  auto mounter = manager_.CreateMounter(disk, filesystem, target_path, options);
  ASSERT_NE(nullptr, mounter.get());
  const SystemMounter* sysmounter =
      static_cast<const SystemMounter*>(mounter.get());
  EXPECT_FALSE(sysmounter->read_only());
}

TEST_F(DiskManagerTest, GetFilesystem) {
  EXPECT_EQ(nullptr, manager_.GetFilesystem("nonexistent-fs"));

  DiskManager::Filesystem normal_fs = {.type = "normal-fs"};
  EXPECT_EQ(nullptr, manager_.GetFilesystem(normal_fs.type));
  manager_.RegisterFilesystem(normal_fs);
  EXPECT_NE(nullptr, manager_.GetFilesystem(normal_fs.type));
}

TEST_F(DiskManagerTest, RegisterFilesystem) {
  const std::map<std::string, DiskManager::Filesystem>& filesystems =
      manager_.filesystems_;
  EXPECT_EQ(0, filesystems.size());
  EXPECT_TRUE(filesystems.find("nonexistent") == filesystems.end());

  DiskManager::Filesystem fat_fs = {.type = "fat"};
  fat_fs.accepts_user_and_group_id = true;
  manager_.RegisterFilesystem(fat_fs);
  EXPECT_EQ(1, filesystems.size());
  EXPECT_TRUE(filesystems.find(fat_fs.type) != filesystems.end());

  DiskManager::Filesystem vfat_fs = {.type = "vfat"};
  vfat_fs.accepts_user_and_group_id = true;
  manager_.RegisterFilesystem(vfat_fs);
  EXPECT_EQ(2, filesystems.size());
  EXPECT_TRUE(filesystems.find(vfat_fs.type) != filesystems.end());
}

TEST_F(DiskManagerTest, CanMount) {
  EXPECT_TRUE(manager_.CanMount("/dev/sda1"));
  EXPECT_TRUE(manager_.CanMount("/devices/block/sda/sda1"));
  EXPECT_TRUE(manager_.CanMount("/sys/devices/block/sda/sda1"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1/"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk 1"));
  EXPECT_FALSE(manager_.CanMount("/media/archive/test.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/archive/test.zip/"));
  EXPECT_FALSE(manager_.CanMount("/media/archive/test 1.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1/test.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1/test 1.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/disk1/dir1/test.zip"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/test.zip/test1.zip"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos/user/Downloads/test1.zip"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos/user/GCache/test1.zip"));
  EXPECT_FALSE(
      manager_.CanMount("/home/chronos"
                        "/u-0123456789abcdef0123456789abcdef01234567"
                        "/Downloads/test1.zip"));
  EXPECT_FALSE(
      manager_.CanMount("/home/chronos"
                        "/u-0123456789abcdef0123456789abcdef01234567"
                        "/GCache/test1.zip"));
  EXPECT_FALSE(manager_.CanMount(""));
  EXPECT_FALSE(manager_.CanMount("/tmp"));
  EXPECT_FALSE(manager_.CanMount("/media/removable"));
  EXPECT_FALSE(manager_.CanMount("/media/removable/"));
  EXPECT_FALSE(manager_.CanMount("/media/archive"));
  EXPECT_FALSE(manager_.CanMount("/media/archive/"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos/user/Downloads"));
  EXPECT_FALSE(manager_.CanMount("/home/chronos/user/Downloads/"));
}

TEST_F(DiskManagerTest, DoMountDiskWithNonexistentSourcePath) {
  std::string filesystem_type = "ext3";
  std::string source_path = "/dev/nonexistent-path";
  std::string mount_path = "/tmp/cros-disks-test";
  MountOptions applied_options;
  MountErrorType mount_error = MOUNT_ERROR_UNKNOWN;
  std::unique_ptr<MountPoint> mount_point = manager_.DoMount(
      source_path, filesystem_type, {} /* options */,
      base::FilePath(mount_path), &applied_options, &mount_error);
  EXPECT_FALSE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_INVALID_DEVICE_PATH, mount_error);
}

TEST_F(DiskManagerTest, EjectDevice) {
  const base::FilePath kMountPath("/media/removable/disk");
  Disk disk;

  std::unique_ptr<MockMountPoint> mount_point =
      std::make_unique<MockMountPoint>(kMountPath);
  EXPECT_CALL(*mount_point, UnmountImpl()).WillOnce(Return(MOUNT_ERROR_NONE));
  disk.device_file = "/dev/sda";
  disk.media_type = DEVICE_MEDIA_USB;
  EXPECT_CALL(ejector_, Eject("/dev/sda")).Times(0);
  std::unique_ptr<MountPoint> wrapped_mount_point =
      manager_.MaybeWrapMountPointForEject(std::move(mount_point), disk);
  EXPECT_EQ(MOUNT_ERROR_NONE, wrapped_mount_point->Unmount());

  mount_point = std::make_unique<MockMountPoint>(kMountPath);
  EXPECT_CALL(*mount_point, UnmountImpl()).WillOnce(Return(MOUNT_ERROR_NONE));
  disk.device_file = "/dev/sr0";
  disk.media_type = DEVICE_MEDIA_OPTICAL_DISC;
  EXPECT_CALL(ejector_, Eject("/dev/sr0")).WillOnce(Return(true));
  wrapped_mount_point =
      manager_.MaybeWrapMountPointForEject(std::move(mount_point), disk);
  EXPECT_EQ(MOUNT_ERROR_NONE, wrapped_mount_point->Unmount());

  mount_point = std::make_unique<MockMountPoint>(kMountPath);
  EXPECT_CALL(*mount_point, UnmountImpl()).WillOnce(Return(MOUNT_ERROR_NONE));
  disk.device_file = "/dev/sr1";
  disk.media_type = DEVICE_MEDIA_DVD;
  EXPECT_CALL(ejector_, Eject("/dev/sr1")).WillOnce(Return(true));
  wrapped_mount_point =
      manager_.MaybeWrapMountPointForEject(std::move(mount_point), disk);
  EXPECT_EQ(MOUNT_ERROR_NONE, wrapped_mount_point->Unmount());
}

TEST_F(DiskManagerTest, EjectDeviceWhenUnmountFailed) {
  const base::FilePath kMountPath("/media/removable/disk");
  Disk disk;
  disk.device_file = "/dev/sr0";
  disk.media_type = DEVICE_MEDIA_OPTICAL_DISC;

  std::unique_ptr<MockMountPoint> mount_point =
      std::make_unique<MockMountPoint>(kMountPath);
  EXPECT_CALL(*mount_point, UnmountImpl())
      .WillRepeatedly(Return(MOUNT_ERROR_UNKNOWN));
  EXPECT_CALL(ejector_, Eject("/dev/sr0")).Times(0);
  std::unique_ptr<MountPoint> wrapped_mount_point =
      manager_.MaybeWrapMountPointForEject(std::move(mount_point), disk);
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN, wrapped_mount_point->Unmount());
}

TEST_F(DiskManagerTest, EjectDeviceWhenExplicitlyDisabled) {
  const base::FilePath kMountPath("/media/removable/disk");
  Disk disk;
  disk.device_file = "/dev/sr0";
  disk.media_type = DEVICE_MEDIA_OPTICAL_DISC;

  std::unique_ptr<MockMountPoint> mount_point =
      std::make_unique<MockMountPoint>(kMountPath);
  EXPECT_CALL(*mount_point, UnmountImpl()).WillOnce(Return(MOUNT_ERROR_NONE));
  manager_.eject_device_on_unmount_ = false;
  EXPECT_CALL(ejector_, Eject("/dev/sr0")).Times(0);
  std::unique_ptr<MountPoint> wrapped_mount_point =
      manager_.MaybeWrapMountPointForEject(std::move(mount_point), disk);
  EXPECT_EQ(MOUNT_ERROR_NONE, wrapped_mount_point->Unmount());
}

TEST_F(DiskManagerTest, EjectDeviceWhenReleased) {
  const base::FilePath kMountPath("/media/removable/disk");
  Disk disk;
  disk.device_file = "/dev/sr0";
  disk.media_type = DEVICE_MEDIA_OPTICAL_DISC;

  std::unique_ptr<MockMountPoint> mount_point =
      std::make_unique<MockMountPoint>(kMountPath);
  EXPECT_CALL(*mount_point, UnmountImpl()).Times(0);
  EXPECT_CALL(ejector_, Eject("/dev/sr0")).Times(0);
  std::unique_ptr<MountPoint> wrapped_mount_point =
      manager_.MaybeWrapMountPointForEject(std::move(mount_point), disk);
  wrapped_mount_point->Release();
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, wrapped_mount_point->Unmount());
}

}  // namespace cros_disks
