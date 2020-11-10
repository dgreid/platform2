// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "libtpmcrypto/tpm.h"
#include "libtpmcrypto/tpm_crypto_impl.h"
#include "libtpmcrypto/tpm_proto_utils.h"

using brillo::Blob;
using brillo::SecureBlob;

namespace tpmcrypto {

// The fake implementation returns length bytes, where each byte
// has the value length % 256.
bool FakeRandBytes(uint8_t* bytes, int length) {
  std::fill(bytes, bytes + length, static_cast<uint8_t>(length % 256));
  return true;
}

// A fake TPM for use in tests.
class FakeTpm : public Tpm {
 public:
  FakeTpm() = default;
  FakeTpm(const FakeTpm&) = delete;
  FakeTpm& operator=(const FakeTpm&) = delete;

  ~FakeTpm() override = default;

  // The fake implementation just flips every bit in the input. The Unseal
  // method does the same so they are symmetric.
  bool SealToPCR0(const SecureBlob& value, SecureBlob* sealed_value) override {
    CHECK(sealed_value);
    // Make sure we are never sealing more than 128 bytes/1024 bits.
    CHECK_LE(value.size(), 128);
    sealed_value->resize(value.size());
    std::transform(value.begin(), value.end(), sealed_value->begin(),
                   std::bit_not<uint8_t>());
    return true;
  }

  // The fake implementation just flips every bit in the input. The SealToPCR0
  // method does the same so they are symmetric.
  bool Unseal(const SecureBlob& sealed_value, SecureBlob* value) override {
    CHECK(value);
    value->resize(sealed_value.size());
    std::transform(sealed_value.begin(), sealed_value.end(), value->begin(),
                   std::bit_not<uint8_t>());
    return true;
  }

  bool GetNVAttributes(uint32_t index, uint32_t* attributes) override {
    return true;
  }

  bool NVReadNoAuth(uint32_t index,
                    uint32_t offset,
                    size_t size,
                    std::string* data) override {
    return true;
  }
};

class TpmCryptoImplTest : public testing::Test {
 public:
  TpmCryptoImplTest() {
    auto tpm = std::make_unique<FakeTpm>();
    tpm_ = tpm.get();
    tpm_crypto_ = std::make_unique<TpmCryptoImpl>(std::move(tpm));
  }
  TpmCryptoImplTest(const TpmCryptoImplTest&) = delete;
  TpmCryptoImplTest& operator=(const TpmCryptoImplTest&) = delete;

  ~TpmCryptoImplTest() override = default;

 protected:
  // Encrypts plaintext then decrypts the result. Returns true if the output
  // of the encryption/decrytion round trip is the original plaintext.
  void ValidateRoundTrip(const std::string& plaintext) {
    SecureBlob expected_plaintext(plaintext);
    std::string serialized_proto;
    ASSERT_TRUE(tpm_crypto_->Encrypt(expected_plaintext, &serialized_proto));

    SecureBlob actual_plain_text;
    ASSERT_TRUE(tpm_crypto_->Decrypt(serialized_proto, &actual_plain_text));
    ASSERT_EQ(expected_plaintext, actual_plain_text);
  }

  std::unique_ptr<TpmCryptoImpl> tpm_crypto_;
  FakeTpm* tpm_;  // Not owned
};

TEST_F(TpmCryptoImplTest, SanityTestFakeTpm) {
  const size_t expected_length = 7;
  SecureBlob rand;
  rand.resize(expected_length);
  ASSERT_TRUE(FakeRandBytes(rand.data(), rand.size()));

  EXPECT_EQ(expected_length, rand.size());
  for (size_t i = 0; i < expected_length; i++) {
    // The "random" content of each byte is length % 256.
    EXPECT_EQ(expected_length, rand[i]);
  }

  // The fake seal function just flips all the bits on the input, so
  // generate that expected result.
  SecureBlob expected_sealed(rand);
  std::transform(expected_sealed.begin(), expected_sealed.end(),
                 expected_sealed.begin(), std::bit_not<uint8_t>());

  // Seal the random number and verify it is the bit flipped input.
  SecureBlob actual_sealed;
  ASSERT_TRUE(tpm_->SealToPCR0(rand, &actual_sealed));
  EXPECT_EQ(actual_sealed.size(), rand.size());
  EXPECT_EQ(actual_sealed.size(), expected_sealed.size());
  EXPECT_EQ(0, memcmp(expected_sealed.data(), actual_sealed.data(),
                      expected_sealed.size()));
  // Double check it's not the same as the original.
  EXPECT_NE(0, memcmp(rand.data(), actual_sealed.data(), rand.size()));

  // Unseal the sealed value and verify it is the original "random" number.
  SecureBlob actual_unsealed;
  ASSERT_TRUE(tpm_->Unseal(actual_sealed, &actual_unsealed));
  EXPECT_EQ(actual_unsealed.size(), actual_unsealed.size());
  EXPECT_EQ(actual_unsealed.size(), rand.size());
  EXPECT_EQ(0, memcmp(rand.data(), actual_unsealed.data(), rand.size()));
}

TEST_F(TpmCryptoImplTest, SimpleEncryptDecryptRoundTrip) {
  ValidateRoundTrip("Secret Message");
}

TEST_F(TpmCryptoImplTest, EmptyPlaintext) {
  // Empty plaintext fails, since GCM is unpadded, this would
  // result in empty ciphertext.
  SecureBlob expected_plaintext("");
  std::string serialized_proto;
  ASSERT_FALSE(tpm_crypto_->Encrypt(expected_plaintext, &serialized_proto));
}

TEST_F(TpmCryptoImplTest, SingleBytePlainText) {
  ValidateRoundTrip(std::string(1, 'X'));
}

TEST_F(TpmCryptoImplTest, MegabytePlaintext) {
  ValidateRoundTrip(std::string(1024 * 1024, 'X'));
}

TEST_F(TpmCryptoImplTest, AnyModificationFailsDecryption) {
  const std::string plaintext = "Secret Message";
  std::string serialized_proto;
  ASSERT_TRUE(tpm_crypto_->Encrypt(SecureBlob(plaintext), &serialized_proto));

  // Sanity check that it does decrypt.
  SecureBlob decrypted_plaintext;
  ASSERT_TRUE(tpm_crypto_->Decrypt(serialized_proto, &decrypted_plaintext));
  decrypted_plaintext.clear();

  // Flip every bit of the serialized proto and verify it doesn't decrypt.
  for (size_t i = 0; i < serialized_proto.size() * 8; i++) {
    std::string modified_proto = serialized_proto;
    modified_proto[i / 8] ^= 1 << (i % 8);

    ASSERT_FALSE(tpm_crypto_->Decrypt(modified_proto, &decrypted_plaintext));
  }
}

TEST_F(TpmCryptoImplTest, TruncateTagFailsDecryption) {
  const std::string plaintext = "Secret Message";
  std::string serialized_proto;
  ASSERT_TRUE(tpm_crypto_->Encrypt(SecureBlob(plaintext), &serialized_proto));

  SecureBlob sealed_key;
  SecureBlob iv;
  SecureBlob tag;
  SecureBlob encrypted_data;
  ASSERT_TRUE(ParseTpmCryptoProto(serialized_proto, &sealed_key, &iv, &tag,
                                  &encrypted_data));

  // Make the tag 1 byte shorter.
  ASSERT_GT(tag.size(), 0);
  tag.resize(tag.size() - 1);

  std::string modified_proto;
  ASSERT_TRUE(CreateSerializedTpmCryptoProto(sealed_key, iv, tag,
                                             encrypted_data, &modified_proto));

  SecureBlob decrypted_plaintext;
  ASSERT_FALSE(tpm_crypto_->Decrypt(modified_proto, &decrypted_plaintext));
}

TEST_F(TpmCryptoImplTest, TruncateKeyFailsDecryption) {
  const std::string plaintext = "Secret Message";
  std::string serialized_proto;
  ASSERT_TRUE(tpm_crypto_->Encrypt(SecureBlob(plaintext), &serialized_proto));

  SecureBlob sealed_key;
  SecureBlob iv;
  SecureBlob tag;
  SecureBlob encrypted_data;
  ASSERT_TRUE(ParseTpmCryptoProto(serialized_proto, &sealed_key, &iv, &tag,
                                  &encrypted_data));

  // Make the sealed key 1 byte shorter.
  ASSERT_GT(sealed_key.size(), 0);
  sealed_key.resize(sealed_key.size() - 1);

  std::string modified_proto;
  ASSERT_TRUE(CreateSerializedTpmCryptoProto(sealed_key, iv, tag,
                                             encrypted_data, &modified_proto));

  SecureBlob decrypted_plaintext;
  ASSERT_FALSE(tpm_crypto_->Decrypt(modified_proto, &decrypted_plaintext));
}

TEST_F(TpmCryptoImplTest, TruncateIVFailsDecryption) {
  const std::string plaintext = "Secret Message";
  std::string serialized_proto;
  ASSERT_TRUE(tpm_crypto_->Encrypt(SecureBlob(plaintext), &serialized_proto));

  SecureBlob sealed_key;
  SecureBlob iv;
  SecureBlob tag;
  SecureBlob encrypted_data;
  ASSERT_TRUE(ParseTpmCryptoProto(serialized_proto, &sealed_key, &iv, &tag,
                                  &encrypted_data));

  // Make the IV 1 byte shorter.
  ASSERT_GT(iv.size(), 0);
  iv.resize(iv.size() - 1);

  std::string modified_proto;
  ASSERT_TRUE(CreateSerializedTpmCryptoProto(sealed_key, iv, tag,
                                             encrypted_data, &modified_proto));

  SecureBlob decrypted_plaintext;
  ASSERT_FALSE(tpm_crypto_->Decrypt(modified_proto, &decrypted_plaintext));
}

}  // namespace tpmcrypto
