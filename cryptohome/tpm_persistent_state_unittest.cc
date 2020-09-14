// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for TpmPersistentState.

#include "cryptohome/tpm_persistent_state.h"

#include <map>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

#include "cryptohome/mock_platform.h"

using brillo::SecureBlob;

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {

using TpmOwnerDependency = TpmPersistentState::TpmOwnerDependency;

extern const base::FilePath kTpmOwnedFile;
const base::FilePath kTpmStatusFile("/mnt/stateful_partition/.tpm_status");
const base::FilePath kShallInitializeFile(
    "/home/.shadow/.can_attempt_ownership");

class TpmPersistentStateTest : public ::testing::Test {
 public:
  // Default mock implementations for |platform_| methods.
  // Files are emulated using |files_| map: <file path> -> <file contents>.
  bool FileExists(const base::FilePath& path) const {
    return files_.count(path) > 0;
  }
  bool FileDelete(const base::FilePath& path, bool /* recursive */) {
    return files_.erase(path) == 1;
  }
  bool FileTouch(const base::FilePath& path) {
    if (!FileExists(path)) {
      files_.emplace(path, brillo::Blob());
    }
    return FileExists(path);
  }
  bool GetFileSize(const base::FilePath& path, int64_t* size) {
    if (!FileExists(path)) {
      return false;
    }
    *size = files_[path].size();
    return true;
  }
  bool FileRead(const base::FilePath& path, brillo::Blob* blob) {
    if (!FileExists(path)) {
      return false;
    }
    *blob = files_[path];
    return true;
  }
  bool FileWrite(const base::FilePath& path, const brillo::Blob& blob) {
    files_[path] = blob;
    return true;
  }

  bool FileWriteSecureBlob(const base::FilePath& path,
                           const brillo::SecureBlob& sblob) {
    return FileWrite(path, brillo::Blob(sblob.begin(), sblob.end()));
  }

  bool FileWriteAtomic(const base::FilePath& path,
                       const brillo::SecureBlob& blob,
                       mode_t /* mode */) {
    return FileWriteSecureBlob(path, blob);
  }

  void SetUp() override {
    ON_CALL(platform_, FileExists(_))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileExists));
    ON_CALL(platform_, DeleteFileDurable(_, _))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileDelete));
    ON_CALL(platform_, TouchFileDurable(_))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileTouch));
    ON_CALL(platform_, GetFileSize(_, _))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::GetFileSize));
    ON_CALL(platform_, ReadFile(_, _))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileRead));
    ON_CALL(platform_, WriteSecureBlobToFile(_, _))
        .WillByDefault(
            Invoke(this, &TpmPersistentStateTest::FileWriteSecureBlob));
    ON_CALL(platform_, WriteSecureBlobToFileAtomicDurable(_, _, _))
        .WillByDefault(Invoke(this, &TpmPersistentStateTest::FileWriteAtomic));
    ON_CALL(platform_, DataSyncFile(_)).WillByDefault(Return(true));
  }

 protected:
  std::map<base::FilePath, brillo::Blob> files_;
  NiceMock<MockPlatform> platform_;

  // Declare tpm_init_ last, so it gets destroyed before all the mocks.
  TpmPersistentState tpm_persistent_state_{&platform_};
};

TEST_F(TpmPersistentStateTest, SetPassword) {
  // Initially there's no password.
  ASSERT_FALSE(FileExists(kTpmStatusFile));
  SecureBlob result;
  EXPECT_FALSE(tpm_persistent_state_.GetSealedPassword(&result));

  // After setting the default password, we get back an empty password.
  EXPECT_TRUE(tpm_persistent_state_.SetDefaultPassword());
  EXPECT_TRUE(tpm_persistent_state_.GetSealedPassword(&result));
  EXPECT_TRUE(result.empty());

  // After setting some password, we get it back.
  SecureBlob password("password");
  EXPECT_TRUE(tpm_persistent_state_.SetSealedPassword(password));
  EXPECT_TRUE(tpm_persistent_state_.GetSealedPassword(&result));
  EXPECT_EQ(password, result);

  // Clearing status clears the password.
  EXPECT_TRUE(tpm_persistent_state_.ClearStatus());
  EXPECT_FALSE(tpm_persistent_state_.GetSealedPassword(&result));
}

TEST_F(TpmPersistentStateTest, SetDependencies) {
  // Initially, there's no password, no dependencies, so clearing suceeds.
  ASSERT_FALSE(FileExists(kTpmStatusFile));
  EXPECT_TRUE(tpm_persistent_state_.ClearStoredPasswordIfNotNeeded());

  // Setting the default password should also set both dependencies to on.
  EXPECT_TRUE(tpm_persistent_state_.SetDefaultPassword());
  EXPECT_FALSE(tpm_persistent_state_.ClearStoredPasswordIfNotNeeded());

  // Clearing the state after setting the password should allow clearing
  // the password (which is already clear).
  EXPECT_TRUE(tpm_persistent_state_.ClearStatus());
  EXPECT_TRUE(tpm_persistent_state_.ClearStoredPasswordIfNotNeeded());

  // Setting any password should also set both dependencies to on.
  SecureBlob password("password");
  EXPECT_TRUE(tpm_persistent_state_.SetSealedPassword(password));
  EXPECT_FALSE(tpm_persistent_state_.ClearStoredPasswordIfNotNeeded());

  // Clearing one dependency is not sufficient for clearing the password.
  EXPECT_TRUE(
      tpm_persistent_state_.ClearDependency(TpmOwnerDependency::kAttestation));
  EXPECT_FALSE(tpm_persistent_state_.ClearStoredPasswordIfNotNeeded());
  SecureBlob result;
  EXPECT_TRUE(tpm_persistent_state_.GetSealedPassword(&result));
  EXPECT_EQ(password, result);

  // Clearing both dependencies should allow clearing the password.
  EXPECT_TRUE(tpm_persistent_state_.ClearDependency(
      TpmOwnerDependency::kInstallAttributes));
  EXPECT_TRUE(tpm_persistent_state_.ClearStoredPasswordIfNotNeeded());
  EXPECT_FALSE(tpm_persistent_state_.GetSealedPassword(&result));

  // Clearing the state after setting the password should allow clearing
  // the password (which is already clear).
  EXPECT_TRUE(tpm_persistent_state_.ClearStatus());
  EXPECT_TRUE(tpm_persistent_state_.ClearStoredPasswordIfNotNeeded());
}

TEST_F(TpmPersistentStateTest, TpmStatusPreExisting) {
  SecureBlob password("password");
  TpmStatus status;
  status.set_flags(TpmStatus::OWNED_BY_THIS_INSTALL |
                   TpmStatus::USES_RANDOM_OWNER |
                   TpmStatus::ATTESTATION_NEEDS_OWNER);
  status.set_owner_password(password.to_string());
  brillo::Blob status_blob = brillo::BlobFromString(status.SerializeAsString());
  FileWrite(kTpmStatusFile, status_blob);

  SecureBlob result;
  EXPECT_TRUE(tpm_persistent_state_.GetSealedPassword(&result));
  EXPECT_EQ(password, result);
  EXPECT_FALSE(tpm_persistent_state_.ClearStoredPasswordIfNotNeeded());
  EXPECT_TRUE(
      tpm_persistent_state_.ClearDependency(TpmOwnerDependency::kAttestation));
  EXPECT_TRUE(tpm_persistent_state_.ClearStoredPasswordIfNotNeeded());
}

TEST_F(TpmPersistentStateTest, TpmStatusCached) {
  // The TpmStatus file is read only once.
  TpmStatus empty_status;
  empty_status.set_flags(TpmStatus::NONE);
  brillo::Blob empty_status_blob =
      brillo::BlobFromString(empty_status.SerializeAsString());
  FileWrite(kTpmStatusFile, empty_status_blob);
  EXPECT_CALL(platform_, ReadFile(kTpmStatusFile, _)).Times(1);
  SecureBlob password("password");
  EXPECT_TRUE(tpm_persistent_state_.SetSealedPassword(password));
  SecureBlob result;
  EXPECT_TRUE(tpm_persistent_state_.GetSealedPassword(&result));
  EXPECT_EQ(password, result);
  EXPECT_TRUE(tpm_persistent_state_.GetSealedPassword(&result));
  EXPECT_EQ(password, result);

  // Each change to state leads to write.
  EXPECT_CALL(platform_,
              WriteSecureBlobToFileAtomicDurable(kTpmStatusFile, _, _))
      .Times(2);
  EXPECT_TRUE(tpm_persistent_state_.SetSealedPassword(password));
  EXPECT_TRUE(tpm_persistent_state_.ClearDependency(
      TpmOwnerDependency::kInstallAttributes));
  // Clearing the status leads to deleting the file.
  EXPECT_TRUE(tpm_persistent_state_.ClearStatus());
  EXPECT_FALSE(FileExists(kTpmStatusFile));
}

TEST_F(TpmPersistentStateTest, TpmReady) {
  // The file is read only once, after that the flag is cached in memory.
  ASSERT_FALSE(FileExists(kTpmOwnedFile));
  EXPECT_CALL(platform_, FileExists(kTpmOwnedFile)).Times(1);
  // Initially, there's no file, so tpm is not ready.
  EXPECT_FALSE(tpm_persistent_state_.IsReady());
  EXPECT_FALSE(tpm_persistent_state_.IsReady());

  // Saying that it's ready creates the file and returns correct status
  // afterwards.
  EXPECT_TRUE(tpm_persistent_state_.SetReady(true));
  EXPECT_TRUE(tpm_persistent_state_.IsReady());
  EXPECT_TRUE(FileExists(kTpmOwnedFile));

  // Setting the flag back to false...
  EXPECT_TRUE(tpm_persistent_state_.SetReady(false));
  EXPECT_FALSE(tpm_persistent_state_.IsReady());
  EXPECT_FALSE(FileExists(kTpmOwnedFile));
}

TEST_F(TpmPersistentStateTest, TpmReadyPreExisting) {
  // If there's a kTpmOwnedFile at start, IsReady returns true.
  FileTouch(kTpmOwnedFile);
  EXPECT_CALL(platform_, FileExists(kTpmOwnedFile)).Times(1);
  EXPECT_TRUE(tpm_persistent_state_.IsReady());
  EXPECT_TRUE(tpm_persistent_state_.IsReady());
}

TEST_F(TpmPersistentStateTest, ShallInitialize) {
  // Two requests result in a single file operation, after that it is cached.
  EXPECT_FALSE(FileExists(kShallInitializeFile));
  EXPECT_CALL(platform_, FileExists(_)).Times(1);
  EXPECT_FALSE(tpm_persistent_state_.ShallInitialize());
  EXPECT_FALSE(tpm_persistent_state_.ShallInitialize());

  // Two identical calls to SetShallInitialize result in one file operation.
  // FileExists() is not called again.
  EXPECT_CALL(platform_, TouchFileDurable(_)).Times(1);
  EXPECT_TRUE(tpm_persistent_state_.SetShallInitialize(true));
  EXPECT_TRUE(FileExists(kShallInitializeFile));
  EXPECT_TRUE(tpm_persistent_state_.ShallInitialize());
  EXPECT_TRUE(tpm_persistent_state_.SetShallInitialize(true));
  EXPECT_TRUE(tpm_persistent_state_.ShallInitialize());

  // Two identical calls to SetShallInitialize result in one file operation.
  // FileExists() is not called again.
  EXPECT_CALL(platform_, DeleteFileDurable(_, _)).Times(1);
  EXPECT_TRUE(tpm_persistent_state_.SetShallInitialize(false));
  EXPECT_FALSE(FileExists(kShallInitializeFile));
  EXPECT_FALSE(tpm_persistent_state_.ShallInitialize());
  EXPECT_TRUE(tpm_persistent_state_.SetShallInitialize(false));
  EXPECT_FALSE(tpm_persistent_state_.ShallInitialize());
}

}  // namespace cryptohome
