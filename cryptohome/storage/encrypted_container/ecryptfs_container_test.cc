// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/ecryptfs_container.h"

#include <memory>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

using ::testing::_;
using ::testing::Return;

namespace cryptohome {

class EcryptfsContainerTest : public ::testing::Test {
 public:
  EcryptfsContainerTest()
      : backing_dir_(base::FilePath("/a/b/c")),
        key_reference_({.fek_sig = brillo::SecureBlob("random_keysig"),
                        .fnek_sig = brillo::SecureBlob("random_fnek_sig")}),
        key_({.fek = brillo::SecureBlob("random key"),
              .fnek = brillo::SecureBlob("random_fnek"),
              .fek_salt = brillo::SecureBlob("random_fek_salt"),
              .fnek_salt = brillo::SecureBlob("random_fnek_salt")}),
        container_(std::make_unique<EcryptfsContainer>(
            backing_dir_, key_reference_, &platform_)) {}
  ~EcryptfsContainerTest() override = default;

 protected:
  base::FilePath backing_dir_;
  FileSystemKeyReference key_reference_;
  FileSystemKey key_;
  MockPlatform platform_;
  std::unique_ptr<EncryptedContainer> container_;
};

// Tests the creation path for an eCryptFs container.
TEST_F(EcryptfsContainerTest, SetupCreateCheck) {
  EXPECT_CALL(platform_,
              AddEcryptfsAuthToken(_, testing::MatchesRegex("[0-9a-z]*"), _))
      .Times(2)
      .WillRepeatedly(Return(true));

  EXPECT_TRUE(container_->Setup(key_, true));
  EXPECT_TRUE(platform_.DirectoryExists(backing_dir_));
}

// Tests the setup path for an existing eCryptFs container.
TEST_F(EcryptfsContainerTest, SetupNoCreateCheck) {
  EXPECT_CALL(platform_,
              AddEcryptfsAuthToken(_, testing::MatchesRegex("[0-9a-z]*"), _))
      .Times(2)
      .WillRepeatedly(Return(true));

  EXPECT_TRUE(container_->Setup(key_, false));
}

// Tests the failure path on failing to add the eCryptFs auth token to the
// user keyring.
TEST_F(EcryptfsContainerTest, SetupFailedEncryptionKeyAdd) {
  EXPECT_CALL(platform_,
              AddEcryptfsAuthToken(_, testing::MatchesRegex("[0-9a-z]*"), _))
      .WillOnce(Return(false));

  EXPECT_FALSE(container_->Setup(key_, false));
}

// Tests the failure path on failing to invalidate the user keyring.
TEST_F(EcryptfsContainerTest, TeardownInvalidateKey) {
  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));

  EXPECT_TRUE(container_->Teardown());
}

}  // namespace cryptohome
