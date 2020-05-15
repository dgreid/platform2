// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/disk_cleanup_routines.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/homedirs.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/platform.h"

using ::testing::_;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::InvokeWithoutArgs;
using ::testing::NiceMock;
using ::testing::Property;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::SetArrayArgument;
using ::testing::StrictMock;

namespace {

const base::FilePath kTestShadowRoot = base::FilePath("/test/routines/root");
const char* kTestUser = "d5510a8dda6d743c46dadd979a61ae5603529742";
const base::FilePath kTestUserPath = kTestShadowRoot.Append(kTestUser);

NiceMock<cryptohome::MockFileEnumerator>* CreateMockFileEnumerator() {
  return new NiceMock<cryptohome::MockFileEnumerator>;
}

NiceMock<cryptohome::MockFileEnumerator>* CreateMockFileEnumeratorWithEntries(
    const std::vector<base::FilePath>& children) {
  auto* mock = new NiceMock<cryptohome::MockFileEnumerator>;
  for (const auto& child : children) {
    struct stat stat = {};
    mock->entries_.push_back(cryptohome::FileEnumerator::FileInfo(child, stat));
  }
  return mock;
}

}  // namespace

namespace cryptohome {

TEST(DiskCleanupRoutinesInitialization, Init) {
  StrictMock<MockPlatform> platform_;
  StrictMock<MockHomeDirs> homedirs_;

  DiskCleanupRoutines routines(&homedirs_, &platform_);
}

class DiskCleanupRoutinesTest
    : public ::testing::TestWithParam<bool /* should_test_ecryptfs */> {
 public:
  DiskCleanupRoutinesTest() : routines_(&homedirs_, &platform_) {}
  virtual ~DiskCleanupRoutinesTest() = default;

  void SetUp() {
    EXPECT_CALL(homedirs_, shadow_root())
        .WillRepeatedly(ReturnRef(kTestShadowRoot));

    EXPECT_CALL(platform_, DirectoryExists(Property(
                               &FilePath::value, EndsWith(kEcryptfsVaultDir))))
        .WillRepeatedly(Return(ShouldTestEcryptfs()));

    EXPECT_CALL(platform_, HasExtendedFileAttribute(_, _))
        .WillRepeatedly(Return(false));
  }

 protected:
  // Returns true if the test is running for eCryptfs, false if for dircrypto.
  bool ShouldTestEcryptfs() const { return GetParam(); }

  // Sets up expectations for the given tracked directories which belong to the
  // same parent directory.
  void ExpectTrackedDirectoryEnumeration(
      const std::vector<base::FilePath>& child_directories) {
    if (ShouldTestEcryptfs())  // No expecations needed for eCryptfs.
      return;

    ASSERT_FALSE(child_directories.empty());
    base::FilePath parent_directory = child_directories[0].DirName();
    // xattr is used to track directories.
    for (const auto& child : child_directories) {
      ASSERT_EQ(parent_directory.value(), child.DirName().value());
      EXPECT_CALL(platform_, GetExtendedFileAttributeAsString(
                                 child, kTrackedDirectoryNameAttribute, _))
          .WillRepeatedly(
              DoAll(SetArgPointee<2>(child.BaseName().value()), Return(true)));
      EXPECT_CALL(platform_, HasExtendedFileAttribute(
                                 child, kTrackedDirectoryNameAttribute))
          .WillRepeatedly(Return(true));
    }

    EXPECT_CALL(platform_, GetFileEnumerator(parent_directory, false,
                                             base::FileEnumerator::DIRECTORIES))
        .WillRepeatedly(InvokeWithoutArgs(
            std::bind(CreateMockFileEnumeratorWithEntries, child_directories)));
  }

  StrictMock<MockPlatform> platform_;
  StrictMock<MockHomeDirs> homedirs_;

  DiskCleanupRoutines routines_;
};

INSTANTIATE_TEST_CASE_P(WithEcryptfs,
                        DiskCleanupRoutinesTest,
                        ::testing::Values(true));
INSTANTIATE_TEST_CASE_P(WithDircrypto,
                        DiskCleanupRoutinesTest,
                        ::testing::Values(false));

TEST_P(DiskCleanupRoutinesTest, DeleteUserCache) {
  base::FilePath mount = kTestUserPath.Append(kMountDir);
  base::FilePath user = mount.Append(kUserHomeSuffix);
  base::FilePath cache = user.Append(kCacheDir);

  ExpectTrackedDirectoryEnumeration({user});
  ExpectTrackedDirectoryEnumeration({cache});

  std::vector<base::FilePath> entriesToClean{base::FilePath("abc"),
                                             base::FilePath("efg")};

  EXPECT_CALL(platform_, GetFileEnumerator(Property(&FilePath::value,
                                                    HasSubstr("user/Cache")),
                                           false, _))
      .WillRepeatedly(InvokeWithoutArgs(
          std::bind(CreateMockFileEnumeratorWithEntries, entriesToClean)));

  // Don't delete anything else.
  EXPECT_CALL(platform_, DeleteFile(_, _)).Times(0);

  for (const auto& entry : entriesToClean)
    EXPECT_CALL(platform_, DeleteFile(entry, true)).WillOnce(Return(true));

  routines_.DeleteUserCache(kTestUser);
}

TEST_P(DiskCleanupRoutinesTest, DeleteUserGCacheV1) {
  base::FilePath mount = kTestUserPath.Append(kMountDir);
  base::FilePath user = mount.Append(kUserHomeSuffix);
  base::FilePath gcache = user.Append(kGCacheDir);
  base::FilePath gcache_version1 = gcache.Append(kGCacheVersion1Dir);
  base::FilePath gcache_version2 = gcache.Append(kGCacheVersion2Dir);
  base::FilePath gcache_tmp = gcache_version1.Append(kGCacheTmpDir);

  ExpectTrackedDirectoryEnumeration({user});
  ExpectTrackedDirectoryEnumeration({gcache});
  ExpectTrackedDirectoryEnumeration({gcache_version1, gcache_version2});
  ExpectTrackedDirectoryEnumeration({gcache_tmp});

  std::vector<base::FilePath> entriesToClean{base::FilePath("abc"),
                                             base::FilePath("efg")};

  EXPECT_CALL(platform_,
              GetFileEnumerator(
                  Property(&FilePath::value, HasSubstr("user/GCache/v1/tmp")),
                  false, _))
      .WillRepeatedly(InvokeWithoutArgs(
          std::bind(CreateMockFileEnumeratorWithEntries, entriesToClean)));

  EXPECT_CALL(platform_, GetFileEnumerator(Property(&FilePath::value,
                                                    EndsWith("user/GCache/v1")),
                                           true, base::FileEnumerator::FILES))
      .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));
  EXPECT_CALL(platform_, GetFileEnumerator(Property(&FilePath::value,
                                                    EndsWith("user/GCache/v2")),
                                           true, base::FileEnumerator::FILES))
      .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  // Don't delete anything else.
  EXPECT_CALL(platform_, DeleteFile(_, _)).Times(0);

  for (const auto& entry : entriesToClean)
    EXPECT_CALL(platform_, DeleteFile(entry, true)).WillOnce(Return(true));

  routines_.DeleteUserGCache(kTestUser);
}

TEST_P(DiskCleanupRoutinesTest, DeleteUserGCacheV2) {
  base::FilePath mount = kTestUserPath.Append(kMountDir);
  base::FilePath user = mount.Append(kUserHomeSuffix);
  base::FilePath gcache = user.Append(kGCacheDir);
  base::FilePath gcache_version1 = gcache.Append(kGCacheVersion1Dir);
  base::FilePath gcache_version2 = gcache.Append(kGCacheVersion2Dir);
  base::FilePath gcache_tmp = gcache_version1.Append(kGCacheTmpDir);

  ExpectTrackedDirectoryEnumeration({user});
  ExpectTrackedDirectoryEnumeration({gcache});
  ExpectTrackedDirectoryEnumeration({gcache_version1, gcache_version2});
  ExpectTrackedDirectoryEnumeration({gcache_tmp});

  std::vector<base::FilePath> entriesToClean{
      base::FilePath("abc"), base::FilePath("efg"), base::FilePath("hij")};

  std::vector<base::FilePath> v1Entries, v2Entries;
  for (const auto& entry : entriesToClean) {
    v1Entries.push_back(gcache_version1.Append(entry));
    v2Entries.push_back(gcache_version2.Append(entry));
  }

  EXPECT_CALL(platform_,
              GetFileEnumerator(
                  Property(&FilePath::value, HasSubstr("user/GCache/v1/tmp")),
                  false, _))
      .WillRepeatedly(InvokeWithoutArgs(CreateMockFileEnumerator));

  EXPECT_CALL(platform_, GetFileEnumerator(Property(&FilePath::value,
                                                    EndsWith("user/GCache/v1")),
                                           true, base::FileEnumerator::FILES))
      .WillRepeatedly(InvokeWithoutArgs(
          std::bind(CreateMockFileEnumeratorWithEntries, v1Entries)));
  EXPECT_CALL(platform_, GetFileEnumerator(Property(&FilePath::value,
                                                    EndsWith("user/GCache/v2")),
                                           true, base::FileEnumerator::FILES))
      .WillRepeatedly(InvokeWithoutArgs(
          std::bind(CreateMockFileEnumeratorWithEntries, v2Entries)));

  EXPECT_CALL(platform_,
              HasExtendedFileAttribute(v1Entries[0], kRemovableFileAttribute))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              HasExtendedFileAttribute(v2Entries[0], kRemovableFileAttribute))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, HasNoDumpFileAttribute(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, HasNoDumpFileAttribute(v1Entries[1]))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, HasNoDumpFileAttribute(v2Entries[1]))
      .WillRepeatedly(Return(true));

  // Don't delete anything else.
  EXPECT_CALL(platform_, DeleteFile(_, _)).Times(0);

  EXPECT_CALL(platform_, DeleteFile(v1Entries[0], false))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(v1Entries[1], false))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_, DeleteFile(v2Entries[0], false))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(v2Entries[1], false))
      .WillOnce(Return(true));

  routines_.DeleteUserGCache(kTestUser);
}

TEST_P(DiskCleanupRoutinesTest, DeleteAndroidCache) {
  base::FilePath mount = kTestUserPath.Append(kMountDir);
  base::FilePath root = mount.Append(kRootHomeSuffix);

  ExpectTrackedDirectoryEnumeration({root});

  std::vector<base::FilePath> entriesToDelete{base::FilePath("abc"),
                                              base::FilePath("efg")};

  base::FilePath codeCacheInodeFile = root.Append("accache");
  base::FilePath cacheInodeFile = root.Append("acache");

  std::vector<base::FilePath> entriesToClean{codeCacheInodeFile.Append("code"),
                                             cacheInodeFile.Append("cache")};

  uint64_t codeCacheInode = 4;
  uint64_t cacheInode = 5;

  for (auto& entry : entriesToClean) {
    std::vector<base::FilePath> entries;
    for (const auto& entryToDelete : entriesToDelete)
      entries.push_back(entry.Append(entryToDelete));

    EXPECT_CALL(platform_, EnumerateDirectoryEntries(entry, false, _))
        .WillOnce(DoAll(SetArgPointee<2>(entries), Return(true)));

    for (const auto& entry : entries)
      EXPECT_CALL(platform_, DeleteFile(entry, true)).WillOnce(Return(true));
  }

  auto* enumerator = new NiceMock<cryptohome::MockFileEnumerator>;

  ASSERT_EQ(entriesToClean.size(), 2);
  struct stat stat = {};

  stat.st_ino = 1;
  enumerator->entries_.push_back(
      cryptohome::FileEnumerator::FileInfo(codeCacheInodeFile, stat));
  enumerator->entries_.push_back(
      cryptohome::FileEnumerator::FileInfo(cacheInodeFile, stat));

  stat.st_ino = codeCacheInode;
  enumerator->entries_.push_back(
      cryptohome::FileEnumerator::FileInfo(entriesToClean[0], stat));
  stat.st_ino = cacheInode;
  enumerator->entries_.push_back(
      cryptohome::FileEnumerator::FileInfo(entriesToClean[1], stat));

  EXPECT_CALL(platform_,
              HasExtendedFileAttribute(codeCacheInodeFile,
                                       kAndroidCodeCacheInodeAttribute))
      .WillOnce(Return(true));
  char* array = reinterpret_cast<char*>(&codeCacheInode);
  EXPECT_CALL(platform_,
              GetExtendedFileAttribute(codeCacheInodeFile,
                                       kAndroidCodeCacheInodeAttribute, _, _))
      .WillOnce(
          DoAll(SetArrayArgument<2>(array, array + sizeof(codeCacheInode)),
                Return(true)));

  EXPECT_CALL(platform_, HasExtendedFileAttribute(cacheInodeFile,
                                                  kAndroidCacheInodeAttribute))
      .WillOnce(Return(true));
  array = reinterpret_cast<char*>(&cacheInode);
  EXPECT_CALL(platform_, GetExtendedFileAttribute(
                             cacheInodeFile, kAndroidCacheInodeAttribute, _, _))
      .WillOnce(DoAll(SetArrayArgument<2>(array, array + sizeof(cacheInode)),
                      Return(true)));

  EXPECT_CALL(
      platform_,
      GetFileEnumerator(
          Property(&FilePath::value,
                   EndsWith(std::string(ShouldTestEcryptfs() ? kEcryptfsVaultDir
                                                             : kMountDir) +
                            "/root")),
          true, base::FileEnumerator::DIRECTORIES))
      .WillRepeatedly(Return(enumerator));

  EXPECT_TRUE(routines_.DeleteUserAndroidCache(kTestUser));
}

TEST_P(DiskCleanupRoutinesTest, DeleteUserProfile) {
  EXPECT_CALL(homedirs_, RemoveLECredentials(kTestUser)).Times(1);
  EXPECT_CALL(platform_, DeleteFile(kTestUserPath, true))
      .WillOnce(Return(true));

  EXPECT_TRUE(routines_.DeleteUserProfile(kTestUser));
}

TEST_P(DiskCleanupRoutinesTest, DeleteUserProfileFail) {
  EXPECT_CALL(homedirs_, RemoveLECredentials(kTestUser)).Times(1);
  EXPECT_CALL(platform_, DeleteFile(kTestUserPath, true))
      .WillOnce(Return(false));

  EXPECT_FALSE(routines_.DeleteUserProfile(kTestUser));
}

}  // namespace cryptohome
