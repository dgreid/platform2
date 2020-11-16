// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/smbfs_helper.h"

#include <string>

#include <base/files/file_path.h>
#include <brillo/process/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/uri.h"

using testing::_;
using testing::DoAll;
using testing::HasSubstr;
using testing::Return;
using testing::SetArgPointee;

namespace cros_disks {

namespace {

const uid_t kFilesUID = 700;
const uid_t kFilesAccessGID = 1501;
const base::FilePath kWorkingDir("/not_accessed_dir");
const base::FilePath kMountDir("/mount_point");
const Uri kSomeSource("smbfs", "foobarbaz");

// Mock Platform implementation for testing.
class MockPlatform : public Platform {
 public:
  MockPlatform() = default;

  MOCK_METHOD(bool,
              GetUserAndGroupId,
              (const std::string&, uid_t*, gid_t*),
              (const, override));
  MOCK_METHOD(bool,
              GetGroupId,
              (const std::string&, gid_t*),
              (const, override));
};

}  // namespace

class SmbfsHelperTest : public ::testing::Test {
 public:
  SmbfsHelperTest() : helper_(&platform_, &process_reaper_) {
    ON_CALL(platform_, GetGroupId(FUSEHelper::kFilesGroup, _))
        .WillByDefault(DoAll(SetArgPointee<1>(kFilesAccessGID), Return(true)));
    ON_CALL(platform_, GetUserAndGroupId(FUSEHelper::kFilesUser, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(kFilesUID), Return(true)));
    ON_CALL(platform_, GetUserAndGroupId("fuse-smbfs", _, _))
        .WillByDefault(
            DoAll(SetArgPointee<1>(123), SetArgPointee<2>(456), Return(true)));
  }

 protected:
  MockPlatform platform_;
  brillo::ProcessReaper process_reaper_;
  SmbfsHelper helper_;
};

TEST_F(SmbfsHelperTest, CreateMounter) {
  auto mounter = helper_.CreateMounter(kWorkingDir, kSomeSource, kMountDir, {});
  const FUSEMounterLegacy* legacy =
      static_cast<FUSEMounterLegacy*>(mounter.get());
  std::string opts = legacy->mount_options().ToString();
  EXPECT_THAT(opts, HasSubstr("mojo_id=foobarbaz"));
  EXPECT_THAT(opts, HasSubstr("uid=700"));
  EXPECT_THAT(opts, HasSubstr("gid=1501"));
}

TEST_F(SmbfsHelperTest, CanMount) {
  EXPECT_TRUE(helper_.CanMount(Uri::Parse("smbfs://foo")));
  EXPECT_FALSE(helper_.CanMount(Uri::Parse("smbfss://foo")));
  EXPECT_FALSE(helper_.CanMount(Uri::Parse("smb://foo")));
  EXPECT_FALSE(helper_.CanMount(Uri::Parse("smbfs://")));
}

}  // namespace cros_disks
