// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/threading/platform_thread.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/ref_count.h"
#include "dlcservice/test_utils.h"
#include "dlcservice/utils.h"

using base::FilePath;
using std::string;
using std::vector;
using testing::_;
using testing::Return;
using testing::SetArgPointee;

namespace dlcservice {

class RefCountTest : public BaseTest {
 public:
  RefCountTest() = default;

 protected:
  void SetUp() override {
    BaseTest::SetUp();
    ref_count_path_ = JoinPaths(prefs_path_, "ref_count.bin");
  }

  RefCountInfo ReadRefCountInfo() {
    EXPECT_TRUE(base::PathExists(ref_count_path_));
    string str;
    EXPECT_TRUE(base::ReadFileToString(ref_count_path_, &str));
    RefCountInfo info;
    EXPECT_TRUE(info.ParseFromString(str));
    return info;
  }

  void GenerateRefCountInfo(const vector<string>& usernames,
                            int64_t access_time) {
    string str;
    RefCountInfo info;
    info.set_last_access_time_us(access_time);
    for (const auto& username : usernames) {
      info.add_users()->set_sanitized_username(username);
    }

    EXPECT_TRUE(info.SerializeToString(&str));
    EXPECT_TRUE(WriteToFile(ref_count_path_, str));
  }

  FilePath ref_count_path_;

 private:
  RefCountTest(const RefCountTest&) = delete;
  RefCountTest& operator=(const RefCountTest&) = delete;
};

TEST_F(RefCountTest, CreateUserBased) {
  auto ref_count = RefCountInterface::Create(kUsedByUser, prefs_path_);
  RefCountInterface& ref_count_ref = *ref_count;
  EXPECT_EQ(typeid(ref_count_ref), typeid(UserRefCount));
}

TEST_F(RefCountTest, CreateSystem) {
  auto ref_count = RefCountInterface::Create(kUsedBySystem, prefs_path_);
  RefCountInterface& ref_count_ref = *ref_count;
  EXPECT_EQ(typeid(ref_count_ref), typeid(SystemRefCount));
}

// Make sure it can read from the file.
TEST_F(RefCountTest, Ctor) {
  GenerateRefCountInfo({"user-1", "user-2"}, 10);
  SystemRefCount ref_count(prefs_path_);
  // TODO(ahassani): Improve the test so we don't access the private variables
  // like this.
  EXPECT_EQ(ref_count.ref_count_info_.users_size(), 2);
  EXPECT_EQ(ref_count.ref_count_info_.last_access_time_us(), 10);
}

TEST_F(RefCountTest, SystemInstalledAndUninstallDlc) {
  SystemRefCount ref_count(prefs_path_);
  EXPECT_TRUE(ref_count.InstalledDlc());
  auto info = ReadRefCountInfo();
  EXPECT_EQ(info.users(0).sanitized_username(), "system");

  EXPECT_TRUE(ref_count.UninstalledDlc());
  info = ReadRefCountInfo();
  EXPECT_EQ(info.users_size(), 0);
}

TEST_F(RefCountTest, UserInstalledAndUninstallDlc) {
  for (const auto& sanitized_username : {"user-1", "user-2"}) {
    auto path = JoinPaths(SystemState::Get()->users_dir(), sanitized_username);
    EXPECT_TRUE(base::CreateDirectoryAndGetError(path, nullptr));
  }

  EXPECT_CALL(*mock_session_manager_proxy_ptr_,
              RetrievePrimarySession(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<0>("username-1"),
                      SetArgPointee<1>("user-1"), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>("username-2"),
                      SetArgPointee<1>("user-2"), Return(true)));

  UserRefCount::SessionChanged(kSessionStarted);
  UserRefCount ref_count(prefs_path_);
  EXPECT_TRUE(ref_count.InstalledDlc());
  auto info = ReadRefCountInfo();
  EXPECT_EQ(info.users_size(), 1);
  EXPECT_EQ(info.users(0).sanitized_username(), "user-1");

  UserRefCount::SessionChanged(kSessionStarted);
  EXPECT_TRUE(ref_count.InstalledDlc());
  info = ReadRefCountInfo();
  EXPECT_EQ(info.users_size(), 2);
  EXPECT_EQ(info.users(0).sanitized_username(), "user-1");
  EXPECT_EQ(info.users(1).sanitized_username(), "user-2");

  // Uninstall should remove the user now.
  EXPECT_TRUE(ref_count.UninstalledDlc());
  info = ReadRefCountInfo();
  EXPECT_EQ(info.users_size(), 1);
  EXPECT_EQ(info.users(0).sanitized_username(), "user-1");
}

class MockRefCountBase : public RefCountBase {
 public:
  explicit MockRefCountBase(const FilePath& prefs_path)
      : RefCountBase(prefs_path) {}

  MOCK_METHOD(base::TimeDelta, GetExpirationDelay, (), (const override));
  MOCK_METHOD(std::string, GetCurrentUserName, (), (const override));

 private:
  MockRefCountBase(const MockRefCountBase&) = delete;
  MockRefCountBase& operator=(const MockRefCountBase&) = delete;
};

TEST_F(RefCountTest, ShouldPurgeDlc) {
  MockRefCountBase ref_count(prefs_path_);

  // If the DLC is not touched yet, it should return false.
  EXPECT_FALSE(ref_count.ShouldPurgeDlc());

  EXPECT_CALL(ref_count, GetCurrentUserName()).WillRepeatedly(Return("user-1"));

  // After this ref count should be persisted.
  EXPECT_TRUE(ref_count.InstalledDlc());

  // We have a user using it, so we can't remove it even if the expiration has
  // passed.
  EXPECT_FALSE(ref_count.ShouldPurgeDlc());

  // Now there is no user after this.
  EXPECT_TRUE(ref_count.UninstalledDlc());
  EXPECT_CALL(ref_count, GetExpirationDelay())
      .WillOnce(Return(base::TimeDelta::FromSeconds(1)));
  // There is no user, but also the expiration has not reached.
  EXPECT_FALSE(ref_count.ShouldPurgeDlc());

  EXPECT_CALL(ref_count, GetExpirationDelay())
      .WillOnce(Return(base::TimeDelta::FromMicroseconds(1)));
  // Now lets sleep briefly.
  base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(3));
  // We have now reached the 1 seconds timeout. So with no user, it should be
  // removed.
  EXPECT_TRUE(ref_count.ShouldPurgeDlc());
}
}  // namespace dlcservice
