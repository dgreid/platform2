// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/smb_filesystem.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <utility>

#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "smbfs/smb_credential.h"

namespace smbfs {
namespace {

using ::testing::_;

constexpr char kSharePath[] = "smb://server/share";
constexpr char kUsername[] = "my-username";

class MockDelegate : public SmbFilesystem::Delegate {
 public:
  MOCK_METHOD(void,
              RequestCredentials,
              (RequestCredentialsCallback),
              (override));
};

class TestSmbFilesystem : public SmbFilesystem {
 public:
  TestSmbFilesystem() : SmbFilesystem(&delegate_, kSharePath) {}

  MockDelegate& delegate() { return delegate_; }

 private:
  MockDelegate delegate_;
};

}  // namespace

class SmbFilesystemTest : public testing::Test {
 protected:
  base::MessageLoopForIO message_loop_;
};

TEST_F(SmbFilesystemTest, SetResolvedAddress) {
  TestSmbFilesystem fs;

  // Initial value is share path.
  EXPECT_EQ(kSharePath, fs.resolved_share_path());

  fs.SetResolvedAddress({1, 2, 3, 4});
  EXPECT_EQ("smb://1.2.3.4/share", fs.resolved_share_path());
  fs.SetResolvedAddress({127, 0, 0, 1});
  EXPECT_EQ("smb://127.0.0.1/share", fs.resolved_share_path());

  // Invalid address does nothing.
  fs.SetResolvedAddress({1, 2, 3});
  EXPECT_EQ("smb://127.0.0.1/share", fs.resolved_share_path());

  // Empty address resets to original share path.
  fs.SetResolvedAddress({});
  EXPECT_EQ(kSharePath, fs.resolved_share_path());
}

TEST_F(SmbFilesystemTest, MakeStatModeBits) {
  TestSmbFilesystem fs;

  // Check: "Other" permission bits are cleared.
  mode_t in_mode = S_IRWXO;
  mode_t out_mode = fs.MakeStatModeBits(in_mode);
  EXPECT_EQ(0, out_mode);

  // Check: Directories have user execute bit set.
  in_mode = S_IFDIR;
  out_mode = fs.MakeStatModeBits(in_mode);
  EXPECT_TRUE(out_mode & S_IXUSR);

  // Check: Files do not have user execute bit set.
  in_mode = S_IFREG;
  out_mode = fs.MakeStatModeBits(in_mode);
  EXPECT_FALSE(out_mode & S_IXUSR);

  // Check: Group bits equal user bits.
  in_mode = S_IRUSR | S_IWUSR;
  out_mode = fs.MakeStatModeBits(in_mode);
  EXPECT_EQ(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, out_mode);
}

TEST_F(SmbFilesystemTest, MakeStatModeBitsFromDOSAttributes) {
  TestSmbFilesystem fs;

  // Check: The directory attribute sets the directory type bit.
  uint16_t dos_attrs = SMBC_DOS_MODE_DIRECTORY;
  mode_t out_mode = fs.MakeStatModeBitsFromDOSAttributes(dos_attrs);
  EXPECT_TRUE(out_mode & S_IFDIR);
  EXPECT_FALSE(out_mode & S_IFREG);

  // Check: Absence of the directory attribute sets the file type bit.
  dos_attrs = 0;
  out_mode = fs.MakeStatModeBitsFromDOSAttributes(dos_attrs);
  EXPECT_TRUE(out_mode & S_IFREG);
  EXPECT_FALSE(out_mode & S_IFDIR);

  // Check: Special attributes (without the directory attribute) set the file
  // type bit.
  dos_attrs = SMBC_DOS_MODE_ARCHIVE;
  out_mode = fs.MakeStatModeBitsFromDOSAttributes(dos_attrs);
  EXPECT_TRUE(out_mode & S_IFREG);

  dos_attrs = SMBC_DOS_MODE_SYSTEM;
  out_mode = fs.MakeStatModeBitsFromDOSAttributes(dos_attrs);
  EXPECT_TRUE(out_mode & S_IFREG);

  dos_attrs = SMBC_DOS_MODE_HIDDEN;
  out_mode = fs.MakeStatModeBitsFromDOSAttributes(dos_attrs);
  EXPECT_TRUE(out_mode & S_IFREG);

  // Check: Absence of the read-only attribute sets the user write bit.
  dos_attrs = 0;
  out_mode = fs.MakeStatModeBitsFromDOSAttributes(dos_attrs);
  EXPECT_TRUE(out_mode & S_IWUSR);

  // Check: Presence of the read-only attribute clears the user write bit.
  dos_attrs = SMBC_DOS_MODE_READONLY;
  out_mode = fs.MakeStatModeBitsFromDOSAttributes(dos_attrs);
  EXPECT_FALSE(out_mode & S_IWUSR);

  dos_attrs = SMBC_DOS_MODE_READONLY | SMBC_DOS_MODE_DIRECTORY;
  out_mode = fs.MakeStatModeBitsFromDOSAttributes(dos_attrs);
  EXPECT_TRUE(out_mode & (S_IFDIR | S_IWUSR));
}

TEST_F(SmbFilesystemTest, MaybeUpdateCredentials_NoRequest) {
  TestSmbFilesystem fs;

  EXPECT_CALL(fs.delegate(), RequestCredentials(_)).Times(0);
  fs.MaybeUpdateCredentials(EBUSY);
  base::RunLoop().RunUntilIdle();
}

TEST_F(SmbFilesystemTest, MaybeUpdateCredentials_RequestOnEPERM) {
  TestSmbFilesystem fs;

  EXPECT_FALSE(fs.credentials_);

  base::RunLoop run_loop;
  EXPECT_CALL(fs.delegate(), RequestCredentials(_))
      .WillOnce([&](MockDelegate::RequestCredentialsCallback callback) {
        std::move(callback).Run(std::make_unique<SmbCredential>(
            "" /* workgroup */, kUsername, nullptr));
        run_loop.Quit();
      });
  fs.MaybeUpdateCredentials(EPERM);
  run_loop.Run();

  EXPECT_TRUE(fs.credentials_);
  EXPECT_EQ(fs.credentials_->username, kUsername);
}

TEST_F(SmbFilesystemTest, MaybeUpdateCredentials_RequestOnEACCES) {
  TestSmbFilesystem fs;

  EXPECT_FALSE(fs.credentials_);

  base::RunLoop run_loop;
  EXPECT_CALL(fs.delegate(), RequestCredentials(_))
      .WillOnce([&](MockDelegate::RequestCredentialsCallback callback) {
        std::move(callback).Run(std::make_unique<SmbCredential>(
            "" /* workgroup */, kUsername, nullptr));
        run_loop.Quit();
      });
  fs.MaybeUpdateCredentials(EACCES);
  run_loop.Run();

  EXPECT_TRUE(fs.credentials_);
  EXPECT_EQ(fs.credentials_->username, kUsername);
}

TEST_F(SmbFilesystemTest, MaybeUpdateCredentials_NoDelegate) {
  TestSmbFilesystem fs;

  fs.MaybeUpdateCredentials(EPERM);
  base::RunLoop().RunUntilIdle();
}

TEST_F(SmbFilesystemTest, MaybeUpdateCredentials_OnlyOneRequest) {
  TestSmbFilesystem fs;

  EXPECT_CALL(fs.delegate(), RequestCredentials(_)).Times(1);
  fs.MaybeUpdateCredentials(EACCES);
  fs.MaybeUpdateCredentials(EACCES);
  base::RunLoop().RunUntilIdle();
}

TEST_F(SmbFilesystemTest, MaybeUpdateCredentials_IgnoreEmptyResponse) {
  TestSmbFilesystem fs;

  fs.credentials_ =
      std::make_unique<SmbCredential>("" /* workgroup */, kUsername, nullptr);

  base::RunLoop run_loop;
  EXPECT_CALL(fs.delegate(), RequestCredentials(_))
      .WillOnce([&](MockDelegate::RequestCredentialsCallback callback) {
        std::move(callback).Run(nullptr);
        run_loop.Quit();
      });
  fs.MaybeUpdateCredentials(EACCES);
  run_loop.Run();

  EXPECT_TRUE(fs.credentials_);
}

}  // namespace smbfs
