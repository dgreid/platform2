// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_block.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <gtest/gtest.h>

#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/mock_le_credential_backend.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_tpm_init.h"
#include "cryptohome/pin_weaver_auth_block.h"
#include "cryptohome/tpm_auth_block.h"
#include "cryptohome/vault_keyset.h"

using ::testing::_;
using ::testing::Exactly;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {

TEST(PinWeaverAuthBlockTest, DeriveTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(CryptoLib::DeriveSecretsSCrypt(vault_key, salt, {&le_secret}));

  NiceMock<MockLECredentialManager> le_cred_manager;

  ON_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(Return(LE_CRED_SUCCESS));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));

  PinWeaverAuthBlock auth_block(&le_cred_manager);

  // Construct the vault keyset.
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(salt.data(), salt.size());
  serialized.set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());
  serialized.set_le_label(0);
  serialized.set_le_fek_iv(fek_iv.data(), fek_iv.size());

  CryptoError error;
  KeyBlobs key_blobs;
  AuthInput user_input = {vault_key};
  AuthBlockState auth_state = {
      base::make_optional<SerializedVaultKeyset>(std::move(serialized))};
  EXPECT_TRUE(auth_block.Derive(user_input, auth_state, &key_blobs, &error));

  // Set expectations of the key blobs.
  EXPECT_NE(key_blobs.reset_secret, base::nullopt);
  EXPECT_NE(key_blobs.authorization_data_iv, base::nullopt);
  EXPECT_NE(key_blobs.chaps_iv, base::nullopt);
  EXPECT_NE(key_blobs.vkk_iv, base::nullopt);

  // PinWeaver should always use unique IVs.
  EXPECT_NE(key_blobs.chaps_iv.value(), key_blobs.vkk_iv.value());
  EXPECT_NE(key_blobs.authorization_data_iv.value(), key_blobs.vkk_iv.value());
}

TEST(PinWeaverAuthBlockTest, CheckCredentialFailureTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');
  brillo::SecureBlob chaps_iv(kAesBlockSize, 'F');
  brillo::SecureBlob fek_iv(kAesBlockSize, 'X');

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  ASSERT_TRUE(CryptoLib::DeriveSecretsSCrypt(vault_key, salt, {&le_secret}));

  NiceMock<MockLECredentialManager> le_cred_manager;

  ON_CALL(le_cred_manager, CheckCredential(_, _, _, _))
      .WillByDefault(Return(LE_CRED_ERROR_INVALID_LE_SECRET));
  EXPECT_CALL(le_cred_manager, CheckCredential(_, le_secret, _, _))
      .Times(Exactly(1));

  PinWeaverAuthBlock auth_block(&le_cred_manager);

  // Construct the vault keyset.
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_salt(salt.data(), salt.size());
  serialized.set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());
  serialized.set_le_label(0);
  serialized.set_le_fek_iv(fek_iv.data(), fek_iv.size());

  CryptoError error;
  KeyBlobs key_blobs;
  AuthInput user_input = {vault_key};
  AuthBlockState auth_state = {
      base::make_optional<SerializedVaultKeyset>(std::move(serialized))};
  EXPECT_FALSE(auth_block.Derive(user_input, auth_state, &key_blobs, &error));
  EXPECT_EQ(CryptoError::CE_LE_INVALID_SECRET, error);
}

TEST(TPMAuthBlockTest, DecryptBoundToPcrTest) {
  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob vkk_key;

  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  ASSERT_TRUE(CryptoLib::DeriveSecretsSCrypt(vault_key, salt, {&pass_blob}));

  NiceMock<MockTpm> tpm;
  NiceMock<MockTpmInit> tpm_init;
  EXPECT_CALL(tpm, UnsealWithAuthorization(_, _, pass_blob, _, _))
      .Times(Exactly(1));

  CryptoError error = CryptoError::CE_NONE;
  TpmAuthBlock tpm_auth_block(/*is_pcr_extended=*/false, &tpm, &tpm_init);
  EXPECT_TRUE(tpm_auth_block.DecryptTpmBoundToPcr(vault_key, tpm_key, salt,
                                                  &error, &vkk_iv, &vkk_key));
  EXPECT_EQ(CryptoError::CE_NONE, error);
}

TEST(TPMAuthBlockTest, DecryptNotBoundToPcrTest) {
  // Set up a SerializedVaultKeyset. In this case, it is only used to check the
  // flags and password_rounds.
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::SCRYPT_DERIVED);

  brillo::SecureBlob vault_key(20, 'C');
  brillo::SecureBlob tpm_key(20, 'B');
  brillo::SecureBlob salt(PKCS5_SALT_LEN, 'A');

  brillo::SecureBlob vkk_key;
  brillo::SecureBlob vkk_iv(kDefaultAesKeySize);
  brillo::SecureBlob aes_key(kDefaultAesKeySize);

  ASSERT_TRUE(CryptoLib::DeriveSecretsSCrypt(vault_key, salt, {&aes_key}));

  NiceMock<MockTpm> tpm;
  NiceMock<MockTpmInit> tpm_init;
  EXPECT_CALL(tpm, DecryptBlob(_, tpm_key, aes_key, _, _)).Times(Exactly(1));

  CryptoError error = CryptoError::CE_NONE;
  TpmAuthBlock tpm_auth_block(/*is_pcr_extended=*/false, &tpm, &tpm_init);
  EXPECT_TRUE(tpm_auth_block.DecryptTpmNotBoundToPcr(
      serialized, vault_key, tpm_key, salt, &error, &vkk_iv, &vkk_key));
  EXPECT_EQ(CryptoError::CE_NONE, error);
}

TEST(TpmAuthBlockTest, DeriveTest) {
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::PCR_BOUND |
                       SerializedVaultKeyset::SCRYPT_DERIVED);

  brillo::SecureBlob key(20, 'B');
  brillo::SecureBlob tpm_key(20, 'C');
  std::string salt(PKCS5_SALT_LEN, 'A');

  serialized.set_salt(salt);
  serialized.set_tpm_key(tpm_key.data(), tpm_key.size());

  // Make sure TpmAuthBlock calls DecryptTpmBoundToPcr in this case.
  NiceMock<MockTpm> tpm;
  NiceMock<MockTpmInit> tpm_init;
  EXPECT_CALL(tpm, UnsealWithAuthorization(_, _, _, _, _)).Times(Exactly(1));

  TpmAuthBlock auth_block(/*is_pcr_extended=*/false, &tpm, &tpm_init);

  KeyBlobs key_out_data;
  AuthInput user_input = {key};
  AuthBlockState auth_state = {
      base::make_optional<SerializedVaultKeyset>(std::move(serialized))};
  CryptoError error;
  EXPECT_TRUE(auth_block.Derive(user_input, auth_state, &key_out_data, &error));

  // Assert that the returned key blobs isn't uninitialized.
  EXPECT_NE(key_out_data.vkk_iv, base::nullopt);
  EXPECT_NE(key_out_data.vkk_key, base::nullopt);
  EXPECT_EQ(key_out_data.vkk_iv.value(), key_out_data.chaps_iv.value());
  EXPECT_EQ(key_out_data.vkk_iv.value(),
            key_out_data.authorization_data_iv.value());
}

}  // namespace cryptohome
