// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_mount_manager.h"

#include <sys/mount.h>

#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_util.h>
#include <brillo/process/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"
#include "cros-disks/sandboxed_process.h"
#include "cros-disks/uri.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::WithArg;

namespace cros_disks {

namespace {

const char kMountRoot[] = "/mntroot";
const char kWorkingDirRoot[] = "/wkdir";
const char kNoType[] = "";
const char kSomeMountpoint[] = "/mnt";
const Uri kSomeSource("fuse", "something");

// Mock Platform implementation for testing.
class MockPlatform : public Platform {
 public:
  MockPlatform() = default;

  MOCK_METHOD(MountErrorType,
              Mount,
              (const std::string& source,
               const std::string& target,
               const std::string& filesystem_type,
               uint64_t flags,
               const std::string& options),
              (const, override));
  MOCK_METHOD(MountErrorType,
              Unmount,
              (const std::string&, int),
              (const, override));
  MOCK_METHOD(bool, DirectoryExists, (const std::string&), (const, override));
  MOCK_METHOD(bool, CreateDirectory, (const std::string&), (const, override));
  MOCK_METHOD(bool,
              SetPermissions,
              (const std::string&, mode_t),
              (const, override));
  MOCK_METHOD(bool,
              CreateTemporaryDirInDir,
              (const std::string&, const std::string&, std::string*),
              (const, override));
};

// Mock implementation of a Mounter.
class MockMounter : public Mounter {
 public:
  MOCK_METHOD(std::unique_ptr<MountPoint>,
              Mount,
              (const std::string& source,
               const base::FilePath& target_path,
               std::vector<std::string> params,
               MountErrorType* error),
              (const, override));
  MOCK_METHOD(bool,
              CanMount,
              (const std::string& source,
               const std::vector<std::string>& params,
               base::FilePath* suggested_dir_name),
              (const, override));
};

class MockSandboxedProcess : public SandboxedProcess {
 public:
  MockSandboxedProcess() = default;
  pid_t StartImpl(base::ScopedFD, base::ScopedFD, base::ScopedFD) override {
    return 123;
  }
  MOCK_METHOD(int, WaitImpl, (), (override));
  MOCK_METHOD(int, WaitNonBlockingImpl, (), (override));
};

}  // namespace

class FUSEMountManagerTest : public ::testing::Test {
 public:
  FUSEMountManagerTest()
      : manager_(kMountRoot,
                 kWorkingDirRoot,
                 &platform_,
                 &metrics_,
                 &process_reaper_),
        foo_(new MockMounter()),
        bar_(new MockMounter()),
        baz_(new MockMounter()) {
    ON_CALL(platform_, Unmount(_, _))
        .WillByDefault(Return(MOUNT_ERROR_INVALID_ARGUMENT));
    ON_CALL(platform_, DirectoryExists(_)).WillByDefault(Return(true));
  }

 protected:
  void RegisterHelper(std::unique_ptr<Mounter> helper) {
    manager_.RegisterHelper(std::move(helper));
  }

  std::unique_ptr<MountPoint> DoMount(const std::string& type,
                                      const std::string& src,
                                      MountErrorType* error) {
    MountOptions mount_options;
    std::unique_ptr<MountPoint> mount_point = manager_.DoMount(
        src, type, {}, base::FilePath(kSomeMountpoint), &mount_options, error);
    if (*error == MOUNT_ERROR_NONE) {
      EXPECT_TRUE(mount_point);
    } else {
      EXPECT_FALSE(mount_point);
    }
    return mount_point;
  }

  Metrics metrics_;
  MockPlatform platform_;
  brillo::ProcessReaper process_reaper_;
  FUSEMountManager manager_;
  std::unique_ptr<MockMounter> foo_;
  std::unique_ptr<MockMounter> bar_;
  std::unique_ptr<MockMounter> baz_;
};

// Verifies that CanMount returns false when there are no handlers registered.
TEST_F(FUSEMountManagerTest, CanMount_NoHandlers) {
  EXPECT_FALSE(manager_.CanMount(kSomeSource.value()));
}

// Verifies that CanMount returns false when known helpers can't handle that.
TEST_F(FUSEMountManagerTest, CanMount_NotHandled) {
  EXPECT_CALL(*foo_, CanMount).WillOnce(Return(false));
  EXPECT_CALL(*bar_, CanMount).WillOnce(Return(false));
  EXPECT_CALL(*baz_, CanMount).WillOnce(Return(false));
  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  EXPECT_FALSE(manager_.CanMount(kSomeSource.value()));
}

// Verify that CanMount returns true when there is a helper that can handle
// this source.
TEST_F(FUSEMountManagerTest, CanMount) {
  EXPECT_CALL(*foo_, CanMount).WillOnce(Return(false));
  EXPECT_CALL(*bar_, CanMount).WillOnce(Return(true));
  EXPECT_CALL(*baz_, CanMount).Times(0);
  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  EXPECT_TRUE(manager_.CanMount(kSomeSource.value()));
}

// Verify that SuggestMountPath dispatches query for name to the correct helper.
TEST_F(FUSEMountManagerTest, SuggestMountPath) {
  EXPECT_CALL(*foo_, CanMount).WillOnce(Return(false));
  EXPECT_CALL(*bar_, CanMount)
      .WillOnce(
          DoAll(SetArgPointee<2>(base::FilePath("suffix")), Return(true)));
  EXPECT_CALL(*baz_, CanMount).Times(0);
  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  EXPECT_EQ("/mntroot/suffix", manager_.SuggestMountPath(kSomeSource.value()));
}

// Verify that DoMount fails when there are no helpers.
TEST_F(FUSEMountManagerTest, DoMount_NoHandlers) {
  MountErrorType mount_error;
  std::unique_ptr<MountPoint> mount_point =
      DoMount(kNoType, kSomeSource.value(), &mount_error);
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM, mount_error);
}

// Verify that DoMount fails when helpers don't handle this source.
TEST_F(FUSEMountManagerTest, DoMount_NotHandled) {
  EXPECT_CALL(*foo_, CanMount).WillOnce(Return(false));
  EXPECT_CALL(*bar_, CanMount).WillOnce(Return(false));
  EXPECT_CALL(*baz_, CanMount).WillOnce(Return(false));
  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  MountErrorType mount_error;
  std::unique_ptr<MountPoint> mount_point =
      DoMount(kNoType, kSomeSource.value(), &mount_error);
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM, mount_error);
}

// Verify that DoMount delegates mounting to the correct helpers when
// dispatching by source description.
TEST_F(FUSEMountManagerTest, DoMount_BySource) {
  EXPECT_CALL(*foo_, CanMount).WillOnce(Return(false));
  EXPECT_CALL(*bar_, CanMount)
      .WillOnce(
          DoAll(SetArgPointee<2>(base::FilePath("suffix")), Return(true)));
  EXPECT_CALL(*baz_, CanMount).Times(0);

  EXPECT_CALL(*foo_, Mount).Times(0);
  EXPECT_CALL(*baz_, Mount).Times(0);

  EXPECT_CALL(*bar_, Mount(kSomeSource.value(), _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(MOUNT_ERROR_NONE),
                      Return(ByMove(MountPoint::CreateLeaking(
                          base::FilePath(kSomeMountpoint))))));

  RegisterHelper(std::move(foo_));
  RegisterHelper(std::move(bar_));
  RegisterHelper(std::move(baz_));
  MountErrorType mount_error;
  std::unique_ptr<MountPoint> mount_point =
      DoMount(kNoType, kSomeSource.value(), &mount_error);
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_error);
  EXPECT_EQ(base::FilePath(kSomeMountpoint), mount_point->path());
}

}  // namespace cros_disks
