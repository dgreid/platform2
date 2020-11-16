// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/disk_manager.h"

#include <stdlib.h>
#include <sys/mount.h>
#include <time.h>

#include <map>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
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
using testing::AllOf;
using testing::Contains;
using testing::DoAll;
using testing::DoDefault;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::Invoke;
using testing::Return;
using testing::SaveArg;
using testing::StrEq;

const uint64_t kExpectedMountFlags =
    MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_NOSYMFOLLOW;

class MockDeviceEjector : public DeviceEjector {
 public:
  MockDeviceEjector() : DeviceEjector(nullptr) {}

  MOCK_METHOD(bool, Eject, (const std::string&), (override));
};

class FakeDiskMonitor : public DiskMonitor {
 public:
  FakeDiskMonitor() = default;

  bool Initialize() override { return true; }

  std::vector<Disk> EnumerateDisks() const override { return disks_; }

  bool GetDiskByDevicePath(const base::FilePath& path,
                           Disk* disk) const override {
    for (const auto& d : disks_) {
      if (d.device_file == path.value()) {
        if (disk)
          *disk = d;
        return true;
      }
    }
    return false;
  }

  std::vector<Disk> disks_;
};

class MockPlatform : public Platform {
 public:
  MockPlatform() = default;

  MOCK_METHOD(MountErrorType,
              Unmount,
              (const std::string&, int),
              (const, override));

  MOCK_METHOD(MountErrorType,
              Mount,
              (const std::string& source,
               const std::string& target,
               const std::string& filesystem_type,
               uint64_t flags,
               const std::string& options),
              (const, override));

  MOCK_METHOD(bool, PathExists, (const std::string& path), (const, override));
  bool PathExistsImpl(const std::string& path) const {
    return Platform::PathExists(path);
  }

  bool Lstat(const std::string& path,
             base::stat_wrapper_t* out) const override {
    if (base::StartsWith(path, "/dev/", base::CompareCase::SENSITIVE)) {
      out->st_mode = S_IFBLK | 0640;
      return true;
    }
    NOTREACHED();
    return false;
  }

  bool SetOwnership(const std::string& path,
                    uid_t user_id,
                    gid_t group_id) const override {
    return true;
  }

  bool SetPermissions(const std::string& path, mode_t mode) const override {
    return true;
  }

  bool GetUserAndGroupId(const std::string& user_name,
                         uid_t* user_id,
                         gid_t* group_id) const override {
    if (user_name == "fuse-exfat") {
      *user_id = 111;
      *group_id = 222;
      return true;
    }
    if (user_name == "ntfs-3g") {
      *user_id = 333;
      *group_id = 444;
      return true;
    }
    NOTREACHED();
    return false;
  }
};

class MockMountPoint : public MountPoint {
 public:
  explicit MockMountPoint(const base::FilePath& path) : MountPoint(path) {}
  ~MockMountPoint() override { DestructorUnmount(); }

  MOCK_METHOD(MountErrorType, UnmountImpl, (), (override));
};

class MockSandboxedProcess : public SandboxedProcess {
 public:
  pid_t StartImpl(base::ScopedFD, base::ScopedFD, base::ScopedFD) override {
    OnStart(arguments());
    return 123;
  }
  int WaitImpl() override { return WaitNonBlockingImpl(); }
  int WaitNonBlockingImpl() override { return 0; }
  MOCK_METHOD(void, OnStart, (const std::vector<std::string>& args), (const));
};

}  // namespace

class DiskManagerTest : public ::testing::Test, public SandboxedProcessFactory {
 public:
  DiskManagerTest() {}

  void SetUp() override {
    CHECK(dir_.CreateUniqueTempDir());
    ON_CALL(platform_, PathExists)
        .WillByDefault(Invoke(&platform_, &MockPlatform::PathExistsImpl));
    ON_CALL(platform_, PathExists("/dev/sda1")).WillByDefault(Return(true));
    manager_ = std::make_unique<DiskManager>(dir_.GetPath().value(), &platform_,
                                             &metrics_, &process_reaper_,
                                             &monitor_, &ejector_, this);
    CHECK(manager_->Initialize());
  }

 protected:
  std::unique_ptr<SandboxedProcess> CreateSandboxedProcess() const override {
    auto ptr = std::make_unique<MockSandboxedProcess>();
    ON_CALL(*ptr, OnStart).WillByDefault(SaveArg<0>(&fuse_args_));
    return ptr;
  }

  base::ScopedTempDir dir_;
  Metrics metrics_;
  MockPlatform platform_;
  brillo::ProcessReaper process_reaper_;
  MockDeviceEjector ejector_;
  FakeDiskMonitor monitor_;
  std::unique_ptr<DiskManager> manager_;
  mutable std::vector<std::string> fuse_args_;
};

MATCHER_P(HasBits, bits, "") {
  return bits == (bits & arg);
}

TEST_F(DiskManagerTest, MountBootDeviceNotAllowed) {
  EXPECT_CALL(platform_, Mount).Times(0);
  EXPECT_CALL(platform_, Unmount).Times(0);
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "vfat", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_INVALID_DEVICE_PATH, err);
  monitor_.disks_.push_back({
      .is_on_boot_device = true,
      .device_file = "/dev/sda1",
      .filesystem_type = "vfat",
  });
  err = manager_->Mount("/dev/sda1", "vfat", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_INVALID_DEVICE_PATH, err);
}

TEST_F(DiskManagerTest, MountNonExistingDevice) {
  EXPECT_CALL(platform_, Mount).Times(0);
  EXPECT_CALL(platform_, Unmount).Times(0);
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "vfat",
  });
  EXPECT_CALL(platform_, PathExists("/dev/sda1")).WillRepeatedly(Return(false));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "vfat", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_INVALID_DEVICE_PATH, err);
}

TEST_F(DiskManagerTest, MountUsesLabel) {
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "vfat",
      .label = "foo",
  });

  std::string opts;
  EXPECT_CALL(platform_, Mount("/dev/sda1", _, "vfat", _, _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
  EXPECT_EQ("foo", base::FilePath(path).BaseName().value());

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, MountFAT) {
  // Override the time zone to make this test deterministic.
  // This test uses AWST (Perth, Australia), which is UTC+8, as the test time
  // zone. However, the TZ environment variable is the time to be added to local
  // time to get to UTC, hence the negative.
  setenv("TZ", "UTC-8", 1);

  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "vfat",
  });

  std::string opts;
  EXPECT_CALL(platform_,
              Mount("/dev/sda1", _, "vfat", HasBits(kExpectedMountFlags), _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
  auto options =
      base::SplitString(opts, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  EXPECT_THAT(options,
              AllOf(Contains("uid=1000"), Contains("gid=1001"),
                    Contains("shortname=mixed"), Contains("time_offset=480")));

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, MountExFAT) {
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "exfat",
      .label = "foo",
  });

  std::string opts;
  EXPECT_CALL(platform_, Mount("/dev/sda1", _, "fuseblk.exfat",
                               HasBits(kExpectedMountFlags), _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
  auto options =
      base::SplitString(opts, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  EXPECT_THAT(options,
              AllOf(Contains("user_id=1000"), Contains("group_id=1001")));
  EXPECT_THAT(fuse_args_,
              ElementsAre("/usr/sbin/mount.exfat-fuse", "-o",
                          HasSubstr("uid=1000,gid=1001"), "/dev/sda1", _));

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, MountNTFS) {
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "ntfs",
      .label = "foo",
  });

  std::string opts;
  EXPECT_CALL(platform_, Mount("/dev/sda1", _, "fuseblk.ntfs",
                               HasBits(kExpectedMountFlags), _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
  auto options =
      base::SplitString(opts, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  EXPECT_THAT(options,
              AllOf(Contains("user_id=1000"), Contains("group_id=1001")));
  EXPECT_THAT(fuse_args_,
              ElementsAre("/usr/bin/ntfs-3g", "-o",
                          HasSubstr("uid=1000,gid=1001"), "/dev/sda1", _));

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, MountCD) {
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "iso9660",
      .label = "foo",
  });

  std::string opts;
  EXPECT_CALL(platform_, Mount("/dev/sda1", _, "iso9660",
                               HasBits(kExpectedMountFlags | MS_RDONLY), _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
  auto options =
      base::SplitString(opts, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  EXPECT_THAT(options, AllOf(Contains("uid=1000"), Contains("gid=1001")));

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, MountDVD) {
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "udf",
      .label = "foo",
  });

  std::string opts;
  EXPECT_CALL(platform_, Mount("/dev/sda1", _, "udf",
                               HasBits(kExpectedMountFlags | MS_RDONLY), _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
  auto options =
      base::SplitString(opts, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  EXPECT_THAT(options, AllOf(Contains("uid=1000"), Contains("gid=1001")));

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, MountHFS) {
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "hfsplus",
      .label = "foo",
  });

  std::string opts;
  EXPECT_CALL(platform_,
              Mount("/dev/sda1", _, "hfsplus", HasBits(kExpectedMountFlags), _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
  auto options =
      base::SplitString(opts, ",", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  EXPECT_THAT(options, AllOf(Contains("uid=1000"), Contains("gid=1001")));

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, MountReadOnlyMedia) {
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .is_read_only = true,
      .device_file = "/dev/sda1",
      .filesystem_type = "vfat",
      .label = "foo",
  });

  std::string opts;
  EXPECT_CALL(platform_, Mount("/dev/sda1", _, "vfat",
                               HasBits(kExpectedMountFlags | MS_RDONLY), _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, MountForcedReadOnly) {
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "vfat",
      .label = "foo",
  });

  std::string opts;
  EXPECT_CALL(platform_, Mount("/dev/sda1", _, "vfat",
                               HasBits(kExpectedMountFlags | MS_RDONLY), _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));
  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {"ro"}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, MountRetryReadOnlyIfFailed) {
  monitor_.disks_.push_back({
      .is_on_boot_device = false,
      .device_file = "/dev/sda1",
      .filesystem_type = "vfat",
      .label = "foo",
  });

  EXPECT_CALL(platform_,
              Mount("/dev/sda1", _, "vfat", HasBits(kExpectedMountFlags), _))
      .WillOnce(Return(MOUNT_ERROR_PATH_NOT_MOUNTED));
  std::string opts;
  EXPECT_CALL(platform_, Mount("/dev/sda1", _, "vfat",
                               HasBits(kExpectedMountFlags | MS_RDONLY), _))
      .WillOnce(DoAll(SaveArg<4>(&opts), Return(MOUNT_ERROR_NONE)));

  std::string path;
  MountErrorType err = manager_->Mount("/dev/sda1", "", {}, &path);
  EXPECT_EQ(MOUNT_ERROR_NONE, err);

  EXPECT_CALL(platform_, Unmount(path, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  err = manager_->Unmount("/dev/sda1");
  EXPECT_EQ(MOUNT_ERROR_NONE, err);
}

TEST_F(DiskManagerTest, CanMount) {
  EXPECT_TRUE(manager_->CanMount("/dev/sda1"));
  EXPECT_TRUE(manager_->CanMount("/devices/block/sda/sda1"));
  EXPECT_TRUE(manager_->CanMount("/sys/devices/block/sda/sda1"));
  EXPECT_FALSE(manager_->CanMount("/media/removable/disk1"));
  EXPECT_FALSE(manager_->CanMount("/media/removable/disk1/"));
  EXPECT_FALSE(manager_->CanMount("/media/removable/disk 1"));
  EXPECT_FALSE(manager_->CanMount("/media/archive/test.zip"));
  EXPECT_FALSE(manager_->CanMount("/media/archive/test.zip/"));
  EXPECT_FALSE(manager_->CanMount("/media/archive/test 1.zip"));
  EXPECT_FALSE(manager_->CanMount("/media/removable/disk1/test.zip"));
  EXPECT_FALSE(manager_->CanMount("/media/removable/disk1/test 1.zip"));
  EXPECT_FALSE(manager_->CanMount("/media/removable/disk1/dir1/test.zip"));
  EXPECT_FALSE(manager_->CanMount("/media/removable/test.zip/test1.zip"));
  EXPECT_FALSE(manager_->CanMount("/home/chronos/user/Downloads/test1.zip"));
  EXPECT_FALSE(manager_->CanMount("/home/chronos/user/GCache/test1.zip"));
  EXPECT_FALSE(
      manager_->CanMount("/home/chronos"
                         "/u-0123456789abcdef0123456789abcdef01234567"
                         "/Downloads/test1.zip"));
  EXPECT_FALSE(
      manager_->CanMount("/home/chronos"
                         "/u-0123456789abcdef0123456789abcdef01234567"
                         "/GCache/test1.zip"));
  EXPECT_FALSE(manager_->CanMount(""));
  EXPECT_FALSE(manager_->CanMount("/tmp"));
  EXPECT_FALSE(manager_->CanMount("/media/removable"));
  EXPECT_FALSE(manager_->CanMount("/media/removable/"));
  EXPECT_FALSE(manager_->CanMount("/media/archive"));
  EXPECT_FALSE(manager_->CanMount("/media/archive/"));
  EXPECT_FALSE(manager_->CanMount("/home/chronos/user/Downloads"));
  EXPECT_FALSE(manager_->CanMount("/home/chronos/user/Downloads/"));
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
      manager_->MaybeWrapMountPointForEject(std::move(mount_point), disk);
  EXPECT_EQ(MOUNT_ERROR_NONE, wrapped_mount_point->Unmount());

  mount_point = std::make_unique<MockMountPoint>(kMountPath);
  EXPECT_CALL(*mount_point, UnmountImpl()).WillOnce(Return(MOUNT_ERROR_NONE));
  disk.device_file = "/dev/sr0";
  disk.media_type = DEVICE_MEDIA_OPTICAL_DISC;
  EXPECT_CALL(ejector_, Eject("/dev/sr0")).WillOnce(Return(true));
  wrapped_mount_point =
      manager_->MaybeWrapMountPointForEject(std::move(mount_point), disk);
  EXPECT_EQ(MOUNT_ERROR_NONE, wrapped_mount_point->Unmount());

  mount_point = std::make_unique<MockMountPoint>(kMountPath);
  EXPECT_CALL(*mount_point, UnmountImpl()).WillOnce(Return(MOUNT_ERROR_NONE));
  disk.device_file = "/dev/sr1";
  disk.media_type = DEVICE_MEDIA_DVD;
  EXPECT_CALL(ejector_, Eject("/dev/sr1")).WillOnce(Return(true));
  wrapped_mount_point =
      manager_->MaybeWrapMountPointForEject(std::move(mount_point), disk);
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
      manager_->MaybeWrapMountPointForEject(std::move(mount_point), disk);
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
  manager_->eject_device_on_unmount_ = false;
  EXPECT_CALL(ejector_, Eject("/dev/sr0")).Times(0);
  std::unique_ptr<MountPoint> wrapped_mount_point =
      manager_->MaybeWrapMountPointForEject(std::move(mount_point), disk);
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
      manager_->MaybeWrapMountPointForEject(std::move(mount_point), disk);
  wrapped_mount_point->Release();
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, wrapped_mount_point->Unmount());
}

}  // namespace cros_disks
