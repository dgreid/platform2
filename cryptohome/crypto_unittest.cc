// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Crypto.

#include "cryptohome/crypto.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <vector>

#include "cryptohome/attestation.pb.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_le_credential_manager.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_tpm_init.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::Blob;
using brillo::SecureBlob;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

namespace cryptohome {

const char kImageDir[] = "test_image_dir";

// FIPS 180-2 test vectors for SHA-1 and SHA-256
class ShaTestVectors {
 public:
  explicit ShaTestVectors(int type);

  ~ShaTestVectors() {}
  const brillo::Blob* input(int index) const { return &input_[index]; }
  const brillo::SecureBlob* output(int index) const { return &output_[index]; }
  size_t count() const { return 3; }  // sizeof(input_); }

  static const char* kOneBlockMessage;
  static const char* kMultiBlockMessage;
  static const uint8_t kSha1Results[][SHA_DIGEST_LENGTH];
  static const uint8_t kSha256Results[][SHA256_DIGEST_LENGTH];

 private:
  brillo::Blob input_[3];
  brillo::SecureBlob output_[3];
};

const char* ShaTestVectors::kMultiBlockMessage =
    "abcdbcdecdefdefgefghfghighijhijkijkl"
    "jklmklmnlmnomnopnopq";
const char* ShaTestVectors::kOneBlockMessage = "abc";
const uint8_t ShaTestVectors::kSha1Results[][SHA_DIGEST_LENGTH] = {
    {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
     0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d},
    {0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae,
     0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1},
    {0x34, 0xaa, 0x97, 0x3c, 0xd4, 0xc4, 0xda, 0xa4, 0xf6, 0x1e,
     0xeb, 0x2b, 0xdb, 0xad, 0x27, 0x31, 0x65, 0x34, 0x01, 0x6f}};
const uint8_t ShaTestVectors::kSha256Results[][SHA256_DIGEST_LENGTH] = {
    {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
     0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
     0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad},
    {0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26,
     0x93, 0x0c, 0x3e, 0x60, 0x39, 0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff,
     0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1},
    {0xcd, 0xc7, 0x6e, 0x5c, 0x99, 0x14, 0xfb, 0x92, 0x81, 0xa1, 0xc7,
     0xe2, 0x84, 0xd7, 0x3e, 0x67, 0xf1, 0x80, 0x9a, 0x48, 0xa4, 0x97,
     0x20, 0x0e, 0x04, 0x6d, 0x39, 0xcc, 0xc7, 0x11, 0x2c, 0xd0}};

ShaTestVectors::ShaTestVectors(int type) {
  // Since we don't do 512+, we can prep here for all types and
  // don't need to get fancy.
  input_[0].resize(strlen(kOneBlockMessage));
  memcpy(&(input_[0][0]), kOneBlockMessage, input_[0].size());
  input_[1].resize(strlen(kMultiBlockMessage));
  memcpy(&input_[1][0], kMultiBlockMessage, input_[1].size());
  input_[2].assign(1000000, 'a');

  switch (type) {
    case 1:
      for (size_t i = 0; i < count(); ++i) {
        output_[i].resize(SHA_DIGEST_LENGTH);
        memcpy(output_[i].data(), kSha1Results[i], output_[i].size());
      }
      break;
    case 256:
      for (size_t i = 0; i < count(); ++i) {
        output_[i].resize(SHA256_DIGEST_LENGTH);
        memcpy(output_[i].data(), kSha256Results[i], output_[i].size());
      }
      break;
    default:
      CHECK(false) << "Only SHA-256 and SHA-1 are supported";
  }
}

class CryptoTest : public ::testing::Test {
 public:
  CryptoTest() {}
  CryptoTest(const CryptoTest&) = delete;
  CryptoTest& operator=(const CryptoTest&) = delete;

  virtual ~CryptoTest() {}

  static bool FindBlobInBlob(const SecureBlob& haystack,
                             const SecureBlob& needle) {
    if (needle.size() > haystack.size()) {
      return false;
    }
    for (unsigned int start = 0; start <= (haystack.size() - needle.size());
         start++) {
      if (brillo::SecureMemcmp(&haystack[start], needle.data(),
                               needle.size()) == 0) {
        return true;
      }
    }
    return false;
  }

  static void GetSerializedBlob(const SerializedVaultKeyset& serialized,
                                SecureBlob* blob) {
    SecureBlob final_blob(serialized.ByteSizeLong());
    serialized.SerializeWithCachedSizesToArray(
        static_cast<google::protobuf::uint8*>(final_blob.data()));
    blob->swap(final_blob);
  }

  static bool FromSerializedBlob(const SecureBlob& blob,
                                 SerializedVaultKeyset* serialized) {
    return serialized->ParseFromArray(blob.data(), blob.size());
  }

 protected:
  MockPlatform platform_;
};

TEST_F(CryptoTest, EncryptionTest) {
  // Check that EncryptVaultKeyset returns something other than the bytes passed
  Crypto crypto(&platform_);

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob key(20);
  CryptoLib::GetSecureRandom(key.data(), key.size());
  SecureBlob salt(PKCS5_SALT_LEN);
  CryptoLib::GetSecureRandom(salt.data(), salt.size());

  SerializedVaultKeyset serialized;
  ASSERT_TRUE(
      crypto.EncryptVaultKeyset(vault_keyset, key, salt, "", &serialized));

  SecureBlob original;
  ASSERT_TRUE(vault_keyset.ToKeysBlob(&original));
  SecureBlob encrypted;
  GetSerializedBlob(serialized, &encrypted);

  ASSERT_GT(encrypted.size(), 0);
  ASSERT_FALSE(CryptoTest::FindBlobInBlob(encrypted, original));
}

TEST_F(CryptoTest, DecryptionTest) {
  // Check that DecryptVaultKeyset returns the original keyset
  MockPlatform platform;
  Crypto crypto(&platform);

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob key(20);
  CryptoLib::GetSecureRandom(key.data(), key.size());
  SecureBlob salt(PKCS5_SALT_LEN);
  CryptoLib::GetSecureRandom(salt.data(), salt.size());

  SerializedVaultKeyset serialized;
  ASSERT_TRUE(
      crypto.EncryptVaultKeyset(vault_keyset, key, salt, "", &serialized));
  SecureBlob encrypted;
  GetSerializedBlob(serialized, &encrypted);

  ASSERT_TRUE(CryptoTest::FindBlobInBlob(encrypted, salt));

  ASSERT_TRUE(CryptoTest::FromSerializedBlob(encrypted, &serialized));

  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform_, &crypto);
  unsigned int crypt_flags = 0;
  CryptoError crypto_error = CryptoError::CE_NONE;
  ASSERT_TRUE(crypto.DecryptVaultKeyset(
      serialized, key, false /* locked_to_single_user */, &crypt_flags,
      &crypto_error, &new_keyset));

  SecureBlob original_data;
  ASSERT_TRUE(vault_keyset.ToKeysBlob(&original_data));
  SecureBlob new_data;
  ASSERT_TRUE(new_keyset.ToKeysBlob(&new_data));

  EXPECT_EQ(new_data.size(), original_data.size());
  ASSERT_TRUE(CryptoTest::FindBlobInBlob(new_data, original_data));
}

TEST_F(CryptoTest, SaltCreateTest) {
  MockPlatform platform;
  Crypto crypto(&platform);

  // Case 1: No salt exists
  SecureBlob salt;
  SecureBlob salt_written;
  SecureBlob* salt_ptr = &salt_written;
  FilePath salt_path(FilePath(kImageDir).Append("crypto_test_salt"));
  EXPECT_CALL(platform, FileExists(salt_path)).WillOnce(Return(false));
  EXPECT_CALL(platform, WriteSecureBlobToFileAtomicDurable(salt_path, _, _))
      .WillOnce(DoAll(SaveArg<1>(salt_ptr), Return(true)));
  crypto.GetOrCreateSalt(salt_path, 32, false, &salt);

  ASSERT_EQ(32, salt.size());
  EXPECT_EQ(salt.to_string(), std::string(salt_ptr->begin(), salt_ptr->end()));

  // Case 2: Salt exists, but forced
  SecureBlob new_salt;
  salt_written.resize(0);
  salt_ptr = &salt_written;
  EXPECT_CALL(platform, FileExists(salt_path)).WillOnce(Return(true));
  int64_t salt_size = 32;
  EXPECT_CALL(platform, GetFileSize(salt_path, _))
      .WillOnce(DoAll(SetArgPointee<1>(salt_size), Return(true)));
  EXPECT_CALL(platform, WriteSecureBlobToFileAtomicDurable(salt_path, _, _))
      .WillOnce(DoAll(SaveArg<1>(salt_ptr), Return(true)));
  crypto.GetOrCreateSalt(salt_path, 32, true, &new_salt);
  ASSERT_EQ(32, new_salt.size());
  EXPECT_EQ(new_salt.to_string(),
            std::string(salt_ptr->begin(), salt_ptr->end()));

  EXPECT_EQ(salt.size(), new_salt.size());
  EXPECT_FALSE(CryptoTest::FindBlobInBlob(salt, new_salt));

  // TODO(wad): cases not covered: file is 0 bytes, file fails to read,
  //            existing salt is read.
}

TEST_F(CryptoTest, BlobToHexTest) {
  // Check that BlobToHexToBuffer works
  SecureBlob blob_in(256);
  SecureBlob blob_out(512);

  for (int i = 0; i < 256; i++) {
    blob_in[i] = i;
    blob_out[i * 2] = 0;
    blob_out[i * 2 + 1] = 0;
  }

  CryptoLib::SecureBlobToHexToBuffer(blob_in, blob_out.data(), blob_out.size());
  for (int i = 0; i < 256; i++) {
    std::string digits = base::StringPrintf("%02x", i);
    ASSERT_EQ(digits[0], blob_out[i * 2]);
    ASSERT_EQ(digits[1], blob_out[i * 2 + 1]);
  }
}

TEST_F(CryptoTest, TpmStepTest) {
  // Check that the code path changes to support the TPM work
  MockPlatform platform;
  Crypto crypto(&platform);
  NiceMock<MockTpm> tpm;
  NiceMock<MockTpmInit> tpm_init;

  crypto.set_tpm(&tpm);

  SecureBlob vkk_key;
  EXPECT_CALL(tpm, GetVersion()).WillRepeatedly(Return(Tpm::TPM_2_0));
  EXPECT_CALL(tpm, SealToPcrWithAuthorization(_, _, _, _, _))
      .Times(2)  // Once for each valid PCR state.
      .WillRepeatedly(DoAll(SaveArg<1>(&vkk_key), Return(Tpm::kTpmRetryNone)));
  EXPECT_CALL(tpm_init, HasCryptohomeKey())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_init, SetupTpm(true))
      .Times(AtLeast(2));  // One by crypto.Init(), one by crypto.EnsureTpm()
  SecureBlob blob("public key hash");
  EXPECT_CALL(tpm, GetPublicKeyHash(_, _))
      .Times(2)  // Once on Encrypt and once on Decrypt of Vault.
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(blob), Return(Tpm::kTpmRetryNone)));
  EXPECT_CALL(tpm, IsOwned()).WillRepeatedly(Return(true));

  crypto.Init(&tpm_init);

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob key(20);
  CryptoLib::GetSecureRandom(key.data(), key.size());
  SecureBlob salt(PKCS5_SALT_LEN);
  CryptoLib::GetSecureRandom(salt.data(), salt.size());

  SerializedVaultKeyset serialized;
  ASSERT_TRUE(
      crypto.EncryptVaultKeyset(vault_keyset, key, salt, "", &serialized));
  SecureBlob encrypted;
  GetSerializedBlob(serialized, &encrypted);

  ASSERT_TRUE(CryptoTest::FindBlobInBlob(encrypted, salt));

  ASSERT_TRUE(CryptoTest::FromSerializedBlob(encrypted, &serialized));

  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform, &crypto);
  unsigned int crypt_flags = 0;
  CryptoError crypto_error = CryptoError::CE_NONE;

  EXPECT_CALL(tpm, UnsealWithAuthorization(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(vkk_key), Return(Tpm::kTpmRetryNone)));

  ASSERT_TRUE(crypto.DecryptVaultKeyset(
      serialized, key, false /* locked_to_single_user */, &crypt_flags,
      &crypto_error, &new_keyset));

  SecureBlob original_data;
  ASSERT_TRUE(vault_keyset.ToKeysBlob(&original_data));
  SecureBlob new_data;
  ASSERT_TRUE(new_keyset.ToKeysBlob(&new_data));

  EXPECT_EQ(new_data.size(), original_data.size());
  ASSERT_TRUE(CryptoTest::FindBlobInBlob(new_data, original_data));

  // Check that the keyset was indeed wrapped by the TPM, and the
  // keys were derived using scrypt.
  EXPECT_EQ(0, (crypt_flags & SerializedVaultKeyset::SCRYPT_WRAPPED));
  EXPECT_EQ(SerializedVaultKeyset::TPM_WRAPPED,
            (crypt_flags & SerializedVaultKeyset::TPM_WRAPPED));
  EXPECT_EQ(SerializedVaultKeyset::SCRYPT_DERIVED,
            (crypt_flags & SerializedVaultKeyset::SCRYPT_DERIVED));
  EXPECT_EQ(SerializedVaultKeyset::PCR_BOUND,
            (crypt_flags & SerializedVaultKeyset::PCR_BOUND));
}

TEST_F(CryptoTest, Tpm1_2_StepTest) {
  // Check that the code path changes to support the TPM work
  MockPlatform platform;
  Crypto crypto(&platform);
  NiceMock<MockTpm> tpm;
  NiceMock<MockTpmInit> tpm_init;

  crypto.set_tpm(&tpm);

  SecureBlob vkk_key;
  EXPECT_CALL(tpm, GetVersion()).WillRepeatedly(Return(Tpm::TPM_1_2));
  EXPECT_CALL(tpm, EncryptBlob(_, _, _, _))
      .Times(1)
      .WillRepeatedly(DoAll(SaveArg<1>(&vkk_key), Return(Tpm::kTpmRetryNone)));
  EXPECT_CALL(tpm_init, HasCryptohomeKey())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_init, SetupTpm(true))
      .Times(AtLeast(2));  // One by crypto.Init(), one by crypto.EnsureTpm()
  SecureBlob blob("public key hash");
  EXPECT_CALL(tpm, GetPublicKeyHash(_, _))
      .Times(2)  // Once on Encrypt and once on Decrypt of Vault.
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(blob), Return(Tpm::kTpmRetryNone)));
  EXPECT_CALL(tpm, IsOwned()).WillRepeatedly(Return(true));

  crypto.Init(&tpm_init);

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob key(20);
  CryptoLib::GetSecureRandom(key.data(), key.size());
  SecureBlob salt(PKCS5_SALT_LEN);
  CryptoLib::GetSecureRandom(salt.data(), salt.size());

  SerializedVaultKeyset serialized;
  ASSERT_TRUE(
      crypto.EncryptVaultKeyset(vault_keyset, key, salt, "", &serialized));
  SecureBlob encrypted;
  GetSerializedBlob(serialized, &encrypted);

  ASSERT_TRUE(CryptoTest::FindBlobInBlob(encrypted, salt));

  ASSERT_TRUE(CryptoTest::FromSerializedBlob(encrypted, &serialized));

  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform, &crypto);
  unsigned int crypt_flags = 0;
  CryptoError crypto_error = CryptoError::CE_NONE;

  EXPECT_CALL(tpm, DecryptBlob(_, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<4>(vkk_key), Return(Tpm::kTpmRetryNone)));

  ASSERT_TRUE(crypto.DecryptVaultKeyset(
      serialized, key, false /* locked_to_single_user */, &crypt_flags,
      &crypto_error, &new_keyset));

  SecureBlob original_data;
  ASSERT_TRUE(vault_keyset.ToKeysBlob(&original_data));
  SecureBlob new_data;
  ASSERT_TRUE(new_keyset.ToKeysBlob(&new_data));

  EXPECT_EQ(new_data.size(), original_data.size());
  ASSERT_TRUE(CryptoTest::FindBlobInBlob(new_data, original_data));

  // Check that the keyset was indeed wrapped by the TPM, and the
  // keys were derived using scrypt.
  EXPECT_EQ(0, (crypt_flags & SerializedVaultKeyset::SCRYPT_WRAPPED));
  EXPECT_EQ(SerializedVaultKeyset::TPM_WRAPPED,
            (crypt_flags & SerializedVaultKeyset::TPM_WRAPPED));
  EXPECT_EQ(SerializedVaultKeyset::SCRYPT_DERIVED,
            (crypt_flags & SerializedVaultKeyset::SCRYPT_DERIVED));
  EXPECT_EQ(0, (crypt_flags & SerializedVaultKeyset::PCR_BOUND));
}

TEST_F(CryptoTest, TpmDecryptFailureTest) {
  // Check how TPM error on Decrypt is reported.
  MockPlatform platform;
  Crypto crypto(&platform);
  NiceMock<MockTpm> tpm;
  NiceMock<MockTpmInit> tpm_init;

  crypto.set_tpm(&tpm);

  EXPECT_CALL(tpm, SealToPcrWithAuthorization(_, _, _, _, _)).Times(2);
  EXPECT_CALL(tpm_init, HasCryptohomeKey())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_init, SetupTpm(true))
      .Times(AtLeast(2));  // One by crypto.Init(), one by crypto.EnsureTpm()
  SecureBlob blob("public key hash");
  EXPECT_CALL(tpm, GetPublicKeyHash(_, _))
      .Times(2)  // Once on Encrypt and once on Decrypt of Vault.
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(blob), Return(Tpm::kTpmRetryNone)));
  EXPECT_CALL(tpm, IsOwned()).WillRepeatedly(Return(true));
  crypto.Init(&tpm_init);

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob key(20);
  CryptoLib::GetSecureRandom(key.data(), key.size());
  SecureBlob salt(PKCS5_SALT_LEN);
  CryptoLib::GetSecureRandom(salt.data(), salt.size());

  SerializedVaultKeyset serialized;
  ASSERT_TRUE(
      crypto.EncryptVaultKeyset(vault_keyset, key, salt, "", &serialized));
  SecureBlob encrypted;
  GetSerializedBlob(serialized, &encrypted);

  ASSERT_TRUE(CryptoTest::FindBlobInBlob(encrypted, salt));

  ASSERT_TRUE(CryptoTest::FromSerializedBlob(encrypted, &serialized));

  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform, &crypto);
  unsigned int crypt_flags = 0;
  CryptoError crypto_error = CryptoError::CE_NONE;

  // UnsealWithAuthorization operation will fail.
  EXPECT_CALL(tpm, UnsealWithAuthorization(_, _, _, _, _))
      .WillOnce(Return(Tpm::kTpmRetryFatal));

  ASSERT_FALSE(crypto.DecryptVaultKeyset(
      serialized, key, false /* locked_to_single_user */, &crypt_flags,
      &crypto_error, &new_keyset));
  ASSERT_NE(CryptoError::CE_NONE, crypto_error);
}

TEST_F(CryptoTest, ScryptStepTest) {
  // Check that the code path changes to support scrypt work
  MockPlatform platform;
  Crypto crypto(&platform);

  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform, &crypto);
  vault_keyset.CreateRandom();

  SecureBlob key(20);
  CryptoLib::GetSecureRandom(key.data(), key.size());
  SecureBlob salt(PKCS5_SALT_LEN);
  CryptoLib::GetSecureRandom(salt.data(), salt.size());

  SerializedVaultKeyset serialized;
  ASSERT_TRUE(
      crypto.EncryptVaultKeyset(vault_keyset, key, salt, "", &serialized));
  SecureBlob encrypted;
  GetSerializedBlob(serialized, &encrypted);

  ASSERT_TRUE(CryptoTest::FindBlobInBlob(encrypted, salt));

  ASSERT_TRUE(CryptoTest::FromSerializedBlob(encrypted, &serialized));

  VaultKeyset new_keyset;
  new_keyset.Initialize(&platform, &crypto);
  unsigned int crypt_flags = 0;
  CryptoError crypto_error = CryptoError::CE_NONE;
  ASSERT_TRUE(crypto.DecryptVaultKeyset(
      serialized, key, false /* locked_to_single_user */, &crypt_flags,
      &crypto_error, &new_keyset));

  SecureBlob original_data;
  ASSERT_TRUE(vault_keyset.ToKeysBlob(&original_data));
  SecureBlob new_data;
  ASSERT_TRUE(new_keyset.ToKeysBlob(&new_data));

  EXPECT_EQ(new_data.size(), original_data.size());
  ASSERT_TRUE(CryptoTest::FindBlobInBlob(new_data, original_data));
}

TEST_F(CryptoTest, GetSha1FipsTest) {
  MockPlatform platform;
  Crypto crypto(&platform);
  ShaTestVectors vectors(1);
  for (size_t i = 0; i < vectors.count(); ++i) {
    Blob digest = CryptoLib::Sha1(*vectors.input(i));
    std::string computed(reinterpret_cast<const char*>(digest.data()),
                         digest.size());
    std::string expected = vectors.output(i)->to_string();
    EXPECT_EQ(expected, computed);
  }
}

TEST_F(CryptoTest, GetSha256FipsTest) {
  MockPlatform platform;
  Crypto crypto(&platform);
  ShaTestVectors vectors(256);
  for (size_t i = 0; i < vectors.count(); ++i) {
    Blob digest = CryptoLib::Sha256(*vectors.input(i));
    std::string computed(reinterpret_cast<const char*>(digest.data()),
                         digest.size());
    std::string expected = vectors.output(i)->to_string();
    EXPECT_EQ(expected, computed);
  }
}

TEST_F(CryptoTest, ComputeEncryptedDataHMAC) {
  MockPlatform platform;
  Crypto crypto(&platform);
  EncryptedData pb;
  std::string data = "iamsoawesome";
  std::string iv = "123456";
  pb.set_encrypted_data(data.data(), data.size());
  pb.set_iv(iv.data(), iv.size());

  // Create hash key.
  SecureBlob hmac_key(32);
  CryptoLib::GetSecureRandom(hmac_key.data(), hmac_key.size());

  // Perturb iv and data slightly. Verify hashes are all different.
  std::string hmac1 = CryptoLib::ComputeEncryptedDataHMAC(pb, hmac_key);
  data = "iamsoawesomf";
  pb.set_encrypted_data(data.data(), data.size());
  std::string hmac2 = CryptoLib::ComputeEncryptedDataHMAC(pb, hmac_key);
  iv = "123457";
  pb.set_iv(iv.data(), iv.size());
  std::string hmac3 = CryptoLib::ComputeEncryptedDataHMAC(pb, hmac_key);

  EXPECT_NE(hmac1, hmac2);
  EXPECT_NE(hmac2, hmac3);
  EXPECT_NE(hmac1, hmac3);
}

TEST_F(CryptoTest, EncryptAndDecryptWithTpm) {
  MockPlatform platform;
  Crypto crypto(&platform);

  NiceMock<MockTpm> tpm;
  NiceMock<MockTpmInit> tpm_init;
  crypto.set_tpm(&tpm);
  crypto.Init(&tpm_init);

  std::string data = "iamsomestufftoencrypt";
  SecureBlob data_blob(data);

  std::string encrypted_data;
  SecureBlob output_blob;

  SecureBlob aes_key(32, 'A');
  brillo::SecureBlob sealed_key(32, 'S');
  SecureBlob iv(16, 'I');

  // Setup the data from the above blobs.
  EXPECT_CALL(tpm, GetRandomDataSecureBlob(32, _))
      .WillOnce(DoAll(SetArgPointee<1>(aes_key), Return(true)));
  EXPECT_CALL(tpm, SealToPCR0(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(sealed_key), Return(true)));
  EXPECT_CALL(tpm, GetRandomDataSecureBlob(16, _))
      .WillOnce(DoAll(SetArgPointee<1>(iv), Return(true)));

  // Matching calls of encrypt/decrypt should give me back the same data.
  EXPECT_TRUE(crypto.EncryptWithTpm(data_blob, &encrypted_data));

  // Unseal for the tpm.
  EXPECT_CALL(tpm, Unseal(sealed_key, _))
      .WillOnce(DoAll(SetArgPointee<1>(aes_key), Return(true)));

  EXPECT_TRUE(crypto.DecryptWithTpm(encrypted_data, &output_blob));
  EXPECT_EQ(data_blob, output_blob);

  // Perturb the data a little and verify we can no longer decrypt it.
  encrypted_data = encrypted_data + "Z";
  EXPECT_FALSE(crypto.DecryptWithTpm(encrypted_data, &output_blob));
}

TEST_F(CryptoTest, EncryptAndDecryptWithTpmWithRandomlyFailingTpm) {
  MockPlatform platform;
  Crypto crypto(&platform);

  NiceMock<MockTpm> tpm;
  NiceMock<MockTpmInit> tpm_init;
  crypto.set_tpm(&tpm);
  crypto.Init(&tpm_init);

  std::string data = "iamsomestufftoencrypt";
  SecureBlob data_blob(data);

  std::string encrypted_data;
  SecureBlob output_blob;

  SecureBlob aes_key(32, 'A');
  brillo::SecureBlob sealed_key(32, 'S');
  SecureBlob iv(16, 'I');

  // Setup the data from the above blobs and fail to seal the key with the tpm.
  EXPECT_CALL(tpm, GetRandomDataSecureBlob(32, _))
      .WillOnce(DoAll(SetArgPointee<1>(aes_key), Return(true)));
  EXPECT_CALL(tpm, SealToPCR0(_, _)).WillOnce(Return(false));
  EXPECT_FALSE(crypto.EncryptWithTpm(data_blob, &encrypted_data));

  // Failed to get random data.
  EXPECT_CALL(tpm, GetRandomDataSecureBlob(32, _)).WillOnce(Return(false));
  EXPECT_FALSE(crypto.EncryptWithTpm(data_blob, &encrypted_data));

  // Now setup successful encrypt data but fail to unseal.
  // Setup the data from the above blobs.
  EXPECT_CALL(tpm, GetRandomDataSecureBlob(32, _))
      .WillOnce(DoAll(SetArgPointee<1>(aes_key), Return(true)));
  EXPECT_CALL(tpm, SealToPCR0(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(sealed_key), Return(true)));
  EXPECT_CALL(tpm, GetRandomDataSecureBlob(16, _))
      .WillOnce(DoAll(SetArgPointee<1>(iv), Return(true)));

  // Matching calls of encrypt/decrypt should give me back the same data.
  EXPECT_TRUE(crypto.EncryptWithTpm(data_blob, &encrypted_data));

  // Tpm failing to unseal a valid key.
  EXPECT_CALL(tpm, Unseal(sealed_key, _)).WillOnce(Return(false));
  EXPECT_FALSE(crypto.DecryptWithTpm(encrypted_data, &output_blob));
}

namespace {
constexpr char kHexHeSecret[] =
    "F3D9D5B126C36676689E18BB8517D95DF4F30947E71D4A840824425760B1D3FA";
constexpr char kHexResetSecret[] =
    "B133D2450392335BA8D33AA95AD52488254070C66F5D79AEA1A46AC4A30760D4";
constexpr char kHexWrappedKeyset[] =
    "B737B5D73E39BD390A4F361CE2FC166CF1E89EC6AEAA35D4B34456502C48B4F5EFA310077"
    "324B393E13AF633DF3072FF2EC78BD2B80D919035DB97C30F1AD418737DA3F26A4D35DF6B"
    "6A9743BD0DF3D37D8A68DE0932A9905452D05ECF92701B9805937F76EE01D10924268F057"
    "EDD66087774BB86C2CB92B01BD3A3C41C10C52838BD3A3296474598418E5191DEE9E8D831"
    "3C859C9EDB0D5F2BC1D7FC3C108A0D4ABB2D90E413086BCFFD0902AB68E2BF787817EB10C"
    "25E2E43011CAB3FB8AA";
constexpr char kHexSalt[] = "D470B9B108902241";
constexpr char kHexVaultKey[] =
    "665A58534E684F2B61516B6D42624B514E6749732B4348427450305453754158377232347"
    "37A79466C6B383D";
constexpr char kHexFekIv[] = "EA80F14BF29C6D580D536E7F0CC47F3E";
constexpr char kHexChapsIv[] = "ED85D928940E5B02ED218F29225AA34F";
constexpr char kHexWrappedChapsKey[] =
    "7D7D01EECC8DAE7906CAD56310954BBEB3CC81765210D29902AB92DDE074217771AD284F2"
    "12C13897C6CBB30CEC4CD75";

std::string HexDecode(const std::string& hex) {
  std::vector<uint8_t> output;
  CHECK(base::HexStringToBytes(hex, &output));
  return std::string(output.begin(), output.end());
}
}  // namespace

class LeCredentialsManagerTest : public ::testing::Test {
 public:
  LeCredentialsManagerTest() : crypto_(&platform_) {
    crypto_.set_tpm(&tpm_);

    EXPECT_CALL(tpm_init_, SetupTpm(true))
        .WillOnce(
            Return(true));  // because HasCryptohomeKey returned false once.

    EXPECT_CALL(tpm_, IsEnabled()).WillRepeatedly(Return(true));
    EXPECT_CALL(tpm_, IsOwned()).WillRepeatedly(Return(true));

    // Raw pointer as crypto expects unique_ptr, which we will wrap this
    // allocation into.
    le_cred_manager_ = new MockLECredentialManager();
    EXPECT_CALL(*le_cred_manager_, CheckCredential(_, _, _, _))
        .WillRepeatedly(DoAll(
            SetArgPointee<2>(brillo::SecureBlob(HexDecode(kHexHeSecret))),
            SetArgPointee<3>(brillo::SecureBlob(HexDecode(kHexResetSecret))),
            Return(LE_CRED_SUCCESS)));
    crypto_.set_le_manager_for_testing(
        std::unique_ptr<cryptohome::LECredentialManager>(le_cred_manager_));

    crypto_.Init(&tpm_init_);

    pin_vault_keyset_.Initialize(&platform_, &crypto_);
  }

  ~LeCredentialsManagerTest() override = default;

  // Not copyable or movable
  LeCredentialsManagerTest(const LeCredentialsManagerTest&) = delete;
  LeCredentialsManagerTest& operator=(const LeCredentialsManagerTest&) = delete;
  LeCredentialsManagerTest(LeCredentialsManagerTest&&) = delete;
  LeCredentialsManagerTest& operator=(LeCredentialsManagerTest&&) = delete;

 protected:
  MockPlatform platform_;
  Crypto crypto_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockTpmInit> tpm_init_;
  MockLECredentialManager* le_cred_manager_;

  VaultKeyset pin_vault_keyset_;
};

TEST_F(LeCredentialsManagerTest, Encrypt) {
  EXPECT_CALL(*le_cred_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(Return(LE_CRED_SUCCESS));

  pin_vault_keyset_.CreateRandom();
  pin_vault_keyset_.mutable_serialized()
      ->mutable_key_data()
      ->mutable_policy()
      ->set_low_entropy_credential(true);

  SerializedVaultKeyset serialized;
  EXPECT_TRUE(crypto_.EncryptVaultKeyset(
      pin_vault_keyset_, brillo::SecureBlob(HexDecode(kHexVaultKey)),
      brillo::SecureBlob(HexDecode(kHexSalt)), "unused", &serialized));

  EXPECT_EQ(serialized.flags(), SerializedVaultKeyset::LE_CREDENTIAL);
}

TEST_F(LeCredentialsManagerTest, EncryptFail) {
  EXPECT_CALL(*le_cred_manager_, InsertCredential(_, _, _, _, _, _))
      .WillOnce(Return(LE_CRED_ERROR_NO_FREE_LABEL));

  pin_vault_keyset_.CreateRandom();
  pin_vault_keyset_.mutable_serialized()
      ->mutable_key_data()
      ->mutable_policy()
      ->set_low_entropy_credential(true);

  SerializedVaultKeyset serialized;
  EXPECT_FALSE(crypto_.EncryptVaultKeyset(
      pin_vault_keyset_, brillo::SecureBlob(HexDecode(kHexVaultKey)),
      brillo::SecureBlob(HexDecode(kHexSalt)), "unused", &serialized));
}

TEST_F(LeCredentialsManagerTest, Decrypt) {
  SerializedVaultKeyset serialized;
  serialized.set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
  serialized.set_le_fek_iv(HexDecode(kHexFekIv));
  serialized.set_le_chaps_iv(HexDecode(kHexChapsIv));
  serialized.set_wrapped_keyset(HexDecode(kHexWrappedKeyset));
  serialized.set_wrapped_chaps_key(HexDecode(kHexWrappedChapsKey));
  serialized.set_salt(HexDecode(kHexSalt));

  CryptoError crypto_error = CryptoError::CE_NONE;
  EXPECT_TRUE(crypto_.DecryptVaultKeyset(
      serialized, brillo::SecureBlob(HexDecode(kHexVaultKey)), false, nullptr,
      &crypto_error, &pin_vault_keyset_));
  EXPECT_EQ(CryptoError::CE_NONE, crypto_error);
}

}  // namespace cryptohome
