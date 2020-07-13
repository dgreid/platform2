// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_mounter.h"

#include <sys/mount.h>

#include <memory>
#include <string>
#include <vector>

#include <brillo/process/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"
#include "cros-disks/sandboxed_process.h"

namespace cros_disks {

namespace {

using testing::_;
using testing::ElementsAre;
using testing::Invoke;
using testing::Return;
using testing::StartsWith;

const uid_t kMountUID = 200;
const gid_t kMountGID = 201;
const uid_t kFilesUID = 700;
const uid_t kFilesGID = 701;
const uid_t kFilesAccessGID = 1501;
const char kMountUser[] = "fuse-fuse";
const char kFUSEType[] = "fuse";
const char kMountProgram[] = "dummy";
const char kSomeSource[] = "/dev/dummy";
const char kMountDir[] = "/mnt";
const int kPasswordNeededCode = 42;

// Mock Platform implementation for testing.
class MockFUSEPlatform : public Platform {
 public:
  MockFUSEPlatform() {
    ON_CALL(*this, GetUserAndGroupId(_, _, _))
        .WillByDefault(Invoke(this, &MockFUSEPlatform::GetUserAndGroupIdImpl));
    ON_CALL(*this, GetGroupId(_, _))
        .WillByDefault(Invoke(this, &MockFUSEPlatform::GetGroupIdImpl));
    ON_CALL(*this, PathExists(_)).WillByDefault(Return(true));
    ON_CALL(*this, SetOwnership(_, _, _)).WillByDefault(Return(true));
    ON_CALL(*this, SetPermissions(_, _)).WillByDefault(Return(true));
  }

  MOCK_METHOD(bool,
              GetUserAndGroupId,
              (const std::string&, uid_t*, gid_t*),
              (const, override));
  MOCK_METHOD(bool,
              GetGroupId,
              (const std::string&, gid_t*),
              (const, override));
  MOCK_METHOD(MountErrorType,
              Mount,
              (const std::string&,
               const std::string&,
               const std::string&,
               uint64_t,
               const std::string&),
              (const, override));
  MOCK_METHOD(MountErrorType,
              Unmount,
              (const std::string&, int),
              (const, override));
  MOCK_METHOD(bool, PathExists, (const std::string&), (const, override));
  MOCK_METHOD(bool,
              RemoveEmptyDirectory,
              (const std::string&),
              (const, override));
  MOCK_METHOD(bool,
              SetOwnership,
              (const std::string&, uid_t, gid_t),
              (const, override));
  MOCK_METHOD(bool,
              GetOwnership,
              (const std::string&, uid_t*, gid_t*),
              (const, override));
  MOCK_METHOD(bool,
              SetPermissions,
              (const std::string&, mode_t),
              (const, override));

 private:
  bool GetUserAndGroupIdImpl(const std::string& user,
                             uid_t* user_id,
                             gid_t* group_id) const {
    if (user == "chronos") {
      if (user_id)
        *user_id = kFilesUID;
      if (group_id)
        *group_id = kFilesGID;
      return true;
    }
    if (user == kMountUser) {
      if (user_id)
        *user_id = kMountUID;
      if (group_id)
        *group_id = kMountGID;
      return true;
    }
    return false;
  }

  bool GetGroupIdImpl(const std::string& group, gid_t* group_id) const {
    if (group == "chronos-access") {
      if (group_id)
        *group_id = kFilesAccessGID;
      return true;
    }
    return false;
  }
};

class MockSandboxedProcess : public SandboxedProcess {
 public:
  MockSandboxedProcess() = default;
  MOCK_METHOD(pid_t,
              StartImpl,
              (base::ScopedFD, base::ScopedFD, base::ScopedFD),
              (override));
  MOCK_METHOD(int, WaitImpl, (), (override));
  MOCK_METHOD(int, WaitNonBlockingImpl, (), (override));
};

class FUSEMounterForTesting : public FUSEMounter {
 public:
  FUSEMounterForTesting(const Platform* platform,
                        brillo::ProcessReaper* process_reaper)
      : FUSEMounter({.filesystem_type = kFUSEType,
                     .mount_program = kMountProgram,
                     .mount_user = kMountUser,
                     .password_needed_code = kPasswordNeededCode,
                     .platform = platform,
                     .process_reaper = process_reaper}) {}

  MOCK_METHOD(int, InvokeMountTool, (const std::vector<std::string>&), (const));

 private:
  std::unique_ptr<SandboxedProcess> CreateSandboxedProcess() const override {
    auto mock = std::make_unique<MockSandboxedProcess>();
    auto* raw_ptr = mock.get();
    ON_CALL(*mock, StartImpl(_, _, _)).WillByDefault(Return(123));
    ON_CALL(*mock, WaitNonBlockingImpl())
        .WillByDefault(Invoke([raw_ptr, this]() {
          return InvokeMountTool(raw_ptr->arguments());
        }));
    return mock;
  }
};

}  // namespace

class FUSEMounterTest : public ::testing::Test {
 public:
  FUSEMounterTest() : mounter_(&platform_, &process_reaper_) {
    ON_CALL(platform_, Mount(kSomeSource, kMountDir, _, _, _))
        .WillByDefault(Return(MOUNT_ERROR_NONE));
  }

 protected:
  // Sets up mock expectations for a successful mount.
  void SetupMountExpectations() {
    EXPECT_CALL(mounter_, InvokeMountTool(ElementsAre(
                              kMountProgram, "-o", MountOptions().ToString(),
                              kSomeSource, StartsWith("/dev/fd/"))))
        .WillOnce(Return(0));
    EXPECT_CALL(platform_, PathExists(kMountProgram)).WillOnce(Return(true));
    EXPECT_CALL(platform_, SetOwnership(kSomeSource, getuid(), kMountGID))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, SetPermissions(kSomeSource, S_IRUSR | S_IWUSR |
                                                           S_IRGRP | S_IWGRP))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, SetOwnership(kMountDir, _, _)).Times(0);
    EXPECT_CALL(platform_, SetPermissions(kMountDir, _)).Times(0);
  }

  MockFUSEPlatform platform_;
  brillo::ProcessReaper process_reaper_;
  FUSEMounterForTesting mounter_;
};

TEST_F(FUSEMounterTest, Sandboxing_Unprivileged) {
  SetupMountExpectations();
  // The MountPoint returned by Mount() will unmount when it is destructed.
  EXPECT_CALL(platform_, Unmount(kMountDir, 0))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_TRUE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);
}

TEST_F(FUSEMounterTest, MountPoint_UnmountTwice) {
  SetupMountExpectations();
  // Even though Unmount() is called twice, the underlying unmount should only
  // be done once.
  EXPECT_CALL(platform_, Unmount(kMountDir, 0))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_TRUE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);

  EXPECT_EQ(MOUNT_ERROR_NONE, mount_point->Unmount());
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, mount_point->Unmount());
}

TEST_F(FUSEMounterTest, MountPoint_UnmountFailure) {
  SetupMountExpectations();
  // If an Unmount fails, we should be able to retry.
  EXPECT_CALL(platform_, Unmount(kMountDir, 0))
      .WillOnce(Return(MOUNT_ERROR_UNKNOWN))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_TRUE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);

  EXPECT_EQ(MOUNT_ERROR_UNKNOWN, mount_point->Unmount());
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_point->Unmount());
}

TEST_F(FUSEMounterTest, MountPoint_UnmountBusy) {
  SetupMountExpectations();
  EXPECT_CALL(platform_, Unmount(kMountDir, 0))
      .WillOnce(Return(MOUNT_ERROR_PATH_ALREADY_MOUNTED));
  EXPECT_CALL(platform_, Unmount(kMountDir, MNT_FORCE | MNT_DETACH))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_TRUE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);

  EXPECT_EQ(MOUNT_ERROR_NONE, mount_point->Unmount());
}

TEST_F(FUSEMounterTest, AppFailed) {
  EXPECT_CALL(platform_, Unmount(_, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(mounter_, InvokeMountTool(_)).WillOnce(Return(1));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_FALSE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_MOUNT_PROGRAM_FAILED, error);
}

TEST_F(FUSEMounterTest, AppNeedsPassword) {
  EXPECT_CALL(platform_, Unmount(_, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(mounter_, InvokeMountTool(_))
      .WillOnce(Return(kPasswordNeededCode));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_FALSE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NEED_PASSWORD, error);
}

}  // namespace cros_disks
