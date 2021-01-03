// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/fscrypt_container.h"

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

class FscryptContainerTest : public ::testing::Test {
 public:
  FscryptContainerTest()
      : backing_dir_(base::FilePath("/a/b/c")),
        key_reference_({.fek_sig = brillo::SecureBlob("random_keysig")}),
        key_({.fek = brillo::SecureBlob("random key")}),
        container_(std::make_unique<FscryptContainer>(
            backing_dir_, key_reference_, &platform_)) {}
  ~FscryptContainerTest() override = default;

 protected:
  base::FilePath backing_dir_;
  FileSystemKeyReference key_reference_;
  FileSystemKey key_;
  MockPlatform platform_;
  std::unique_ptr<EncryptedContainer> container_;
};

// Tests the create path for fscrypt containers.
TEST_F(FscryptContainerTest, SetupCreateCheck) {
  EXPECT_CALL(platform_, AddDirCryptoKeyToKeyring(_, _)).WillOnce(Return(true));

  EXPECT_CALL(platform_, SetDirCryptoKey(backing_dir_, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(container_->Setup(key_, true));
  EXPECT_TRUE(platform_.DirectoryExists(backing_dir_));
}

// Tests the setup path for an existing fscrypt container.
TEST_F(FscryptContainerTest, SetupNoCreateCheck) {
  EXPECT_CALL(platform_, AddDirCryptoKeyToKeyring(_, _)).WillOnce(Return(true));

  EXPECT_CALL(platform_, SetDirCryptoKey(backing_dir_, _))
      .WillOnce(Return(true));

  EXPECT_TRUE(container_->Setup(key_, false));
}

// Tests failure path when adding the encryption key to the kernel/filesystem
// keyring fails.
TEST_F(FscryptContainerTest, SetupFailedEncryptionKeyAdd) {
  EXPECT_CALL(platform_, AddDirCryptoKeyToKeyring(_, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(container_->Setup(key_, false));
}

// Tests failure path when setting the encryption policy for the backing
// directory fails.
TEST_F(FscryptContainerTest, SetupFailedEncryptionKeySet) {
  EXPECT_CALL(platform_, AddDirCryptoKeyToKeyring(_, _)).WillOnce(Return(true));

  EXPECT_CALL(platform_, SetDirCryptoKey(backing_dir_, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(container_->Setup(key_, false));
}

// Tests failure path on failing to invalidate an added key from the
// kernel/filesystem keyring.
TEST_F(FscryptContainerTest, TeardownInvalidateKey) {
  EXPECT_CALL(platform_, InvalidateDirCryptoKey(_, _)).WillOnce(Return(false));

  EXPECT_FALSE(container_->Teardown());
}

}  // namespace cryptohome
