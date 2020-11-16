// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_mounter.h"

#include <sys/mount.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
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
using testing::ByMove;
using testing::DoAll;
using testing::ElementsAre;
using testing::EndsWith;
using testing::Invoke;
using testing::IsEmpty;
using testing::Return;
using testing::SetArgPointee;
using testing::StartsWith;

const uid_t kMountUID = 200;
const gid_t kMountGID = 201;
const char kMountUser[] = "fuse-fuse";
const char kFUSEType[] = "fuse";
const char kMountProgram[] = "/bin/dummy";
const char kSomeSource[] = "/dev/dummy";
const char kMountDir[] = "/mnt";
const int kPasswordNeededCode = 42;

// Mock Platform implementation for testing.
class MockFUSEPlatform : public Platform {
 public:
  MockFUSEPlatform() {
    ON_CALL(*this, GetUserAndGroupId(_, _, _))
        .WillByDefault(Invoke(this, &MockFUSEPlatform::GetUserAndGroupIdImpl));
    ON_CALL(*this, PathExists(_)).WillByDefault(Return(true));
    ON_CALL(*this, SetOwnership(_, _, _)).WillByDefault(Return(true));
    ON_CALL(*this, SetPermissions(_, _)).WillByDefault(Return(true));
  }

  MOCK_METHOD(bool,
              GetUserAndGroupId,
              (const std::string&, uid_t*, gid_t*),
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
    if (user == kMountUser) {
      if (user_id)
        *user_id = kMountUID;
      if (group_id)
        *group_id = kMountGID;
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
      : FUSEMounter(
            platform, process_reaper, kFUSEType, true /* nosymfollow */) {}

  MOCK_METHOD(std::unique_ptr<SandboxedProcess>,
              PrepareSandbox,
              (const std::string& source,
               const base::FilePath& target_path,
               std::vector<std::string> params,
               MountErrorType* error),
              (const override));

  bool CanMount(const std::string& source,
                const std::vector<std::string>& params,
                base::FilePath* suggested_dir_name) const override {
    NOTREACHED();
    return true;
  }
};

}  // namespace

class FUSESandboxedProcessFactoryTest : public ::testing::Test {
 public:
  FUSESandboxedProcessFactoryTest() {}

 protected:
  static bool ApplyConfiguration(const FUSESandboxedProcessFactory& factory,
                                 SandboxedProcess* sandbox) {
    return factory.ConfigureSandbox(sandbox);
  }

  MockFUSEPlatform platform_;
  const base::FilePath exe_{"/bin/exe"};
  const OwnerUser run_as_{123, 456};
};

TEST_F(FUSESandboxedProcessFactoryTest, BasicSetup) {
  EXPECT_CALL(platform_, PathExists(exe_.value())).WillOnce(Return(true));
  FUSESandboxedProcessFactory factory(&platform_, {exe_}, run_as_);
  MockSandboxedProcess sandbox_;
  EXPECT_TRUE(ApplyConfiguration(factory, &sandbox_));
}

TEST_F(FUSESandboxedProcessFactoryTest, BasicSetup_MissingExecutable) {
  EXPECT_CALL(platform_, PathExists(exe_.value())).WillOnce(Return(false));
  FUSESandboxedProcessFactory factory(&platform_, {exe_}, run_as_);
  MockSandboxedProcess sandbox_;
  EXPECT_FALSE(ApplyConfiguration(factory, &sandbox_));
}

// TODO(crbug.com/1149685): Disabled as seccomp crashes qemu used for ARM.
TEST_F(FUSESandboxedProcessFactoryTest, DISABLED_SeccompPolicy) {
  base::ScopedTempDir tmp;
  ASSERT_TRUE(tmp.CreateUniqueTempDir());
  base::FilePath seccomp = tmp.GetPath().Append("exe.policy");
  std::string policy = "close: 1\n";
  base::WriteFile(seccomp, policy.c_str(), policy.length());
  EXPECT_CALL(platform_, PathExists(seccomp.value())).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists(exe_.value())).WillOnce(Return(true));
  FUSESandboxedProcessFactory factory(&platform_, {exe_, seccomp}, run_as_);
  MockSandboxedProcess sandbox_;
  EXPECT_TRUE(ApplyConfiguration(factory, &sandbox_));
}

TEST_F(FUSESandboxedProcessFactoryTest, SeccompPolicy_MissingPolicy) {
  base::ScopedTempDir tmp;
  ASSERT_TRUE(tmp.CreateUniqueTempDir());
  base::FilePath seccomp = tmp.GetPath().Append("exe.policy");
  EXPECT_CALL(platform_, PathExists(seccomp.value())).WillOnce(Return(false));
  FUSESandboxedProcessFactory factory(&platform_, {exe_, seccomp}, run_as_);
  MockSandboxedProcess sandbox_;
  EXPECT_FALSE(ApplyConfiguration(factory, &sandbox_));
}

TEST_F(FUSESandboxedProcessFactoryTest, NetworkEnabled_NonCrostini) {
  EXPECT_CALL(platform_, PathExists(exe_.value())).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("/etc/hosts.d")).WillOnce(Return(false));
  FUSESandboxedProcessFactory factory(&platform_, {exe_}, run_as_, true);
  MockSandboxedProcess sandbox_;
  EXPECT_TRUE(ApplyConfiguration(factory, &sandbox_));
}

TEST_F(FUSESandboxedProcessFactoryTest, NetworkEnabled_Crostini) {
  EXPECT_CALL(platform_, PathExists(exe_.value())).WillOnce(Return(true));
  EXPECT_CALL(platform_, PathExists("/etc/hosts.d")).WillOnce(Return(true));
  FUSESandboxedProcessFactory factory(&platform_, {exe_}, run_as_, true);
  MockSandboxedProcess sandbox_;
  EXPECT_TRUE(ApplyConfiguration(factory, &sandbox_));
}

TEST_F(FUSESandboxedProcessFactoryTest, SupplementaryGroups) {
  FUSESandboxedProcessFactory factory(&platform_, {exe_}, run_as_, false,
                                      {11, 22, 33});
  MockSandboxedProcess sandbox_;
  EXPECT_TRUE(ApplyConfiguration(factory, &sandbox_));
}

TEST_F(FUSESandboxedProcessFactoryTest, MountNamespace) {
  base::FilePath mount_ns(base::StringPrintf("/proc/%d/ns/mnt", getpid()));
  FUSESandboxedProcessFactory factory(&platform_, {exe_}, run_as_, false, {},
                                      mount_ns);
  MockSandboxedProcess sandbox_;
  EXPECT_TRUE(ApplyConfiguration(factory, &sandbox_));
}

class FUSEMounterTest : public ::testing::Test {
 public:
  FUSEMounterTest() : mounter_(&platform_, &process_reaper_) {}

 protected:
  MockFUSEPlatform platform_;
  brillo::ProcessReaper process_reaper_;
  FUSEMounterForTesting mounter_;
};

TEST_F(FUSEMounterTest, MountingUnprivileged) {
  EXPECT_CALL(platform_,
              Mount("fuse.fuse:source", kMountDir, "fuse.fuse",
                    MountOptions::kMountFlags | MS_DIRSYNC | MS_NOSYMFOLLOW,
                    EndsWith(",user_id=1000,group_id=1001,allow_other,default_"
                             "permissions,rootmode=40000")))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  auto process_ptr = std::make_unique<MockSandboxedProcess>();
  EXPECT_CALL(*process_ptr, StartImpl).WillOnce(Return(123));
  EXPECT_CALL(mounter_, PrepareSandbox("source", base::FilePath(kMountDir),
                                       ElementsAre("arg1", "arg2", "arg3"), _))
      .WillOnce(Return(ByMove(std::move(process_ptr))));
  // The MountPoint returned by Mount() will unmount when it is destructed.
  EXPECT_CALL(platform_, Unmount(kMountDir, 0))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point = mounter_.Mount("source", base::FilePath(kMountDir),
                                    {"arg1", "arg2", "arg3"}, &error);
  EXPECT_TRUE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);
}

TEST_F(FUSEMounterTest, MountingUnprivileged_ReadOnly) {
  EXPECT_CALL(platform_, Mount(_, kMountDir, _,
                               MountOptions::kMountFlags | MS_DIRSYNC |
                                   MS_NOSYMFOLLOW | MS_RDONLY,
                               _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  auto process_ptr = std::make_unique<MockSandboxedProcess>();
  EXPECT_CALL(*process_ptr, StartImpl).WillOnce(Return(123));
  EXPECT_CALL(mounter_, PrepareSandbox(kSomeSource, base::FilePath(kMountDir),
                                       ElementsAre("arg1", "arg2", "ro"), _))
      .WillOnce(Return(ByMove(std::move(process_ptr))));
  // The MountPoint returned by Mount() will unmount when it is destructed.
  EXPECT_CALL(platform_, Unmount(kMountDir, 0))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point = mounter_.Mount(kSomeSource, base::FilePath(kMountDir),
                                    {"arg1", "arg2", "ro"}, &error);
  EXPECT_TRUE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);
}

TEST_F(FUSEMounterTest, MountingUnprivileged_MountFailed) {
  EXPECT_CALL(platform_, Mount(_, kMountDir, _, _, _))
      .WillOnce(Return(MOUNT_ERROR_UNKNOWN_FILESYSTEM));
  EXPECT_CALL(mounter_, PrepareSandbox).Times(0);
  EXPECT_CALL(platform_, Unmount(kMountDir, _)).Times(0);

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_FALSE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_UNKNOWN_FILESYSTEM, error);
}

TEST_F(FUSEMounterTest, MountingUnprivileged_SandboxFailed) {
  EXPECT_CALL(platform_, Mount(_, kMountDir, _, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(mounter_, PrepareSandbox)
      .WillOnce(DoAll(SetArgPointee<3>(MOUNT_ERROR_INVALID_MOUNT_OPTIONS),
                      Return(ByMove(nullptr))));
  EXPECT_CALL(platform_, Unmount(kMountDir, MNT_FORCE | MNT_DETACH))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_FALSE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_INVALID_MOUNT_OPTIONS, error);
}

TEST_F(FUSEMounterTest, MountingUnprivileged_AppFailed) {
  EXPECT_CALL(platform_, Mount(_, kMountDir, _, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  auto process_ptr = std::make_unique<MockSandboxedProcess>();
  EXPECT_CALL(*process_ptr, StartImpl).WillOnce(Return(123));
  EXPECT_CALL(*process_ptr, WaitNonBlockingImpl).WillOnce(Return(1));
  EXPECT_CALL(mounter_, PrepareSandbox(_, base::FilePath(kMountDir), _, _))
      .WillOnce(Return(ByMove(std::move(process_ptr))));
  EXPECT_CALL(platform_, Unmount(kMountDir, MNT_FORCE | MNT_DETACH))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_FALSE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_MOUNT_PROGRAM_FAILED, error);
}

TEST_F(FUSEMounterTest, MountPoint_UnmountTwice) {
  EXPECT_CALL(platform_, Mount(_, kMountDir, _, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  auto process_ptr = std::make_unique<MockSandboxedProcess>();
  EXPECT_CALL(*process_ptr, StartImpl).WillOnce(Return(123));
  EXPECT_CALL(mounter_, PrepareSandbox(_, base::FilePath(kMountDir), _, _))
      .WillOnce(Return(ByMove(std::move(process_ptr))));
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
  EXPECT_CALL(platform_, Mount(_, kMountDir, _, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  auto process_ptr = std::make_unique<MockSandboxedProcess>();
  EXPECT_CALL(*process_ptr, StartImpl).WillOnce(Return(123));
  EXPECT_CALL(mounter_, PrepareSandbox(_, base::FilePath(kMountDir), _, _))
      .WillOnce(Return(ByMove(std::move(process_ptr))));
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
  EXPECT_CALL(platform_, Mount(_, kMountDir, _, _, _))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  auto process_ptr = std::make_unique<MockSandboxedProcess>();
  EXPECT_CALL(*process_ptr, StartImpl).WillOnce(Return(123));
  EXPECT_CALL(mounter_, PrepareSandbox(_, base::FilePath(kMountDir), _, _))
      .WillOnce(Return(ByMove(std::move(process_ptr))));
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

namespace {

class FUSEMounterLegacyForTesting : public FUSEMounterLegacy {
 public:
  FUSEMounterLegacyForTesting(const Platform* platform,
                              brillo::ProcessReaper* process_reaper)
      : FUSEMounterLegacy({.filesystem_type = kFUSEType,
                           .mount_program = kMountProgram,
                           .mount_user = kMountUser,
                           .password_needed_codes = {kPasswordNeededCode},
                           .platform = platform,
                           .process_reaper = process_reaper}) {}

  MOCK_METHOD(int, OnInput, (const std::string&), (const));
  MOCK_METHOD(int, InvokeMountTool, (const std::vector<std::string>&), (const));

  mutable std::vector<std::string> environment;

 private:
  std::unique_ptr<SandboxedProcess> CreateSandboxedProcess() const override {
    auto mock = std::make_unique<MockSandboxedProcess>();
    const SandboxedProcess* const process = mock.get();
    mock->AddArgument(kMountProgram);
    ON_CALL(*mock, StartImpl(_, _, _)).WillByDefault(Return(123));
    ON_CALL(*mock, WaitNonBlockingImpl())
        .WillByDefault(Invoke([this, process]() {
          const std::string& input = process->input();
          if (!input.empty())
            OnInput(input);

          return InvokeMountTool(process->arguments());
        }));
    return mock;
  }
};

}  // namespace

class FUSEMounterLegacyTest : public ::testing::Test {
 public:
  FUSEMounterLegacyTest() : mounter_(&platform_, &process_reaper_) {
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
  FUSEMounterLegacyForTesting mounter_;
};

TEST_F(FUSEMounterLegacyTest, AppNeedsPassword) {
  EXPECT_CALL(platform_, Unmount(_, _)).WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_CALL(mounter_, InvokeMountTool(_))
      .WillOnce(Return(kPasswordNeededCode));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point =
      mounter_.Mount(kSomeSource, base::FilePath(kMountDir), {}, &error);
  EXPECT_FALSE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NEED_PASSWORD, error);
}

TEST_F(FUSEMounterLegacyTest, WithPassword) {
  const std::string password = "My Password";

  SetupMountExpectations();
  EXPECT_CALL(mounter_, OnInput(password)).Times(1);
  // The MountPoint returned by Mount() will unmount when it is destructed.
  EXPECT_CALL(platform_, Unmount(kMountDir, 0))
      .WillOnce(Return(MOUNT_ERROR_NONE));

  MountErrorType error = MOUNT_ERROR_UNKNOWN;
  auto mount_point = mounter_.Mount(kSomeSource, base::FilePath(kMountDir),
                                    {"password=" + password}, &error);
  EXPECT_TRUE(mount_point);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);
}

TEST(FUSEMounterPasswordTest, NoPassword) {
  MockFUSEPlatform platform;
  const FUSEMounterLegacy mounter({
      .mount_program = kMountProgram,
      .mount_user = kMountUser,
      .password_needed_codes = {kPasswordNeededCode},
      .platform = &platform,
  });
  SandboxedProcess process;
  mounter.CopyPassword(
      {
          "Password=1",   // Options are case sensitive
          "password =2",  // Space is significant
          " password=3",  // Space is significant
          "password",     // Not a valid option
      },
      &process);
  EXPECT_EQ(process.input(), "");
}

TEST(FUSEMounterPasswordTest, CopiesPassword) {
  MockFUSEPlatform platform;
  const FUSEMounterLegacy mounter({
      .mount_program = kMountProgram,
      .mount_user = kMountUser,
      .password_needed_codes = {kPasswordNeededCode},
      .platform = &platform,
  });
  for (const std::string password : {
           "",
           " ",
           "=",
           "simple",
           R"( !@#$%^&*()_-+={[}]|\:;"'<,>.?/ )",
       }) {
    SandboxedProcess process;
    mounter.CopyPassword({"password=" + password}, &process);
    EXPECT_EQ(process.input(), password);
  }
}

TEST(FUSEMounterPasswordTest, FirstPassword) {
  MockFUSEPlatform platform;
  const FUSEMounterLegacy mounter({
      .mount_program = kMountProgram,
      .mount_user = kMountUser,
      .password_needed_codes = {kPasswordNeededCode},
      .platform = &platform,
  });
  SandboxedProcess process;
  mounter.CopyPassword({"other1=value1", "password=1", "password=2",
                        "other2=value2", "password=3"},
                       &process);
  EXPECT_EQ(process.input(), "1");
}

TEST(FUSEMounterPasswordTest, IgnoredIfNotNeeded) {
  MockFUSEPlatform platform;
  const FUSEMounterLegacy mounter({
      .mount_program = kMountProgram,
      .mount_user = kMountUser,
      .platform = &platform,
  });
  SandboxedProcess process;
  mounter.CopyPassword({"password=dummy"}, &process);
  EXPECT_EQ(process.input(), "");
}

}  // namespace cros_disks
