// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for cros_disks::MountManager. See mount-manager.h for details
// on MountManager.

#include "cros-disks/mount_manager.h"

#include <sys/mount.h>
#include <sys/unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include <brillo/process/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/metrics.h"
#include "cros-disks/mount_entry.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/mounter.h"
#include "cros-disks/platform.h"

using testing::_;
using testing::ElementsAre;
using testing::Invoke;
using testing::Return;
using testing::WithArgs;

namespace cros_disks {
namespace {

const char kMountRootDirectory[] = "/media/removable";
const char kTestSourcePath[] = "source";
const char kTestMountPath[] = "/media/removable/test";
const char kInvalidMountPath[] = "/media/removable/../test/doc";
const char kMountOptionRemount[] = "remount";
const char kMountOptionReadOnly[] = "ro";
const char kMountOptionReadWrite[] = "rw";

}  // namespace

// A mock platform class for testing the mount manager base class.
class MockPlatform : public Platform {
 public:
  MockPlatform() = default;

  MOCK_METHOD(bool, CreateDirectory, (const std::string&), (const, override));
  MOCK_METHOD(bool,
              CreateOrReuseEmptyDirectory,
              (const std::string&),
              (const, override));
  MOCK_METHOD(bool,
              CreateOrReuseEmptyDirectoryWithFallback,
              (std::string*, unsigned, const std::set<std::string>&),
              (const, override));
  MOCK_METHOD(bool,
              RemoveEmptyDirectory,
              (const std::string&),
              (const, override));
  MOCK_METHOD(bool,
              SetOwnership,
              (const std::string&, uid_t, gid_t),
              (const, override));
  MOCK_METHOD(bool,
              SetPermissions,
              (const std::string&, mode_t),
              (const, override));
};

// Fake mount point that always returns a specific error on unmount. Set up as a
// mock to expect that UnmountImpl() is always called.
class FakeMountPoint : public MountPoint {
 public:
  explicit FakeMountPoint(const base::FilePath& path, MountErrorType error)
      : MountPoint({path}) {
    if (error == MOUNT_ERROR_NONE) {
      EXPECT_CALL(*this, UnmountImpl()).WillOnce(Return(error));
    } else {
      EXPECT_CALL(*this, UnmountImpl()).WillRepeatedly(Return(error));
    }
  }
  ~FakeMountPoint() override { DestructorUnmount(); }

  MOCK_METHOD(MountErrorType, UnmountImpl, (), (override));
};

// Fake mount point that is neve unmounted (due to being released). Set up as a
// mock to expect that UnmountImpl() is never called.
class NeverUnmountedMountPoint : public MountPoint {
 public:
  explicit NeverUnmountedMountPoint(const base::FilePath& path)
      : MountPoint({path}) {
    EXPECT_CALL(*this, UnmountImpl()).Times(0);
  }
  ~NeverUnmountedMountPoint() override { DestructorUnmount(); }

  MOCK_METHOD(MountErrorType, UnmountImpl, (), (override));
};

// A mock mount manager class for testing the mount manager base class.
class MountManagerUnderTest : public MountManager {
 public:
  MountManagerUnderTest(Platform* platform,
                        Metrics* metrics,
                        brillo::ProcessReaper* process_reaper)
      : MountManager(kMountRootDirectory, platform, metrics, process_reaper) {}

  ~MountManagerUnderTest() override { UnmountAll(); }

  MOCK_METHOD(bool, CanMount, (const std::string&), (const, override));
  MOCK_METHOD(MountSourceType, GetMountSourceType, (), (const, override));
  MOCK_METHOD(std::unique_ptr<MountPoint>,
              DoMount,
              (const std::string&,
               const std::string&,
               const std::vector<std::string>&,
               const base::FilePath&,
               bool*,
               MountErrorType*),
              (override));
  MOCK_METHOD(bool,
              ShouldReserveMountPathOnError,
              (MountErrorType),
              (const, override));
  MOCK_METHOD(std::string,
              SuggestMountPath,
              (const std::string&),
              (const, override));
};

class MountManagerTest : public ::testing::Test {
 public:
  MountManagerTest() : manager_(&platform_, &metrics_, &process_reaper_) {
    ON_CALL(manager_, GetMountSourceType())
        .WillByDefault(Return(MOUNT_SOURCE_REMOVABLE_DEVICE));
  }

  std::unique_ptr<MountPoint> MakeMountPoint(const std::string& mount_path) {
    return MountPoint::CreateLeaking(base::FilePath(mount_path));
  }

 protected:
  Metrics metrics_;
  MockPlatform platform_;
  brillo::ProcessReaper process_reaper_;
  MountManagerUnderTest manager_;
  std::string filesystem_type_;
  std::string mount_path_;
  std::string source_path_;
  std::vector<std::string> options_;
};

// Mock action to emulate DoMount with fallback to read-only mode.
std::unique_ptr<MountPoint> DoMountSuccessReadOnly(
    const std::string& source_path,
    const std::string& filesystem_type,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    bool* mounted_as_read_only,
    MountErrorType* error) {
  *mounted_as_read_only = true;
  *error = MOUNT_ERROR_NONE;
  return std::make_unique<FakeMountPoint>(mount_path, MOUNT_ERROR_NONE);
}

// Mock action to emulate DoMount successfully finished.
std::unique_ptr<MountPoint> DoMountSuccess(
    const std::string& source_path,
    const std::string& filesystem_type,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    bool* mounted_as_read_only,
    MountErrorType* error) {
  *mounted_as_read_only = IsReadOnlyMount(options);
  *error = MOUNT_ERROR_NONE;
  return std::make_unique<FakeMountPoint>(mount_path, MOUNT_ERROR_NONE);
}

// Mock action to emulate DoMount successfully finished, returning a MountPoint
// that returns |unmount_error| when unmounted.
decltype(auto) DoMountSuccessWithUnmountError(MountErrorType unmount_error) {
  return [unmount_error](
             const std::string& source_path, const std::string& filesystem_type,
             const std::vector<std::string>& options,
             const base::FilePath& mount_path, bool* mounted_as_read_only,
             MountErrorType* error) -> std::unique_ptr<MountPoint> {
    *mounted_as_read_only = IsReadOnlyMount(options);
    *error = MOUNT_ERROR_NONE;
    return std::make_unique<FakeMountPoint>(mount_path, unmount_error);
  };
}

// Mock action to emulate DoMount failing with a given error code.
decltype(auto) DoMountFailure(MountErrorType mount_error) {
  return [mount_error](
             const std::string& source_path, const std::string& filesystem_type,
             const std::vector<std::string>& options,
             const base::FilePath& mount_path, bool* mounted_as_read_only,
             MountErrorType* error) -> std::unique_ptr<MountPoint> {
    *error = mount_error;
    return nullptr;
  };
}

// Verifies that MountManager::Initialize() returns false when it fails to
// create the mount root directory.
TEST_F(MountManagerTest, InitializeFailedInCreateDirectory) {
  EXPECT_CALL(platform_, CreateDirectory(kMountRootDirectory))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, SetOwnership(kMountRootDirectory, getuid(), getgid()))
      .Times(0);
  EXPECT_CALL(platform_, SetPermissions(kMountRootDirectory, _)).Times(0);

  EXPECT_FALSE(manager_.Initialize());
}

// Verifies that MountManager::Initialize() returns false when it fails to
// set the ownership of the created mount root directory.
TEST_F(MountManagerTest, InitializeFailedInSetOwnership) {
  EXPECT_CALL(platform_, CreateDirectory(kMountRootDirectory))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kMountRootDirectory, getuid(), getgid()))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, SetPermissions(kMountRootDirectory, _)).Times(0);

  EXPECT_FALSE(manager_.Initialize());
}

// Verifies that MountManager::Initialize() returns false when it fails to
// set the permissions of the created mount root directory.
TEST_F(MountManagerTest, InitializeFailedInSetPermissions) {
  EXPECT_CALL(platform_, CreateDirectory(kMountRootDirectory))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kMountRootDirectory, getuid(), getgid()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(kMountRootDirectory, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(manager_.Initialize());
}

// Verifies that MountManager::Initialize() returns true when it creates
// the mount root directory with the specified ownership and permissions.
TEST_F(MountManagerTest, InitializeSucceeded) {
  EXPECT_CALL(platform_, CreateDirectory(kMountRootDirectory))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kMountRootDirectory, getuid(), getgid()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetPermissions(kMountRootDirectory, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(manager_.Initialize());
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// to mount an empty source path.
TEST_F(MountManagerTest, MountFailedWithEmptySourcePath) {
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(
      MOUNT_ERROR_INVALID_ARGUMENT,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// with a nullptr mount path.
TEST_F(MountManagerTest, MountFailedWithNullMountPath) {
  source_path_ = kTestSourcePath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_INVALID_ARGUMENT,
            manager_.Mount(source_path_, filesystem_type_, options_, nullptr));
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// with an invalid mount path.
TEST_F(MountManagerTest, MountFailedWithInvalidMountPath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kInvalidMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(
      MOUNT_ERROR_INVALID_PATH,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// without a given mount path and the suggested mount path is invalid.
TEST_F(MountManagerTest, MountFailedWithInvalidSuggestedMountPath) {
  source_path_ = kTestSourcePath;
  std::string suggested_mount_path = kInvalidMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_))
      .WillRepeatedly(Return(suggested_mount_path));

  EXPECT_EQ(
      MOUNT_ERROR_INVALID_PATH,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));

  options_.push_back("mountlabel=custom_label");
  EXPECT_EQ(
      MOUNT_ERROR_INVALID_PATH,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it is invoked
// with an mount label that yields an invalid mount path.
TEST_F(MountManagerTest, MountFailedWithInvalidMountLabel) {
  source_path_ = kTestSourcePath;
  std::string suggested_mount_path = kTestSourcePath;
  options_.push_back("mountlabel=../custom_label");

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(
      MOUNT_ERROR_INVALID_PATH,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it fails to
// create the specified mount directory.
TEST_F(MountManagerTest, MountFailedInCreateOrReuseEmptyDirectory) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(
      MOUNT_ERROR_DIRECTORY_CREATION_FAILED,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it fails to
// create a specified but already reserved mount directory.
TEST_F(MountManagerTest, MountFailedInCreateDirectoryDueToReservedMountPath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  manager_.ReserveMountPath(mount_path_, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  EXPECT_EQ(
      MOUNT_ERROR_DIRECTORY_CREATION_FAILED,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
}

// Verifies that MountManager::Mount() returns an error when it fails to
// create a mount directory after a number of trials.
TEST_F(MountManagerTest, MountFailedInCreateOrReuseEmptyDirectoryWithFallback) {
  source_path_ = kTestSourcePath;
  std::string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(
      MOUNT_ERROR_DIRECTORY_CREATION_FAILED,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ("", mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(suggested_mount_path));
}

// Verifies that MountManager::Mount() returns no error when it successfully
// mounts a source path to a specified mount path in read-write mode.
TEST_F(MountManagerTest, MountSucceededWithGivenMountPath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  options_.push_back("rw");
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(Invoke(DoMountSuccess));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  base::Optional<MountEntry> mount_entry;
  mount_entry = manager_.GetMountEntryForTest(source_path_);
  EXPECT_TRUE(mount_entry);
  EXPECT_FALSE(mount_entry->is_read_only);

  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() stores correct mount status in cache when
// read-only option is specified.
TEST_F(MountManagerTest, MountCachesStatusWithReadOnlyOption) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  // Add read-only mount option.
  options_.push_back("ro");
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(Invoke(DoMountSuccess));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  base::Optional<MountEntry> mount_entry;
  mount_entry = manager_.GetMountEntryForTest(source_path_);
  EXPECT_TRUE(mount_entry);
  EXPECT_TRUE(mount_entry->is_read_only);
}

// Verifies that MountManager::Mount() stores correct mount status in cache when
// the mounter requested to mount in read-write mode but fell back to read-only
// mode.
TEST_F(MountManagerTest, MountSuccededWithReadOnlyFallback) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  options_.push_back("rw");
  // Emulate Mounter added read-only option as a fallback.
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(Invoke(DoMountSuccessReadOnly));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  base::Optional<MountEntry> mount_entry;
  mount_entry = manager_.GetMountEntryForTest(source_path_);
  EXPECT_TRUE(mount_entry);
  EXPECT_TRUE(mount_entry->is_read_only);
}

// Verifies that MountManager::Mount() returns no error when it successfully
// mounts a source path with no mount path specified.
TEST_F(MountManagerTest, MountSucceededWithEmptyMountPath) {
  source_path_ = kTestSourcePath;
  std::string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(suggested_mount_path), _, _))
      .WillOnce(Invoke(DoMountSuccess));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() returns no error when it successfully
// mounts a source path with a given mount label in options.
TEST_F(MountManagerTest, MountSucceededWithGivenMountLabel) {
  source_path_ = kTestSourcePath;
  std::string suggested_mount_path = kTestMountPath;
  std::string final_mount_path =
      std::string(kMountRootDirectory) + "/custom_label";
  options_.push_back("mountlabel=custom_label");
  std::vector<std::string> updated_options;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(final_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, updated_options,
                                base::FilePath(final_mount_path), _, _))
      .WillOnce(Invoke(DoMountSuccess));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(final_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() handles the mounting of an already
// mounted source path properly.
TEST_F(MountManagerTest, MountWithAlreadyMountedSourcePath) {
  source_path_ = kTestSourcePath;
  std::string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(suggested_mount_path), _, _))
      .WillOnce(Invoke(DoMountSuccess));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  // Mount an already-mounted source path without specifying a mount path
  mount_path_.clear();
  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  // Mount an already-mounted source path to the same mount path
  mount_path_ = suggested_mount_path;
  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  // Mount an already-mounted source path to a different mount path
  mount_path_ = "another-path";
  EXPECT_EQ(
      MOUNT_ERROR_PATH_ALREADY_MOUNTED,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathInCache(suggested_mount_path));

  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(suggested_mount_path));
}

// Verifies that MountManager::Mount() successfully reserves a path for a given
// type of error. A specific mount path is given in this case.
TEST_F(MountManagerTest, MountSucceededWithGivenMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(DoMountFailure(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(
      MOUNT_ERROR_UNKNOWN_FILESYSTEM,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() successfully reserves a path for a given
// type of error. No specific mount path is given in this case.
TEST_F(MountManagerTest, MountSucceededWithEmptyMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  std::string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(suggested_mount_path), _, _))
      .WillOnce(DoMountFailure(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(
      MOUNT_ERROR_UNKNOWN_FILESYSTEM,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() successfully reserves a path for a given
// type of error and returns the same error when it tries to mount the same path
// again.
TEST_F(MountManagerTest, MountSucceededWithAlreadyReservedMountPath) {
  source_path_ = kTestSourcePath;
  std::string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(suggested_mount_path), _, _))
      .WillOnce(DoMountFailure(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(
      MOUNT_ERROR_UNKNOWN_FILESYSTEM,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));

  mount_path_ = "";
  EXPECT_EQ(
      MOUNT_ERROR_UNKNOWN_FILESYSTEM,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ(suggested_mount_path, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));

  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() successfully reserves a path for a given
// type of error and returns the same error when it tries to mount the same path
// again.
TEST_F(MountManagerTest, MountFailedWithGivenMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(DoMountFailure(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(false));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(
      MOUNT_ERROR_UNKNOWN_FILESYSTEM,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Mount() fails to mount or reserve a path for
// a type of error that is not enabled for reservation.
TEST_F(MountManagerTest, MountFailedWithEmptyMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  std::string suggested_mount_path = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, RemoveEmptyDirectory(suggested_mount_path))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(suggested_mount_path), _, _))
      .WillOnce(DoMountFailure(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(false));
  EXPECT_CALL(manager_, SuggestMountPath(source_path_))
      .WillOnce(Return(suggested_mount_path));

  EXPECT_EQ(
      MOUNT_ERROR_UNKNOWN_FILESYSTEM,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ("", mount_path_);
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Unmount() returns an error when it is invoked
// to unmount an empty path.
TEST_F(MountManagerTest, UnmountFailedWithEmptyPath) {
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, manager_.Unmount(mount_path_));
}

// Verifies that MountManager::Unmount() returns an error when it fails to
// unmount a path that is not mounted.
TEST_F(MountManagerTest, UnmountFailedWithPathNotMounted) {
  mount_path_ = "nonexistent-path";

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(_)).Times(0);
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(_)).Times(0);
  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, manager_.Unmount(mount_path_));
}

// Verifies that MountManager::Unmount() returns no error when it successfully
// unmounts a source path.
TEST_F(MountManagerTest, UnmountSucceededWithGivenSourcePath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(Invoke(DoMountSuccess));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Unmount(source_path_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::Unmount() returns no error when it successfully
// unmounts a mount path.
TEST_F(MountManagerTest, UnmountSucceededWithGivenMountPath) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(Invoke(DoMountSuccess));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Unmount(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::Unmount() removes mount path from cache if
// it appears to be not mounted.
TEST_F(MountManagerTest, UnmountRemovesFromCacheIfNotMounted) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(DoMountSuccessWithUnmountError(MOUNT_ERROR_PATH_NOT_MOUNTED));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(source_path_, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, manager_.Unmount(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::Unmount() returns no error when it is invoked
// to unmount the source path of a reserved mount path.
TEST_F(MountManagerTest, UnmountSucceededWithGivenSourcePathInReservedCase) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(DoMountFailure(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(
      MOUNT_ERROR_UNKNOWN_FILESYSTEM,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Unmount(source_path_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::Unmount() returns no error when it is invoked
// to unmount a reserved mount path.
TEST_F(MountManagerTest, UnmountSucceededWithGivenMountPathInReservedCase) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .Times(0);
  EXPECT_CALL(platform_, RemoveEmptyDirectory(mount_path_))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, DoMount(source_path_, filesystem_type_, options_,
                                base::FilePath(mount_path_), _, _))
      .WillOnce(DoMountFailure(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(manager_,
              ShouldReserveMountPathOnError(MOUNT_ERROR_UNKNOWN_FILESYSTEM))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, SuggestMountPath(_)).Times(0);

  EXPECT_EQ(
      MOUNT_ERROR_UNKNOWN_FILESYSTEM,
      manager_.Mount(source_path_, filesystem_type_, options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));

  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Unmount(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
}

// Verifies that MountManager::AddOrUpdateMountStateCache() works as expected.
TEST_F(MountManagerTest, AddOrUpdateMountStateCache) {
  std::string result;
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;
  std::string mount_path_2 = "target2";
  bool is_read_only = true;

  manager_.AddOrUpdateMountStateCache(source_path_, MakeMountPoint(mount_path_),
                                      is_read_only);
  EXPECT_TRUE(manager_.GetMountPathFromCache(source_path_, &result));
  EXPECT_EQ(mount_path_, result);
  base::Optional<MountEntry> mount_entry;
  mount_entry = manager_.GetMountEntryForTest(source_path_);
  EXPECT_TRUE(mount_entry);
  EXPECT_EQ(kTestMountPath, mount_entry->mount_path);
  EXPECT_EQ(is_read_only, mount_entry->is_read_only);

  manager_.AddOrUpdateMountStateCache(source_path_,
                                      MakeMountPoint(mount_path_2), false);
  mount_entry = manager_.GetMountEntryForTest(source_path_);
  EXPECT_TRUE(mount_entry);
  EXPECT_EQ(mount_path_2, mount_entry->mount_path);
  EXPECT_FALSE(mount_entry->is_read_only);

  EXPECT_FALSE(manager_.RemoveMountPathFromCache(mount_path_));
  EXPECT_TRUE(manager_.RemoveMountPathFromCache(mount_path_2));
}

// Verifies that MountManager::IsMountPathInCache() works as expected.
TEST_F(MountManagerTest, IsMountPathInCache) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
  manager_.AddOrUpdateMountStateCache(source_path_, MakeMountPoint(mount_path_),
                                      false);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));
  EXPECT_TRUE(manager_.RemoveMountPathFromCache(mount_path_));
  EXPECT_FALSE(manager_.IsMountPathInCache(mount_path_));
}

// Verifies that MountManager::RemoveMountPathFromCache() works as expected.
TEST_F(MountManagerTest, RemoveMountPathFromCache) {
  source_path_ = kTestSourcePath;
  mount_path_ = kTestMountPath;

  EXPECT_FALSE(manager_.RemoveMountPathFromCache(mount_path_));
  manager_.AddOrUpdateMountStateCache(source_path_, MakeMountPoint(mount_path_),
                                      false);
  EXPECT_TRUE(manager_.RemoveMountPathFromCache(mount_path_));
  EXPECT_FALSE(manager_.RemoveMountPathFromCache(mount_path_));
}

// Verifies that MountManager::GetReservedMountPaths() works as expected.
TEST_F(MountManagerTest, GetReservedMountPaths) {
  std::set<std::string> reserved_paths;
  std::set<std::string> expected_paths;
  std::string path1 = "path1";
  std::string path2 = "path2";

  reserved_paths = manager_.GetReservedMountPaths();
  EXPECT_TRUE(expected_paths == reserved_paths);

  manager_.ReserveMountPath(path1, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  reserved_paths = manager_.GetReservedMountPaths();
  expected_paths.insert(path1);
  EXPECT_TRUE(expected_paths == reserved_paths);

  manager_.ReserveMountPath(path2, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  reserved_paths = manager_.GetReservedMountPaths();
  expected_paths.insert(path2);
  EXPECT_TRUE(expected_paths == reserved_paths);

  manager_.UnreserveMountPath(path1);
  reserved_paths = manager_.GetReservedMountPaths();
  expected_paths.erase(path1);
  EXPECT_TRUE(expected_paths == reserved_paths);

  manager_.UnreserveMountPath(path2);
  reserved_paths = manager_.GetReservedMountPaths();
  expected_paths.erase(path2);
  EXPECT_TRUE(expected_paths == reserved_paths);
}

// Verifies that MountManager::ReserveMountPath() and
// MountManager::UnreserveMountPath() work as expected.
TEST_F(MountManagerTest, ReserveAndUnreserveMountPath) {
  mount_path_ = kTestMountPath;

  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  manager_.ReserveMountPath(mount_path_, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  manager_.UnreserveMountPath(mount_path_);
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));

  // Removing a nonexistent mount path should be ok
  manager_.UnreserveMountPath(mount_path_);
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));

  // Adding an existent mount path should be ok
  manager_.ReserveMountPath(mount_path_, MOUNT_ERROR_UNSUPPORTED_FILESYSTEM);
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNSUPPORTED_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  manager_.ReserveMountPath(mount_path_, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  EXPECT_TRUE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_UNSUPPORTED_FILESYSTEM,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
  manager_.UnreserveMountPath(mount_path_);
  EXPECT_FALSE(manager_.IsMountPathReserved(mount_path_));
  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.GetMountErrorOfReservedMountPath(mount_path_));
}

// Verifies that MountManager::GetMountEntries() returns the expected list of
// mount entries under different scenarios.
TEST_F(MountManagerTest, GetMountEntries) {
  // No mount entry is returned.
  std::vector<MountEntry> mount_entries = manager_.GetMountEntries();
  EXPECT_TRUE(mount_entries.empty());

  // A normal mount entry is returned.
  manager_.AddOrUpdateMountStateCache(kTestSourcePath,
                                      MakeMountPoint(kTestMountPath), false);
  mount_entries = manager_.GetMountEntries();
  ASSERT_EQ(1, mount_entries.size());
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_entries[0].error_type);
  EXPECT_EQ(kTestSourcePath, mount_entries[0].source_path);
  EXPECT_EQ(MOUNT_SOURCE_REMOVABLE_DEVICE, mount_entries[0].source_type);
  EXPECT_EQ(kTestMountPath, mount_entries[0].mount_path);

  // A reserved mount entry is returned.
  manager_.ReserveMountPath(kTestMountPath, MOUNT_ERROR_UNKNOWN_FILESYSTEM);
  mount_entries = manager_.GetMountEntries();
  ASSERT_EQ(1, mount_entries.size());
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM, mount_entries[0].error_type);
  EXPECT_EQ(kTestSourcePath, mount_entries[0].source_path);
  EXPECT_EQ(MOUNT_SOURCE_REMOVABLE_DEVICE, mount_entries[0].source_type);
  EXPECT_EQ(kTestMountPath, mount_entries[0].mount_path);
}

// Verifies that MountManager::ExtractMountLabelFromOptions() extracts a mount
// label from the given options and returns true.
TEST_F(MountManagerTest, ExtractMountLabelFromOptions) {
  std::vector<std::string> options = {"ro", "mountlabel=My USB Drive",
                                      "noexec"};
  std::string mount_label;

  EXPECT_TRUE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre("ro", "noexec"));
  EXPECT_EQ("My USB Drive", mount_label);
}

// Verifies that MountManager::ExtractMountLabelFromOptions() returns false
// when no mount label is found in the given options.
TEST_F(MountManagerTest, ExtractMountLabelFromOptionsWithNoMountLabel) {
  std::vector<std::string> options;
  std::string mount_label;

  EXPECT_FALSE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre());
  EXPECT_EQ("", mount_label);

  options.push_back("ro");
  EXPECT_FALSE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre("ro"));
  EXPECT_EQ("", mount_label);

  options.push_back("mountlabel");
  EXPECT_FALSE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre("ro", "mountlabel"));
  EXPECT_EQ("", mount_label);
}

// Verifies that MountManager::ExtractMountLabelFromOptions() extracts the last
// mount label from the given options with two mount labels.
TEST_F(MountManagerTest, ExtractMountLabelFromOptionsWithTwoMountLabels) {
  std::vector<std::string> options = {"ro", "mountlabel=My USB Drive", "noexec",
                                      "mountlabel=Another Label"};
  std::string mount_label;

  EXPECT_TRUE(manager_.ExtractMountLabelFromOptions(&options, &mount_label));
  EXPECT_THAT(options, ElementsAre("ro", "noexec"));
  EXPECT_EQ("Another Label", mount_label);
}

// Verifies that MountManager::IsPathImmediateChildOfParent() correctly
// determines if a path is an immediate child of another path.
TEST_F(MountManagerTest, IsPathImmediateChildOfParent) {
  EXPECT_TRUE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/archive/test.zip"),
      base::FilePath("/media/archive")));
  EXPECT_TRUE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/archive/test.zip/"),
      base::FilePath("/media/archive")));
  EXPECT_TRUE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/archive/test.zip"),
      base::FilePath("/media/archive/")));
  EXPECT_TRUE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/archive/test.zip/"),
      base::FilePath("/media/archive/")));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/archive/test.zip/doc.zip"),
      base::FilePath("/media/archive/")));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/archive/test.zip"),
      base::FilePath("/media/removable")));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/tmp/archive/test.zip"),
      base::FilePath("/media/removable")));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media"), base::FilePath("/media/removable")));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/removable"), base::FilePath("/media/removable")));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/removable/"), base::FilePath("/media/removable")));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/removable/."),
      base::FilePath("/media/removable")));
  EXPECT_FALSE(manager_.IsPathImmediateChildOfParent(
      base::FilePath("/media/removable/.."),
      base::FilePath("/media/removable")));
}

// Verifies that MountManager::IsValidMountPath() correctly determines if a
// mount path is an immediate child of the mount root.
TEST_F(MountManagerTest, IsValidMountPath) {
  EXPECT_TRUE(
      manager_.IsValidMountPath(base::FilePath("/media/removable/test")));
  EXPECT_TRUE(
      manager_.IsValidMountPath(base::FilePath("/media/removable/test/")));
  EXPECT_TRUE(
      manager_.IsValidMountPath(base::FilePath("/media/removable/test/")));
  EXPECT_TRUE(
      manager_.IsValidMountPath(base::FilePath("/media/removable//test")));
  EXPECT_FALSE(
      manager_.IsValidMountPath(base::FilePath("/media/archive/test")));
  EXPECT_FALSE(manager_.IsValidMountPath(base::FilePath("/media/removable")));
  EXPECT_FALSE(manager_.IsValidMountPath(base::FilePath("/media/removable/")));
  EXPECT_FALSE(manager_.IsValidMountPath(base::FilePath("/media/removable/.")));
  EXPECT_FALSE(
      manager_.IsValidMountPath(base::FilePath("/media/removable/..")));
  EXPECT_FALSE(
      manager_.IsValidMountPath(base::FilePath("/media/removable/test/doc")));
  EXPECT_FALSE(
      manager_.IsValidMountPath(base::FilePath("/media/removable/../test")));
  EXPECT_FALSE(
      manager_.IsValidMountPath(base::FilePath("/media/removable/../test/")));
  EXPECT_FALSE(
      manager_.IsValidMountPath(base::FilePath("/media/removable/test/..")));
  EXPECT_FALSE(
      manager_.IsValidMountPath(base::FilePath("/media/removable/test/../")));
}

// Verifies that MountManager::Mount() returns an error when the source is
// not mounted yet but attempted to remount it.
TEST_F(MountManagerTest, RemountFailedNotMounted) {
  options_.push_back(kMountOptionRemount);

  EXPECT_CALL(manager_, DoMount(_, _, _, _, _, _)).Times(0);

  // source_path = kTestSourcePath has not been mounted yet.
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED,
            manager_.Mount(kTestSourcePath, filesystem_type_, options_,
                           &mount_path_));
}

// Verifies that MountManager::Mount() returns no error when it successfully
// remounts a source path on a specified mount path.
TEST_F(MountManagerTest, RemountSucceededWithGivenSourcePath) {
  // Mount a device in read-write mode.
  EXPECT_CALL(manager_, SuggestMountPath(_)).WillOnce(Return(kTestMountPath));
  EXPECT_CALL(platform_, CreateOrReuseEmptyDirectoryWithFallback(_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_,
              DoMount(kTestSourcePath, filesystem_type_,
                      std::vector<std::string>({kMountOptionReadWrite}),
                      base::FilePath(kTestMountPath), _, _))
      .WillOnce(WithArgs<5>([](MountErrorType* error) {
        *error = MOUNT_ERROR_NONE;
        return std::make_unique<NeverUnmountedMountPoint>(
            base::FilePath(kTestMountPath));
      }));
  mount_path_ = "";
  EXPECT_EQ(MOUNT_ERROR_NONE,
            manager_.Mount(kTestSourcePath, filesystem_type_,
                           {kMountOptionReadWrite}, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  base::Optional<MountEntry> mount_entry;
  mount_entry = manager_.GetMountEntryForTest(kTestSourcePath);
  ASSERT_TRUE(mount_entry);
  EXPECT_FALSE(mount_entry->is_read_only);
  EXPECT_EQ(kTestMountPath, mount_entry->mount_path);

  // Remount with read-only mount option.
  options_.push_back(kMountOptionRemount);
  options_.push_back(kMountOptionReadOnly);
  std::vector<std::string> expected_options = options_;
  EXPECT_CALL(manager_,
              DoMount(kTestSourcePath, filesystem_type_, expected_options,
                      base::FilePath(kTestMountPath), _, _))
      .WillOnce(Invoke(DoMountSuccessReadOnly));
  mount_path_ = "";
  EXPECT_EQ(MOUNT_ERROR_NONE, manager_.Mount(kTestSourcePath, filesystem_type_,
                                             options_, &mount_path_));
  EXPECT_EQ(kTestMountPath, mount_path_);
  EXPECT_TRUE(manager_.IsMountPathInCache(mount_path_));

  mount_entry = manager_.GetMountEntryForTest(kTestSourcePath);
  EXPECT_TRUE(mount_entry);
  EXPECT_TRUE(mount_entry->is_read_only);

  // Should be unmounted correctly even after remount.
  EXPECT_TRUE(manager_.UnmountAll());
  EXPECT_FALSE(manager_.IsMountPathInCache(kTestMountPath));
  EXPECT_FALSE(manager_.IsMountPathReserved(kTestMountPath));
}

}  // namespace cros_disks
