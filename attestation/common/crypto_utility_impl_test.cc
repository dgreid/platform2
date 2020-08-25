// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/objects.h>

#include "attestation/common/crypto_utility_impl.h"
#include "attestation/common/mock_tpm_utility.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace {

const char kValidPublicKeyHex[] =
    "3082010A0282010100"
    "961037BC12D2A298BEBF06B2D5F8C9B64B832A2237F8CF27D5F96407A6041A4D"
    "AD383CB5F88E625F412E8ACD5E9D69DF0F4FA81FCE7955829A38366CBBA5A2B1"
    "CE3B48C14B59E9F094B51F0A39155874C8DE18A0C299EBF7A88114F806BE4F25"
    "3C29A509B10E4B19E31675AFE3B2DA77077D94F43D8CE61C205781ED04D183B4"
    "C349F61B1956C64B5398A3A98FAFF17D1B3D9120C832763EDFC8F4137F6EFBEF"
    "46D8F6DE03BD00E49DEF987C10BDD5B6F8758B6A855C23C982DDA14D8F0F2B74"
    "E6DEFA7EEE5A6FC717EB0FF103CB8049F693A2C8A5039EF1F5C025DC44BD8435"
    "E8D8375DADE00E0C0F5C196E04B8483CC98B1D5B03DCD7E0048B2AB343FFC11F"
    "0203"
    "010001";

const char kValidModulusHex[] =
    "961037BC12D2A298BEBF06B2D5F8C9B64B832A2237F8CF27D5F96407A6041A4D"
    "AD383CB5F88E625F412E8ACD5E9D69DF0F4FA81FCE7955829A38366CBBA5A2B1"
    "CE3B48C14B59E9F094B51F0A39155874C8DE18A0C299EBF7A88114F806BE4F25"
    "3C29A509B10E4B19E31675AFE3B2DA77077D94F43D8CE61C205781ED04D183B4"
    "C349F61B1956C64B5398A3A98FAFF17D1B3D9120C832763EDFC8F4137F6EFBEF"
    "46D8F6DE03BD00E49DEF987C10BDD5B6F8758B6A855C23C982DDA14D8F0F2B74"
    "E6DEFA7EEE5A6FC717EB0FF103CB8049F693A2C8A5039EF1F5C025DC44BD8435"
    "E8D8375DADE00E0C0F5C196E04B8483CC98B1D5B03DCD7E0048B2AB343FFC11F";

std::string HexDecode(const std::string hex) {
  std::vector<uint8_t> output;
  CHECK(base::HexStringToBytes(hex, &output));
  return std::string(reinterpret_cast<char*>(output.data()), output.size());
}

}  // namespace

namespace attestation {

class CryptoUtilityImplTest : public testing::Test {
 public:
  ~CryptoUtilityImplTest() override = default;
  void SetUp() override {
    crypto_utility_.reset(new CryptoUtilityImpl(&mock_tpm_utility_));
  }

  bool EncryptIdentityCertificate(const std::string& credential,
                                  const std::string& certificate,
                                  EncryptedData* encrypted) {
    return crypto_utility_->EncryptWithSeed(
        CryptoUtilityImpl::KeyDerivationScheme::kHashWithHeaders, certificate,
        credential, encrypted);
  }

  std::string KDFa(const std::string& key,
                   const std::string& label,
                   const std::string& context,
                   int bits) {
    return crypto_utility_->Tpm2CompatibleKDFa(key, label, context, bits);
  }

 protected:
  NiceMock<MockTpmUtility> mock_tpm_utility_;
  std::unique_ptr<CryptoUtilityImpl> crypto_utility_;
};

TEST_F(CryptoUtilityImplTest, GetRandomSuccess) {
  std::string random1;
  EXPECT_TRUE(crypto_utility_->GetRandom(20, &random1));
  std::string random2;
  EXPECT_TRUE(crypto_utility_->GetRandom(20, &random2));
  EXPECT_NE(random1, random2);
}

TEST_F(CryptoUtilityImplTest, GetRandomIntOverflow) {
  size_t num_bytes = -1;
  std::string buffer;
  EXPECT_FALSE(crypto_utility_->GetRandom(num_bytes, &buffer));
}

TEST_F(CryptoUtilityImplTest, PairwiseSealedEncryption) {
  std::string key;
  std::string sealed_key;
  EXPECT_TRUE(crypto_utility_->CreateSealedKey(&key, &sealed_key));
  std::string data("test");
  std::string encrypted_data;
  EXPECT_TRUE(
      crypto_utility_->EncryptData(data, key, sealed_key, &encrypted_data));
  key.clear();
  sealed_key.clear();
  data.clear();
  EXPECT_TRUE(crypto_utility_->UnsealKey(encrypted_data, &key, &sealed_key));
  EXPECT_TRUE(crypto_utility_->DecryptData(encrypted_data, key, &data));
  EXPECT_EQ("test", data);
}

TEST_F(CryptoUtilityImplTest, SealFailure) {
  EXPECT_CALL(mock_tpm_utility_, SealToPCR0(_, _))
      .WillRepeatedly(Return(false));
  std::string key;
  std::string sealed_key;
  EXPECT_FALSE(crypto_utility_->CreateSealedKey(&key, &sealed_key));
}

TEST_F(CryptoUtilityImplTest, EncryptNoData) {
  std::string key(32, 0);
  std::string output;
  EXPECT_TRUE(crypto_utility_->EncryptData(std::string(), key, key, &output));
}

TEST_F(CryptoUtilityImplTest, EncryptInvalidKey) {
  std::string key(12, 0);
  std::string output;
  EXPECT_FALSE(crypto_utility_->EncryptData(std::string(), key, key, &output));
}

TEST_F(CryptoUtilityImplTest, UnsealInvalidData) {
  std::string output;
  EXPECT_FALSE(crypto_utility_->UnsealKey("invalid", &output, &output));
}

TEST_F(CryptoUtilityImplTest, UnsealError) {
  EXPECT_CALL(mock_tpm_utility_, Unseal(_, _)).WillRepeatedly(Return(false));
  std::string key(32, 0);
  std::string data;
  EXPECT_TRUE(crypto_utility_->EncryptData("data", key, key, &data));
  std::string output;
  EXPECT_FALSE(crypto_utility_->UnsealKey(data, &output, &output));
}

TEST_F(CryptoUtilityImplTest, DecryptInvalidKey) {
  std::string key(12, 0);
  std::string output;
  EXPECT_FALSE(crypto_utility_->DecryptData(std::string(), key, &output));
}

TEST_F(CryptoUtilityImplTest, DecryptInvalidData) {
  std::string key(32, 0);
  std::string output;
  EXPECT_FALSE(crypto_utility_->DecryptData("invalid", key, &output));
}

TEST_F(CryptoUtilityImplTest, DecryptInvalidData2) {
  std::string key(32, 0);
  std::string output;
  EncryptedData proto;
  std::string input;
  proto.SerializeToString(&input);
  EXPECT_FALSE(crypto_utility_->DecryptData(input, key, &output));
}

TEST_F(CryptoUtilityImplTest, GetRSASubjectPublicKeyInfo) {
  std::string public_key = HexDecode(kValidPublicKeyHex);
  std::string output;
  EXPECT_TRUE(crypto_utility_->GetRSASubjectPublicKeyInfo(public_key, &output));
}

TEST_F(CryptoUtilityImplTest, GetRSASubjectPublicKeyInfoBadInput) {
  std::string public_key = "bad_public_key";
  std::string output;
  EXPECT_FALSE(
      crypto_utility_->GetRSASubjectPublicKeyInfo(public_key, &output));
}

TEST_F(CryptoUtilityImplTest, GetRSASubjectPublicKeyInfoPairWise) {
  std::string public_key = HexDecode(kValidPublicKeyHex);
  std::string output;
  EXPECT_TRUE(crypto_utility_->GetRSASubjectPublicKeyInfo(public_key, &output));
  std::string public_key2;
  EXPECT_TRUE(crypto_utility_->GetRSAPublicKey(output, &public_key2));
  EXPECT_EQ(public_key, public_key2);
}

TEST_F(CryptoUtilityImplTest, EncryptIdentityCredential) {
  std::string public_key = HexDecode(kValidPublicKeyHex);
  EncryptedIdentityCredential output;
  EXPECT_TRUE(crypto_utility_->EncryptIdentityCredential(
      TPM_1_2, "credential", public_key, "aik", &output));
  EXPECT_TRUE(output.has_asym_ca_contents());
  EXPECT_TRUE(output.has_sym_ca_attestation());
  EXPECT_EQ(TPM_1_2, output.tpm_version());
}

TEST_F(CryptoUtilityImplTest, EncryptIdentityCredentialBadEK) {
  EncryptedIdentityCredential output;
  EXPECT_FALSE(crypto_utility_->EncryptIdentityCredential(
      TPM_1_2, "credential", "bad_ek", "aik", &output));
}

TEST_F(CryptoUtilityImplTest, EncryptIdentityCredentialTpm2) {
  std::string public_key = HexDecode(kValidPublicKeyHex);
  std::string public_key_info;
  EXPECT_TRUE(crypto_utility_->GetRSASubjectPublicKeyInfo(public_key,
                                                          &public_key_info));
  EncryptedIdentityCredential output;
  EXPECT_TRUE(crypto_utility_->EncryptIdentityCredential(
      TPM_2_0, "credential", public_key_info, "aik", &output));
  EXPECT_FALSE(output.has_asym_ca_contents());
  EXPECT_FALSE(output.has_sym_ca_attestation());
  EXPECT_TRUE(output.has_encrypted_seed());
  EXPECT_TRUE(output.has_credential_mac());
  EXPECT_TRUE(output.has_wrapped_certificate());
  EXPECT_EQ(TPM_2_0, output.tpm_version());
}

TEST_F(CryptoUtilityImplTest, EncryptIdentityCredentialTpm2BadEK) {
  EncryptedIdentityCredential output;
  EXPECT_FALSE(crypto_utility_->EncryptIdentityCredential(
      TPM_2_0, "credential", "bad_ek", "aik", &output));
}

TEST_F(CryptoUtilityImplTest, DecryptIdentityCertificateForTpm2) {
  std::string credential(32, 'A');
  std::string certificate(10000, 'B');
  EncryptedData encrypted;
  ASSERT_TRUE(EncryptIdentityCertificate(credential, certificate, &encrypted));
  std::string decrypted_certificate;
  EXPECT_TRUE(crypto_utility_->DecryptIdentityCertificateForTpm2(
      credential, encrypted, &decrypted_certificate));
  EXPECT_EQ(certificate, decrypted_certificate);
}

TEST_F(CryptoUtilityImplTest, DecryptIdentityCertificateForTpm2BadInput) {
  std::string credential(32, 'A');
  std::string certificate(10000, 'B');
  EncryptedData encrypted;
  std::string decrypted_certificate;
  EXPECT_FALSE(crypto_utility_->DecryptIdentityCertificateForTpm2(
      credential, encrypted, &decrypted_certificate));
}

TEST_F(CryptoUtilityImplTest, EncryptForUnbind) {
  std::string public_key = HexDecode(kValidPublicKeyHex);
  std::string public_key_info;
  EXPECT_TRUE(crypto_utility_->GetRSASubjectPublicKeyInfo(public_key,
                                                          &public_key_info));
  std::string output;
  EXPECT_TRUE(
      crypto_utility_->EncryptForUnbind(public_key_info, "input", &output));
  EXPECT_FALSE(output.empty());
}

TEST_F(CryptoUtilityImplTest, EncryptForUnbindBadKey) {
  std::string output;
  EXPECT_FALSE(crypto_utility_->EncryptForUnbind("bad_key", "input", &output));
}

TEST_F(CryptoUtilityImplTest, EncryptForUnbindLargeInput) {
  std::string public_key = HexDecode(kValidPublicKeyHex);
  std::string public_key_info;
  EXPECT_TRUE(crypto_utility_->GetRSASubjectPublicKeyInfo(public_key,
                                                          &public_key_info));
  std::string input(1000, 'A');
  std::string output;
  EXPECT_FALSE(
      crypto_utility_->EncryptForUnbind(public_key_info, input, &output));
}

TEST_F(CryptoUtilityImplTest, VerifySignatureBadSignature) {
  std::string public_key = HexDecode(kValidPublicKeyHex);
  std::string public_key_info;
  EXPECT_TRUE(crypto_utility_->GetRSASubjectPublicKeyInfo(public_key,
                                                          &public_key_info));
  std::string output;
  EXPECT_FALSE(crypto_utility_->VerifySignature(NID_sha256, public_key_info,
                                                "input", "signature"));
}

TEST_F(CryptoUtilityImplTest, VerifySignatureBadKey) {
  EXPECT_FALSE(
      crypto_utility_->VerifySignature(NID_sha256, "bad_key", "input", ""));
}

TEST_F(CryptoUtilityImplTest, EncryptDataForGoogle) {
  std::string certificate(10000, 'C');
  EncryptedData encrypted;
  ASSERT_TRUE(crypto_utility_->EncryptDataForGoogle(
      certificate, kValidModulusHex, "ID", &encrypted));
  EXPECT_EQ("ID", encrypted.wrapping_key_id());
  EXPECT_TRUE(encrypted.has_iv());
  EXPECT_TRUE(encrypted.has_mac());
  EXPECT_TRUE(encrypted.has_encrypted_data());
  EXPECT_TRUE(encrypted.has_wrapped_key());
}

TEST_F(CryptoUtilityImplTest, EncryptDataForGoogleBadInput) {
  std::string certificate(10000, 'C');
  EncryptedData encrypted;
  EXPECT_FALSE(crypto_utility_->EncryptDataForGoogle(
      certificate, "bad_public_key", "ID", &encrypted));
  EXPECT_FALSE(encrypted.has_wrapping_key_id());
  EXPECT_FALSE(encrypted.has_iv());
  EXPECT_FALSE(encrypted.has_mac());
  EXPECT_FALSE(encrypted.has_encrypted_data());
  EXPECT_FALSE(encrypted.has_wrapped_key());
}

TEST_F(CryptoUtilityImplTest, KDFaKnownAnswerTest) {
  std::string key = HexDecode(
      "BF88BFC2D0FFA48025830745960EE0A53A66B31E7206321F8FC95B89FD63E8C3");
  std::string context = HexDecode(
      "000BEDB60C6A4E2470EF4804FDE7FA35B94A5338DB7D5B5A3A1DE0E7EB12152A1A95");
  std::string expected_output_256 = HexDecode(
      "C8307D1197CBD3A26E78D5519C26E08661C79D36B528A1089E6156627441ECCD");
  std::string expected_output_128 =
      HexDecode("9FD97E677A4538557783B3442EC41993");
  EXPECT_EQ(expected_output_256, KDFa(key, "STORAGE", context, 256));
  EXPECT_EQ(expected_output_128, KDFa(key, "STORAGE", context, 128));
}

TEST_F(CryptoUtilityImplTest, HmacSha256KnownAnswerTest) {
  std::string key = HexDecode(
      "3DE45E7AC03354DE42C3B02A8D370FBEE98AAEC3F333D1475EC3BB62D4690A07");
  std::string data = HexDecode("706C656173655F686D61635F74686973");
  std::string expected_output = HexDecode(
      "02E3A9D63DA50426AF92AA849FE20D5BA7DB656E8E35CC1E1562D7365F9D893E");

  EXPECT_EQ(expected_output, crypto_utility_->HmacSha256(key, data));
}

TEST_F(CryptoUtilityImplTest, HmacSha512KnownAnswerTest) {
  std::string key = HexDecode(
      "3DE45E7AC03354DE42C3B02A8D370FBEE98AAEC3F333D1475EC3BB62D4690A07");
  std::string data = HexDecode("706C656173655F686D61635F74686973");
  std::string expected_output = HexDecode(
      "573C0E7F8CE36935F5110363CCC240B30225919226730121DC0D1982ECB007CCE2D85C11"
      "15921E13AC8180B4BD9454B1463B897462F9FAD831066E117CF1D077");

  EXPECT_EQ(expected_output, crypto_utility_->HmacSha512(key, data));
}

}  // namespace attestation
