// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/boot_lockbox.h"

#include <map>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"

using base::FilePath;
using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace cryptohome {

// The DER encoding of SHA-256 DigestInfo as defined in PKCS #1.
const unsigned char kSha256DigestInfo[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};

class BootLockboxTest : public testing::Test {
 public:
  BootLockboxTest() : is_fake_extended_(false) {}
  virtual ~BootLockboxTest() {}

  void SetUp() {
    // Configure a fake TPM.
    ON_CALL(tpm_, Sign(_, _, _, _))
        .WillByDefault(
            WithArgs<1, 3>(Invoke(this, &BootLockboxTest::FakeSign)));
    ON_CALL(tpm_, CreatePCRBoundKey(_, _, _, _, _))
        .WillByDefault(WithArgs<3>(Invoke(this, &BootLockboxTest::FakeCreate)));
    ON_CALL(tpm_, VerifyPCRBoundKey(_, _, _)).WillByDefault(Return(true));
    ON_CALL(tpm_, ExtendPCR(_, _))
        .WillByDefault(InvokeWithoutArgs(this, &BootLockboxTest::FakeExtend));
    ON_CALL(tpm_, ReadPCR(_, _))
        .WillByDefault(
            WithArgs<1>(Invoke(this, &BootLockboxTest::FakeReadPCR)));
    ON_CALL(crypto_, EncryptWithTpm(_, _))
        .WillByDefault(Invoke(this, &BootLockboxTest::FakeEncrypt));
    ON_CALL(crypto_, DecryptWithTpm(_, _))
        .WillByDefault(Invoke(this, &BootLockboxTest::FakeDecrypt));
    // Configure a fake filesystem.
    ON_CALL(platform_, WriteStringToFileAtomicDurable(_, _, _))
        .WillByDefault(
            WithArgs<0, 1>(Invoke(this, &BootLockboxTest::FakeWriteFile)));
    ON_CALL(platform_, ReadFileToString(_, _))
        .WillByDefault(Invoke(this, &BootLockboxTest::FakeReadFile));
    lockbox_.reset(new BootLockbox(&tpm_, &platform_, &crypto_));
    lockbox2_.reset(new BootLockbox(&tpm_, &platform_, &crypto_));
  }

  bool FakeSign(const brillo::SecureBlob& input,
                brillo::SecureBlob* signature) {
    if (is_fake_extended_)
      return false;
    brillo::SecureBlob der_header(std::begin(kSha256DigestInfo),
                                  std::end(kSha256DigestInfo));
    brillo::SecureBlob der_encoded_input =
        brillo::SecureBlob::Combine(der_header, CryptoLib::Sha256(input));
    unsigned char buffer[256];
    int length =
        RSA_private_encrypt(der_encoded_input.size(), der_encoded_input.data(),
                            buffer, rsa(), RSA_PKCS1_PADDING);
    brillo::SecureBlob tmp(buffer, buffer + length);
    signature->swap(tmp);
    return true;
  }

  bool FakeCreate(brillo::SecureBlob* public_key) {
    rsa_.reset();
    unsigned char* buffer = NULL;
    int length = i2d_RSAPublicKey(rsa(), &buffer);
    if (length <= 0)
      return false;
    brillo::SecureBlob tmp(buffer, buffer + length);
    public_key->swap(tmp);
    OPENSSL_free(buffer);
    return true;
  }

  bool FakeExtend() {
    is_fake_extended_ = true;
    return true;
  }

  bool FakeReadPCR(brillo::Blob* pcr) {
    pcr->assign(20, is_fake_extended_ ? 0xAA : 0);
    return true;
  }

  bool FakeWriteFile(const FilePath& path, const std::string& data) {
    fake_files_[path.value()] = data;
    return true;
  }

  bool FakeReadFile(const FilePath& path, std::string* data) {
    if (fake_files_.count(path.value()) == 0)
      return false;
    *data = fake_files_[path.value()];
    return true;
  }

  bool FakeEncrypt(const brillo::SecureBlob& in, std::string* out) {
    *out = in.to_string();
    return true;
  }

  bool FakeDecrypt(const std::string& in, brillo::SecureBlob* out) {
    *out = brillo::SecureBlob(in);
    return true;
  }

 protected:
  NiceMock<MockTpm> tpm_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockCrypto> crypto_;
  std::unique_ptr<BootLockbox> lockbox_;
  std::unique_ptr<BootLockbox> lockbox2_;
  bool is_fake_extended_;
  std::map<std::string, std::string> fake_files_;
  crypto::ScopedRSA rsa_;  // Access with rsa().

  RSA* rsa() {
    if (!rsa_) {
      crypto::ScopedBIGNUM e(BN_new());
      CHECK(e);
      EXPECT_TRUE(BN_set_word(e.get(), 65537));
      rsa_.reset(RSA_new());
      CHECK(rsa_);
      EXPECT_TRUE(RSA_generate_key_ex(rsa_.get(), 2048, e.get(), nullptr));
    }
    return rsa_.get();
  }
};

TEST_F(BootLockboxTest, NormalUse) {
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  EXPECT_LT(0, signature.size());
  ASSERT_TRUE(lockbox_->Verify(data, signature));
  EXPECT_TRUE(lockbox_->FinalizeBoot());
  ASSERT_TRUE(lockbox_->Verify(data, signature));
}

TEST_F(BootLockboxTest, SignAfterFinalize) {
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  ASSERT_TRUE(lockbox_->FinalizeBoot());
  EXPECT_CALL(tpm_, Sign(_, _, _, _)).Times(0);
  ASSERT_FALSE(lockbox_->Sign(data, &signature));
}

TEST_F(BootLockboxTest, CreateAfterFinalize) {
  ASSERT_TRUE(lockbox_->FinalizeBoot());
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  // Not only signing should fail, but a new key file should not be created
  // or signing attempted, if already finalized.
  ASSERT_EQ(0, fake_files_.size());
  EXPECT_CALL(tpm_, Sign(_, _, _, _)).Times(0);
  EXPECT_CALL(platform_, WriteStringToFileAtomicDurable(_, _, _)).Times(0);
  ASSERT_FALSE(lockbox_->Sign(data, &signature));
  ASSERT_EQ(0, fake_files_.size());
}

TEST_F(BootLockboxTest, VerifyIsFinalized) {
  ASSERT_FALSE(lockbox_->IsFinalized());
  ASSERT_TRUE(lockbox_->FinalizeBoot());
  ASSERT_TRUE(lockbox_->IsFinalized());
}

TEST_F(BootLockboxTest, LoadFromFile) {
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  // Verify in another instance which needs to load the key.
  ASSERT_TRUE(lockbox2_->Verify(data, signature));
}

TEST_F(BootLockboxTest, FileErrors) {
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));

  EXPECT_CALL(platform_, WriteStringToFileAtomicDurable(_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, ReadFileToString(_, _)).WillRepeatedly(Return(false));

  EXPECT_FALSE(lockbox2_->Sign(data, &signature));
  EXPECT_FALSE(lockbox2_->Verify(data, signature));
  EXPECT_TRUE(lockbox2_->FinalizeBoot());
}

TEST_F(BootLockboxTest, SignError) {
  EXPECT_CALL(tpm_, Sign(_, _, _, _)).WillRepeatedly(Return(false));
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_FALSE(lockbox_->Sign(data, &signature));
}

TEST_F(BootLockboxTest, ExtendPCRError) {
  EXPECT_CALL(tpm_, ExtendPCR(_, _)).WillRepeatedly(Return(false));
  ASSERT_FALSE(lockbox_->FinalizeBoot());
}

TEST_F(BootLockboxTest, VerifyWithBadKey) {
  EXPECT_CALL(tpm_, VerifyPCRBoundKey(_, _, _)).WillRepeatedly(Return(false));
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  ASSERT_FALSE(lockbox_->Verify(data, signature));
}

TEST_F(BootLockboxTest, VerifyWithNoKey) {
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_FALSE(lockbox_->Verify(data, signature));
}

TEST_F(BootLockboxTest, VerifyWithBadSignature) {
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  ASSERT_TRUE(lockbox_->Verify(data, signature));
  brillo::Blob swapped_data(signature.begin(), signature.end());
  brillo::SecureBlob swapped_signature(data.begin(), data.end());
  ASSERT_FALSE(lockbox_->Verify(swapped_data, swapped_signature));
}

TEST_F(BootLockboxTest, EncryptError) {
  // Induce encryption failures; we expect a key cannot be successfully created.
  EXPECT_CALL(crypto_, EncryptWithTpm(_, _)).WillRepeatedly(Return(false));
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_FALSE(lockbox_->Sign(data, &signature));
}

TEST_F(BootLockboxTest, DecryptError) {
  // Induce decryption failures; we expect keys can be created and written to
  // 'disk' but they cannot be loaded again.
  EXPECT_CALL(crypto_, DecryptWithTpm(_, _)).WillRepeatedly(Return(false));
  brillo::Blob data(100);
  brillo::SecureBlob signature;
  ASSERT_TRUE(lockbox_->Sign(data, &signature));
  EXPECT_TRUE(lockbox_->Verify(data, signature));
  // A second instance will not be able to load from disk.
  EXPECT_FALSE(lockbox2_->Verify(data, signature));
  brillo::SecureBlob signature2;
  // Sign() should still succeed because it can create a new key.
  EXPECT_TRUE(lockbox2_->Sign(data, &signature2));
  EXPECT_TRUE(lockbox2_->Verify(data, signature2));
  // Now the two instances should have different keys.
  EXPECT_FALSE(lockbox2_->Verify(data, signature));
  EXPECT_FALSE(lockbox_->Verify(data, signature2));
}

}  // namespace cryptohome
