// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for TpmInit.

#include "cryptohome/tpm_init.h"

#include <map>
#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {

extern const base::FilePath kTpmOwnedFile;
const base::FilePath kTpmStatusFile("/mnt/stateful_partition/.tpm_status");
const base::FilePath kDefaultCryptohomeKeyFile("/home/.shadow/cryptohome.key");
const TpmKeyHandle kTestKeyHandle = 17;  // any non-zero value

// Tests that need to do more setup work before calling Service::Initialize can
// use this instead of ServiceTest.
class TpmInitTest : public ::testing::Test {
 public:
  TpmInitTest()
      : is_tpm_owned_(false),
        is_tpm_being_owned_(false),
        is_tpm_initialized_(false),
        tpm_init_(&tpm_, &platform_) {}
  TpmInitTest(const TpmInitTest&) = delete;
  TpmInitTest& operator=(const TpmInitTest&) = delete;

  ~TpmInitTest() override = default;

  // Default mock implementations for |tpm_| methods.
  // For TPM-related flags: enabled is always true, other flags are settable.
  bool IsTpmOwned() const { return is_tpm_owned_; }
  void SetIsTpmOwned(bool is_tpm_owned) { is_tpm_owned_ = is_tpm_owned; }
  bool IsTpmBeingOwned() const { return is_tpm_being_owned_; }
  void SetIsTpmBeingOwned(bool is_tpm_being_owned) {
    is_tpm_being_owned_ = is_tpm_being_owned;
  }
  bool IsTpmInitialized() { return is_tpm_initialized_; }
  void SetIsTpmInitialized(bool is_tpm_initialized) {
    is_tpm_initialized_ = is_tpm_initialized;
  }
  bool PerformTpmEnabledOwnedCheck(bool* is_enabled, bool* is_owned) {
    *is_enabled = true;
    *is_owned = is_tpm_owned_;
    return true;
  }
  bool GetRandomDataBlob(size_t length, brillo::Blob* data) const {
    data->resize(length, 0);
    return true;
  }
  bool GetRandomDataSecureBlob(size_t length, brillo::SecureBlob* data) const {
    data->resize(length, 0);
    return true;
  }

  // Default mock implementations for |platform_| methods.
  // Files are emulated using |files_| map: <file path> -> <file contents>.
  bool FileExists(const base::FilePath& path) const {
    return files_.count(path) > 0;
  }
  bool FileMove(const base::FilePath& from, const base::FilePath& to) {
    if (!FileExists(from)) {
      return false;
    }
    if (FileExists(to)) {
      return false;
    }
    files_[to] = files_[from];
    return FileDelete(from, false);
  }
  bool FileDelete(const base::FilePath& path, bool /* recursive */) {
    return files_.erase(path) == 1;
  }
  bool FileTouch(const base::FilePath& path) {
    files_.emplace(path, brillo::Blob());
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
  bool FileReadToString(const base::FilePath& path, std::string* str) {
    brillo::Blob blob;
    if (!FileRead(path, &blob)) {
      return false;
    }
    str->assign(reinterpret_cast<char*>(blob.data()), blob.size());
    return true;
  }
  bool FileReadToSecureBlob(const base::FilePath& path,
                            brillo::SecureBlob* sblob) {
    if (!FileExists(path)) {
      return false;
    }
    brillo::Blob temp = files_[path];
    sblob->assign(temp.begin(), temp.end());
    return true;
  }
  bool FileWrite(const base::FilePath& path, const brillo::Blob& blob) {
    files_[path] = blob;
    return true;
  }
  bool FileWriteFromSecureBlob(const base::FilePath& path,
                               const brillo::SecureBlob& sblob) {
    brillo::Blob blob(sblob.begin(), sblob.end());
    files_[path] = blob;
    return true;
  }
  bool FileWriteAtomic(const base::FilePath& path,
                       const brillo::SecureBlob& sblob,
                       mode_t /* mode */) {
    return FileWriteFromSecureBlob(path, sblob);
  }
  bool FileWriteString(const base::FilePath& path, const std::string& str) {
    brillo::Blob blob(str.begin(), str.end());
    return FileWrite(path, blob);
  }

  void SetTpmStatus(const TpmStatus& tpm_status) {
    brillo::Blob file_data(tpm_status.ByteSizeLong());
    tpm_status.SerializeWithCachedSizesToArray(file_data.data());
    EXPECT_TRUE(FileWrite(kTpmStatusFile, file_data));
  }

  void GetTpmStatus(TpmStatus* tpm_status) {
    brillo::Blob file_data;
    tpm_status->Clear();
    EXPECT_TRUE(FileRead(kTpmStatusFile, &file_data));
    EXPECT_TRUE(tpm_status->ParseFromArray(file_data.data(), file_data.size()));
  }

  void SetTpmReady() {
    SetIsTpmOwned(true);
    SetIsTpmBeingOwned(false);
    FileTouch(kTpmOwnedFile);
    ASSERT_TRUE(tpm_init_.IsTpmReady());
  }

  void SetUp() override {
    ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
    ON_CALL(tpm_, IsOwned())
        .WillByDefault(Invoke(this, &TpmInitTest::IsTpmOwned));
    ON_CALL(tpm_, SetIsOwned(_))
        .WillByDefault(Invoke(this, &TpmInitTest::SetIsTpmOwned));
    ON_CALL(tpm_, IsBeingOwned())
        .WillByDefault(Invoke(this, &TpmInitTest::IsTpmBeingOwned));
    ON_CALL(tpm_, SetIsBeingOwned(_))
        .WillByDefault(Invoke(this, &TpmInitTest::SetIsTpmBeingOwned));
    ON_CALL(tpm_, IsInitialized())
        .WillByDefault(Invoke(this, &TpmInitTest::IsTpmInitialized));
    ON_CALL(tpm_, SetIsInitialized(_))
        .WillByDefault(Invoke(this, &TpmInitTest::SetIsTpmInitialized));
    ON_CALL(tpm_, PerformEnabledOwnedCheck(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::PerformTpmEnabledOwnedCheck));

    ON_CALL(tpm_, GetRandomDataBlob(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::GetRandomDataBlob));
    ON_CALL(tpm_, GetRandomDataSecureBlob(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::GetRandomDataSecureBlob));

    ON_CALL(platform_, FileExists(_))
        .WillByDefault(Invoke(this, &TpmInitTest::FileExists));
    ON_CALL(platform_, Move(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::FileMove));
    ON_CALL(platform_, DeleteFile(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::FileDelete));
    ON_CALL(platform_, DeleteFileDurable(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::FileDelete));
    ON_CALL(platform_, TouchFileDurable(_))
        .WillByDefault(Invoke(this, &TpmInitTest::FileTouch));
    ON_CALL(platform_, GetFileSize(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::GetFileSize));
    ON_CALL(platform_, ReadFile(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::FileRead));
    ON_CALL(platform_, ReadFileToSecureBlob(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::FileReadToSecureBlob));
    ON_CALL(platform_, WriteSecureBlobToFile(_, _))
        .WillByDefault(Invoke(this, &TpmInitTest::FileWriteFromSecureBlob));
    ON_CALL(platform_, WriteSecureBlobToFileAtomic(_, _, _))
        .WillByDefault(Invoke(this, &TpmInitTest::FileWriteAtomic));
    ON_CALL(platform_, WriteSecureBlobToFileAtomicDurable(_, _, _))
        .WillByDefault(Invoke(this, &TpmInitTest::FileWriteAtomic));
    ON_CALL(platform_, DataSyncFile(_)).WillByDefault(Return(true));
  }

  void TearDown() override {}

 protected:
  bool is_tpm_owned_;
  bool is_tpm_being_owned_;
  bool is_tpm_initialized_;
  std::map<base::FilePath, brillo::Blob> files_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockPlatform> platform_;

  // Declare tpm_init_ last, so it gets destroyed before all the mocks.
  TpmInit tpm_init_;
};

MATCHER_P(HasStoredCryptohomeKey, str, "") {
  std::string stored_key;
  if (!arg->FileReadToString(kDefaultCryptohomeKeyFile, &stored_key)) {
    *result_listener << "has no stored cryptohome key";
    return false;
  }
  if (stored_key != str) {
    *result_listener << "has stored cryptohome key \"" << stored_key << "\"";
    return false;
  }
  return true;
}

MATCHER_P(HasLoadedCryptohomeKey, handle, "") {
  if (!arg->HasCryptohomeKey()) {
    *result_listener << "has no loaded cryptohome key";
    return false;
  }
  TpmKeyHandle loaded_handle = arg->GetCryptohomeKey();
  if (loaded_handle != handle) {
    *result_listener << "has loaded cryptohome key " << loaded_handle;
    return false;
  }
  return true;
}

MATCHER(HasNoLoadedCryptohomeKey, "") {
  TpmKeyHandle loaded_handle = arg->GetCryptohomeKey();
  *result_listener << "has loaded cryptohome key " << loaded_handle;
  return !arg->HasCryptohomeKey() && loaded_handle == kInvalidKeyHandle;
}

ACTION_P(GenerateWrappedKey, wrapped_key) {
  *arg2 = brillo::SecureBlob(wrapped_key);
  return true;
}

ACTION_P(LoadWrappedKeyToHandle, handle) {
  arg1->reset(nullptr, handle);
  return Tpm::kTpmRetryNone;
}

TEST_F(TpmInitTest, AlreadyOwnedSuccess) {
  bool took_ownership = false;
  SetIsTpmOwned(true);
  FileTouch(kTpmOwnedFile);
  ASSERT_TRUE(tpm_init_.TakeOwnership(&took_ownership));
  ASSERT_FALSE(took_ownership);
}

TEST_F(TpmInitTest, TakeOwnershipMonolithicSuccess) {
  // Setup TPM.
  EXPECT_CALL(tpm_, SetIsOwned(false)).Times(1);
  EXPECT_CALL(tpm_, SetIsEnabled(true)).Times(1);
  EXPECT_TRUE(tpm_init_.SetupTpm(false));
  EXPECT_TRUE(IsTpmInitialized());
  EXPECT_FALSE(IsTpmOwned());
  EXPECT_FALSE(FileExists(kTpmOwnedFile));
  ::testing::Mock::VerifyAndClearExpectations(&tpm_);

  // Take Ownership.
  EXPECT_CALL(tpm_, DoesUseTpmManager()).WillOnce(Return(false));
  EXPECT_CALL(tpm_, IsEndorsementKeyAvailable())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, CreateEndorsementKey()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, TakeOwnership(_, _)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, TestTpmAuth(_)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, InitializeSrk(_)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, ChangeOwnerPassword(_, _)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, SetOwnerPassword(_)).Times(1);

  bool took_ownership = false;
  EXPECT_TRUE(tpm_init_.TakeOwnership(&took_ownership));
  EXPECT_TRUE(took_ownership);
  EXPECT_TRUE(IsTpmOwned());
  EXPECT_FALSE(IsTpmBeingOwned());
  EXPECT_TRUE(FileExists(kTpmOwnedFile));
}

TEST_F(TpmInitTest, TakeOwnershipDistributedSuccess) {
  // Set initial TPM states.
  SetIsTpmOwned(false);
  SetIsTpmInitialized(true);
  SetIsTpmBeingOwned(false);

  // Take Ownership.
  EXPECT_CALL(tpm_, DoesUseTpmManager()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsEndorsementKeyAvailable())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, CreateEndorsementKey()).WillOnce(Return(true));
  EXPECT_CALL(tpm_, TakeOwnership(_, _)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, TestTpmAuth(_)).Times(0);
  EXPECT_CALL(tpm_, InitializeSrk(_)).Times(0);
  EXPECT_CALL(tpm_, ChangeOwnerPassword(_, _)).Times(0);
  EXPECT_CALL(tpm_, SetOwnerPassword(_)).Times(0);

  bool took_ownership = false;
  EXPECT_TRUE(tpm_init_.TakeOwnership(&took_ownership));
  EXPECT_TRUE(took_ownership);
  EXPECT_TRUE(IsTpmOwned());
  EXPECT_FALSE(IsTpmBeingOwned());
  EXPECT_TRUE(FileExists(kTpmOwnedFile));
}

TEST_F(TpmInitTest, RemoveTpmOwnerDependencySuccess) {
  TpmStatus tpm_status;
  tpm_status.set_flags(TpmStatus::ATTESTATION_NEEDS_OWNER |
                       TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER);
  SetTpmStatus(tpm_status);
  auto dependency = TpmPersistentState::TpmOwnerDependency::kAttestation;
  EXPECT_CALL(tpm_, RemoveOwnerDependency(dependency)).WillOnce(Return(true));
  EXPECT_CALL(platform_,
              WriteSecureBlobToFileAtomicDurable(kTpmStatusFile, _, _))
      .Times(1);
  tpm_init_.RemoveTpmOwnerDependency(dependency);
  GetTpmStatus(&tpm_status);
  EXPECT_EQ(TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER, tpm_status.flags());
}

TEST_F(TpmInitTest, RemoveTpmOwnerDependencyAlreadyRemoved) {
  TpmStatus tpm_status;
  tpm_status.set_flags(TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER);
  SetTpmStatus(tpm_status);
  auto dependency = TpmPersistentState::TpmOwnerDependency::kAttestation;
  EXPECT_CALL(tpm_, RemoveOwnerDependency(dependency)).WillOnce(Return(true));
  EXPECT_CALL(platform_,
              WriteSecureBlobToFileAtomicDurable(kTpmStatusFile, _, _))
      .Times(0);
  tpm_init_.RemoveTpmOwnerDependency(dependency);
  GetTpmStatus(&tpm_status);
  EXPECT_EQ(TpmStatus::INSTALL_ATTRIBUTES_NEEDS_OWNER, tpm_status.flags());
}

TEST_F(TpmInitTest, RemoveTpmOwnerDependencyTpmFailure) {
  TpmStatus tpm_status;
  tpm_status.set_flags(TpmStatus::ATTESTATION_NEEDS_OWNER);
  SetTpmStatus(tpm_status);
  auto dependency = TpmPersistentState::TpmOwnerDependency::kAttestation;
  EXPECT_CALL(tpm_, RemoveOwnerDependency(dependency)).WillOnce(Return(false));
  EXPECT_CALL(platform_,
              WriteSecureBlobToFileAtomicDurable(kTpmStatusFile, _, _))
      .Times(0);
  tpm_init_.RemoveTpmOwnerDependency(dependency);
  GetTpmStatus(&tpm_status);
  EXPECT_EQ(TpmStatus::ATTESTATION_NEEDS_OWNER, tpm_status.flags());
}

TEST_F(TpmInitTest, RemoveTpmOwnerDependencyNoTpmStatus) {
  auto dependency = TpmPersistentState::TpmOwnerDependency::kAttestation;
  EXPECT_CALL(tpm_, RemoveOwnerDependency(dependency)).WillOnce(Return(true));
  EXPECT_CALL(platform_,
              WriteSecureBlobToFileAtomicDurable(kTpmStatusFile, _, _))
      .Times(0);
  tpm_init_.RemoveTpmOwnerDependency(dependency);
}

TEST_F(TpmInitTest, LoadCryptohomeKeySuccess) {
  SetIsTpmInitialized(true);
  FileTouch(kDefaultCryptohomeKeyFile);
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(LoadWrappedKeyToHandle(kTestKeyHandle));
  tpm_init_.SetupTpm(true);
  EXPECT_THAT(&tpm_init_, HasLoadedCryptohomeKey(kTestKeyHandle));
}

TEST_F(TpmInitTest, LoadCryptohomeKeyTransientFailure) {
  // Transient failure on the first attempt leads to key not being loaded.
  // But the key is not re-created. Success on the second attempt loads the
  // old key.
  SetIsTpmInitialized(true);
  FileWriteString(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(Return(Tpm::kTpmRetryCommFailure))
      .WillOnce(LoadWrappedKeyToHandle(kTestKeyHandle));
  EXPECT_CALL(tpm_, WrapRsaKey(_, _, _)).Times(0);
  tpm_init_.SetupTpm(true);
  EXPECT_THAT(&tpm_init_, HasNoLoadedCryptohomeKey());
  tpm_init_.SetupTpm(true);
  EXPECT_THAT(&tpm_init_, HasLoadedCryptohomeKey(kTestKeyHandle));
  EXPECT_THAT(this, HasStoredCryptohomeKey("old-key"));
}

TEST_F(TpmInitTest, ReCreateCryptohomeKeyAfterLoadFailure) {
  // Permanent failure while loading the key leads to re-creating, storing
  // and loading the new key.
  SetTpmReady();
  SetIsTpmInitialized(true);
  FileWriteString(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(Return(Tpm::kTpmRetryFailNoRetry))
      .WillOnce(LoadWrappedKeyToHandle(kTestKeyHandle));
  EXPECT_CALL(tpm_, WrapRsaKey(_, _, _))
      .WillOnce(GenerateWrappedKey("new-key"));
  tpm_init_.SetupTpm(true);
  EXPECT_THAT(&tpm_init_, HasLoadedCryptohomeKey(kTestKeyHandle));
  EXPECT_THAT(this, HasStoredCryptohomeKey("new-key"));
}

TEST_F(TpmInitTest, ReCreateCryptohomeKeyFailureDuringKeyCreation) {
  // Permanent failure while loading the key leads to an attempt to re-create
  // the key. Which fails. So, nothing new is stored or loaded.
  SetTpmReady();
  SetIsTpmInitialized(true);
  FileWriteString(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(Return(Tpm::kTpmRetryFailNoRetry));
  EXPECT_CALL(tpm_, WrapRsaKey(_, _, _)).WillOnce(Return(false));
  tpm_init_.SetupTpm(true);
  EXPECT_THAT(&tpm_init_, HasNoLoadedCryptohomeKey());
  EXPECT_THAT(this, HasStoredCryptohomeKey("old-key"));
}

TEST_F(TpmInitTest, ReCreateCryptohomeKeyFailureDuringKeyLoading) {
  // Permanent failure while loading the key leads to re-creating the key.
  // It is stored. But then loading fails.
  // Still, on the next attempt, the key is loaded, and not re-created again.
  SetTpmReady();
  SetIsTpmInitialized(true);
  FileWriteString(kDefaultCryptohomeKeyFile, "old-key");
  EXPECT_CALL(tpm_, LoadWrappedKey(_, _))
      .WillOnce(Return(Tpm::kTpmRetryFailNoRetry))
      .WillOnce(Return(Tpm::kTpmRetryFailNoRetry))
      .WillOnce(LoadWrappedKeyToHandle(kTestKeyHandle));
  EXPECT_CALL(tpm_, WrapRsaKey(_, _, _))
      .WillOnce(GenerateWrappedKey("new-key"));
  tpm_init_.SetupTpm(true);
  EXPECT_THAT(&tpm_init_, HasNoLoadedCryptohomeKey());
  EXPECT_THAT(this, HasStoredCryptohomeKey("new-key"));
  tpm_init_.SetupTpm(true);
  EXPECT_THAT(&tpm_init_, HasLoadedCryptohomeKey(kTestKeyHandle));
  EXPECT_THAT(this, HasStoredCryptohomeKey("new-key"));
}

TEST_F(TpmInitTest, IsTpmReadyWithOwnedFile) {
  FileTouch(kTpmOwnedFile);

  SetIsTpmOwned(true);
  SetIsTpmBeingOwned(false);
  EXPECT_CALL(tpm_, DoesUseTpmManager()).WillOnce(Return(false));
  EXPECT_TRUE(tpm_init_.IsTpmReady());
  EXPECT_CALL(tpm_, DoesUseTpmManager()).WillOnce(Return(true));
  EXPECT_TRUE(tpm_init_.IsTpmReady());

  EXPECT_CALL(tpm_, DoesUseTpmManager()).Times(0);

  SetIsTpmOwned(false);
  EXPECT_FALSE(tpm_init_.IsTpmReady());

  SetIsTpmOwned(true);
  SetIsTpmBeingOwned(true);
  EXPECT_FALSE(tpm_init_.IsTpmReady());
}

TEST_F(TpmInitTest, IsTpmReadyNoOwnedFile) {
  FileDelete(kTpmOwnedFile, false);
  SetIsTpmOwned(true);
  SetIsTpmBeingOwned(false);

  EXPECT_CALL(tpm_, DoesUseTpmManager()).WillOnce(Return(false));
  EXPECT_FALSE(tpm_init_.IsTpmReady());

  EXPECT_CALL(tpm_, DoesUseTpmManager()).WillOnce(Return(true));
  EXPECT_TRUE(tpm_init_.IsTpmReady());
}

}  // namespace cryptohome
