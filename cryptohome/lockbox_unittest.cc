// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for Lockbox.

#include "cryptohome/lockbox.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/process/process_mock.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_lockbox.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

namespace cryptohome {
using brillo::SecureBlob;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

// Provides a test fixture for ensuring Lockbox-flows work as expected.
//
// Multiple helpers are included to ensure tests are starting from the same
// baseline for difference scenarios, such as first boot or all-other-normal
// boots.
class LockboxTest : public ::testing::TestWithParam<Tpm::TpmVersion> {
 public:
  LockboxTest() : lockbox_(nullptr, 0xdeadbeef) {}
  ~LockboxTest() override = default;

  void SetUp() override {
    ON_CALL(tpm_, GetVersion()).WillByDefault(Return(GetParam()));

    // Create the OOBE data to reuse for post-boot tests.
    // This generates the expected NVRAM value and serialized file data.
    file_data_.assign(kFileData, kFileData + strlen(kFileData));
    lockbox_.set_tpm(&tpm_);
    lockbox_.set_process(&process_);
  }

  uint32_t GetExpectedNvramSpaceFlags() {
    switch (GetParam()) {
      case Tpm::TpmVersion::TPM_1_2:
        return Tpm::kTpmNvramWriteDefine | Tpm::kTpmNvramBindToPCR0;
      case Tpm::TpmVersion::TPM_2_0:
        return Tpm::kTpmNvramWriteDefine;
      case Tpm::TpmVersion::TPM_UNKNOWN:
        break;
    }
    NOTREACHED();
    return 0;
  }

  static const char* kFileData;
  Lockbox lockbox_;
  NiceMock<MockTpm> tpm_;
  NiceMock<brillo::ProcessMock> process_;
  brillo::Blob file_data_;

 private:
  DISALLOW_COPY_AND_ASSIGN(LockboxTest);
};

const char* LockboxTest::kFileData = "42";

TEST_P(LockboxTest, ResetTpmUnavailable) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(false));

  LockboxError error;
  EXPECT_FALSE(lockbox_.Reset(&error));
  EXPECT_EQ(error, LockboxError::kTpmUnavailable);
}

TEST_P(LockboxTest, ResetNoNvramSpace) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsNvramDefined(0xdeadbeef)).WillOnce(Return(false));

  LockboxError error;
  EXPECT_FALSE(lockbox_.Reset(&error));
  EXPECT_EQ(error, LockboxError::kNvramSpaceAbsent);
}

TEST_P(LockboxTest, ResetExistingSpaceUnlocked) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsNvramDefined(0xdeadbeef)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramLocked(0xdeadbeef)).WillOnce(Return(false));

  LockboxError error;
  EXPECT_TRUE(lockbox_.Reset(&error));
}

TEST_P(LockboxTest, ResetExistingSpaceLocked) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsNvramDefined(0xdeadbeef)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramLocked(0xdeadbeef)).WillOnce(Return(true));

  LockboxError error;
  EXPECT_FALSE(lockbox_.Reset(&error));
  EXPECT_EQ(error, LockboxError::kNvramInvalid);
}

TEST_P(LockboxTest, ResetCreateSpace) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

  // Make the owner password available.
  static const char* kOwnerPassword = "sup";
  brillo::SecureBlob pw;
  pw.assign(kOwnerPassword, kOwnerPassword + strlen(kOwnerPassword));
  EXPECT_CALL(tpm_, GetOwnerPassword(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(pw), Return(true)));

  // No pre-existing space.
  EXPECT_CALL(tpm_, IsNvramDefined(0xdeadbeef)).WillOnce(Return(false));

  // Create the new space.
  EXPECT_CALL(tpm_,
              DefineNvram(0xdeadbeef,
                          LockboxContents::GetNvramSize(NvramVersion::kDefault),
                          GetExpectedNvramSpaceFlags()))
      .WillOnce(Return(true));

  LockboxError error;
  EXPECT_TRUE(lockbox_.Reset(&error));
}

TEST_P(LockboxTest, ResetCreateSpacePreexisting) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

  // Make the owner password available.
  static const char* kOwnerPassword = "sup";
  brillo::SecureBlob pw;
  pw.assign(kOwnerPassword, kOwnerPassword + strlen(kOwnerPassword));
  EXPECT_CALL(tpm_, GetOwnerPassword(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(pw), Return(true)));

  // Pre-existing space, expect the space to get destroyed.
  EXPECT_CALL(tpm_, IsNvramDefined(0xdeadbeef)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, DestroyNvram(0xdeadbeef)).WillOnce(Return(true));

  // Create the new space.
  EXPECT_CALL(tpm_,
              DefineNvram(0xdeadbeef,
                          LockboxContents::GetNvramSize(NvramVersion::kDefault),
                          GetExpectedNvramSpaceFlags()))
      .WillOnce(Return(true));

  LockboxError error;
  EXPECT_TRUE(lockbox_.Reset(&error));
}

TEST_P(LockboxTest, ResetNoOwnerAuth) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, GetOwnerPassword(_)).WillRepeatedly(Return(false));

  LockboxError error;
  EXPECT_FALSE(lockbox_.Reset(&error));
  EXPECT_EQ(error, LockboxError::kNvramSpaceAbsent);
}

TEST_P(LockboxTest, StoreOk) {
  brillo::SecureBlob key_material;
  switch (GetParam()) {
    case Tpm::TpmVersion::TPM_1_2:
      key_material.assign(static_cast<size_t>(NvramVersion::kDefault), 'A');
      break;
    case Tpm::TpmVersion::TPM_2_0:
      key_material.assign(static_cast<size_t>(NvramVersion::kDefault), 0);
      break;
    case Tpm::TpmVersion::TPM_UNKNOWN:
      NOTREACHED();
      break;
  }

  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

  // Destroy calls with no file or existing NVRAM space.
  EXPECT_CALL(tpm_, IsNvramDefined(0xdeadbeef)).WillOnce(Return(true));
  {
    InSequence s;
    EXPECT_CALL(tpm_, IsNvramLocked(0xdeadbeef)).WillOnce(Return(false));
    EXPECT_CALL(tpm_, GetNvramSize(0xdeadbeef))
        .WillOnce(
            Return(LockboxContents::GetNvramSize(NvramVersion::kDefault)));
    EXPECT_CALL(tpm_, GetRandomDataSecureBlob(key_material.size(), _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(key_material), Return(true)));
    EXPECT_CALL(tpm_, WriteNvram(0xdeadbeef, _)).WillOnce(Return(true));
    EXPECT_CALL(tpm_, WriteLockNvram(0xdeadbeef)).WillRepeatedly(Return(true));
    EXPECT_CALL(tpm_, IsNvramLocked(0xdeadbeef)).WillOnce(Return(true));
  }
  EXPECT_CALL(process_, Reset(0)).Times(1);
  EXPECT_CALL(process_, AddArg("/usr/sbin/mount-encrypted")).Times(1);
  EXPECT_CALL(process_, AddArg("finalize")).Times(1);
  EXPECT_CALL(
      process_,
      AddArg(CryptoLib::SecureBlobToHex(CryptoLib::Sha256(key_material))))
      .Times(1);
  EXPECT_CALL(process_, BindFd(_, 1)).Times(1);
  EXPECT_CALL(process_, BindFd(_, 2)).Times(1);
  EXPECT_CALL(process_, Run()).WillOnce(Return(0));

  LockboxError error;
  EXPECT_TRUE(lockbox_.Store(file_data_, &error));
}

TEST_P(LockboxTest, StoreLockedNvram) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsNvramDefined(0xdeadbeef)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramLocked(0xdeadbeef)).WillOnce(Return(true));

  LockboxError error;
  EXPECT_FALSE(lockbox_.Store(file_data_, &error));
  EXPECT_EQ(error, LockboxError::kNvramInvalid);
}

TEST_P(LockboxTest, StoreUnlockedNvramSizeBad) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

  EXPECT_CALL(tpm_, IsNvramDefined(0xdeadbeef)).WillOnce(Return(true));
  EXPECT_CALL(tpm_, IsNvramLocked(0xdeadbeef)).WillOnce(Return(false));
  EXPECT_CALL(tpm_, GetNvramSize(0xdeadbeef)).WillOnce(Return(0));

  LockboxError error;
  EXPECT_FALSE(lockbox_.Store(file_data_, &error));
  EXPECT_EQ(error, LockboxError::kNvramInvalid);
}

TEST_P(LockboxTest, StoreNoNvram) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

  EXPECT_CALL(tpm_, IsNvramDefined(0xdeadbeef)).WillOnce(Return(false));

  LockboxError error;
  EXPECT_FALSE(lockbox_.Store(file_data_, &error));
  EXPECT_EQ(error, LockboxError::kNvramInvalid);
}

TEST_P(LockboxTest, StoreTpmNotReady) {
  EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(false));

  LockboxError error;
  EXPECT_FALSE(lockbox_.Store(file_data_, &error));
  EXPECT_EQ(error, LockboxError::kNvramInvalid);
}

INSTANTIATE_TEST_SUITE_P(LockboxTestTpm12,
                         LockboxTest,
                         testing::Values(Tpm::TpmVersion::TPM_1_2));
INSTANTIATE_TEST_SUITE_P(LockboxTestTpm20,
                         LockboxTest,
                         testing::Values(Tpm::TpmVersion::TPM_2_0));

class LockboxContentsTest : public testing::Test {
 public:
  LockboxContentsTest() = default;

  void GenerateNvramData(NvramVersion version, brillo::SecureBlob* nvram_data) {
    std::unique_ptr<LockboxContents> contents =
        LockboxContents::New(LockboxContents::GetNvramSize(version));
    ASSERT_TRUE(contents);
    ASSERT_TRUE(contents->SetKeyMaterial(
        brillo::SecureBlob(contents->key_material_size(), 'A')));
    ASSERT_TRUE(contents->Protect({42}));
    ASSERT_TRUE(contents->Encode(nvram_data));
  }

  void LoadAndVerify(const brillo::SecureBlob& nvram_data,
                     const brillo::Blob& data,
                     LockboxContents::VerificationResult expected_result) {
    std::unique_ptr<LockboxContents> contents =
        LockboxContents::New(nvram_data.size());
    ASSERT_TRUE(contents);
    ASSERT_TRUE(contents->Decode(nvram_data));
    EXPECT_EQ(expected_result, contents->Verify(data));
  }
};

TEST_F(LockboxContentsTest, LoadAndVerifyOkTpmDefault) {
  brillo::SecureBlob nvram_data;
  ASSERT_NO_FATAL_FAILURE(
      GenerateNvramData(NvramVersion::kDefault, &nvram_data));
  LoadAndVerify(nvram_data, {42}, LockboxContents::VerificationResult::kValid);
}

TEST_F(LockboxContentsTest, LoadAndVerifyOkTpmV1) {
  brillo::SecureBlob nvram_data;
  ASSERT_NO_FATAL_FAILURE(
      GenerateNvramData(NvramVersion::kVersion1, &nvram_data));
  LoadAndVerify(nvram_data, {42}, LockboxContents::VerificationResult::kValid);
}

TEST_F(LockboxContentsTest, LoadAndVerifyOkTpmV2) {
  brillo::SecureBlob nvram_data;
  ASSERT_NO_FATAL_FAILURE(
      GenerateNvramData(NvramVersion::kVersion2, &nvram_data));
  LoadAndVerify(nvram_data, {42}, LockboxContents::VerificationResult::kValid);
}

TEST_F(LockboxContentsTest, LoadAndVerifyBadSize) {
  SecureBlob nvram_data;
  ASSERT_NO_FATAL_FAILURE(
      GenerateNvramData(NvramVersion::kVersion2, &nvram_data));

  // Change the expected file size to 0.
  nvram_data[0] = 0;
  nvram_data[1] = 0;
  nvram_data[2] = 0;
  nvram_data[3] = 0;

  LoadAndVerify(nvram_data, {42},
                LockboxContents::VerificationResult::kSizeMismatch);
}

TEST_F(LockboxContentsTest, LoadAndVerifyBadHash) {
  SecureBlob nvram_data;
  ASSERT_NO_FATAL_FAILURE(
      GenerateNvramData(NvramVersion::kVersion2, &nvram_data));

  // Invalidate the hash.
  nvram_data.back() ^= 0xff;

  LoadAndVerify(nvram_data, {42},
                LockboxContents::VerificationResult::kHashMismatch);
}

TEST_F(LockboxContentsTest, LoadAndVerifyBadData) {
  SecureBlob nvram_data;
  ASSERT_NO_FATAL_FAILURE(
      GenerateNvramData(NvramVersion::kVersion2, &nvram_data));
  LoadAndVerify(nvram_data, {17},
                LockboxContents::VerificationResult::kHashMismatch);
}

}  // namespace cryptohome
