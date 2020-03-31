// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/tpm2_utility_impl.h"

#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <trunks/mock_hmac_session.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_state.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/trunks_factory_for_test.h>

#include "chaps/chaps_utility.h"

using brillo::SecureBlob;
using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using trunks::kStorageRootKey;
using trunks::TPM_RC_FAILURE;
using trunks::TPM_RC_SUCCESS;

namespace chaps {

class TPM2UtilityTest : public testing::Test {
 public:
  TPM2UtilityTest() : factory_(new trunks::TrunksFactoryForTest()) {}
  ~TPM2UtilityTest() override {}

  void SetUp() override {
    factory_->set_tpm(&mock_tpm_);
    factory_->set_tpm_state(&mock_tpm_state_);
    factory_->set_tpm_utility(&mock_tpm_utility_);
    factory_->set_hmac_session(&mock_session_);
  }

  trunks::TPM2B_PUBLIC_KEY_RSA GetValidRSAPublicKey() {
    constexpr char kValidModulus[] =
        "A1D50D088994000492B5F3ED8A9C5FC8772706219F4C063B2F6A8C6B74D3AD6B"
        "212A53D01DABB34A6261288540D420D3BA59ED279D859DE6227A7AB6BD88FADD"
        "FC3078D465F4DF97E03A52A587BD0165AE3B180FE7B255B7BEDC1BE81CB1383F"
        "E9E46F9312B1EF28F4025E7D332E33F4416525FEB8F0FC7B815E8FBB79CDABE6"
        "327B5A155FEF13F559A7086CB8A543D72AD6ECAEE2E704FF28824149D7F4E393"
        "D3C74E721ACA97F7ADBE2CCF7B4BCC165F7380F48065F2C8370F25F066091259"
        "D14EA362BAF236E3CD8771A94BDEDA3900577143A238AB92B6C55F11DEFAFB31"
        "7D1DC5B6AE210C52B008D87F2A7BFF6EB5C4FB32D6ECEC6505796173951A3167";
    std::vector<uint8_t> bytes;
    CHECK(base::HexStringToBytes(kValidModulus, &bytes));
    CHECK_EQ(bytes.size(), 256u);
    trunks::TPM2B_PUBLIC_KEY_RSA rsa;
    rsa.size = bytes.size();
    memcpy(rsa.buffer, bytes.data(), bytes.size());
    return rsa;
  }

  std::string GetRSAPSSParam(CK_MECHANISM_TYPE hashAlg,
                             CK_RSA_PKCS_MGF_TYPE mgf,
                             CK_ULONG sLen) {
    CK_RSA_PKCS_PSS_PARAMS params;
    params.hashAlg = hashAlg;
    params.mgf = mgf;
    params.sLen = sLen;
    std::string param_bytes(reinterpret_cast<char*>(&params),
                            sizeof(CK_RSA_PKCS_PSS_PARAMS));
    return param_bytes;
  }

  trunks::TPMT_PUBLIC GetValidECCPublicKey(
      std::string* der_public_point = nullptr) {
    constexpr char kValidECPointX[] =
        "06845c8f3ac8b98d0e8163d0475ad4c8be1710c9f2d39965719e3684a7b3f40b";
    constexpr char kValidECPointY[] =
        "0400e219928d45093b3d7ff3cae43468e24684454f318b83b12304d1194a3286";
    constexpr char kDerEncodedPublicPoint[] =
        "04410406845C8F3AC8B98D0E8163D0475AD4C8BE1710C9F2D39965719E3684A7"
        "B3F40B0400E219928D45093B3D7FF3CAE43468E24684454F318B83B12304D119"
        "4A3286";

    trunks::TPMT_PUBLIC public_area;
    std::vector<uint8_t> bytes;

    CHECK(base::HexStringToBytes(kValidECPointX, &bytes));
    CHECK_EQ(bytes.size(), base::size(kValidECPointX) / 2);
    public_area.unique.ecc.x.size = bytes.size();
    memcpy(public_area.unique.ecc.x.buffer, bytes.data(), bytes.size());

    bytes.clear();
    CHECK(base::HexStringToBytes(kValidECPointY, &bytes));
    CHECK_EQ(bytes.size(), base::size(kValidECPointY) / 2);
    public_area.unique.ecc.y.size = bytes.size();
    memcpy(public_area.unique.ecc.y.buffer, bytes.data(), bytes.size());

    public_area.type = trunks::TPM_ALG_ECC;
    public_area.parameters.ecc_detail.curve_id = trunks::TPM_ECC_NIST_P256;
    public_area.parameters.ecc_detail.kdf.scheme = trunks::TPM_ALG_NULL;
    public_area.parameters.ecc_detail.scheme.scheme = trunks::TPM_ALG_NULL;

    if (der_public_point) {
      *der_public_point = kDerEncodedPublicPoint;
    }

    return public_area;
  }

 protected:
  std::unique_ptr<trunks::TrunksFactoryForTest> factory_;
  NiceMock<trunks::MockTpm> mock_tpm_;
  NiceMock<trunks::MockTpmState> mock_tpm_state_;
  NiceMock<trunks::MockTpmUtility> mock_tpm_utility_;
  NiceMock<trunks::MockHmacSession> mock_session_;
};

TEST(TPM2Utility_DeathTest, LoadKeyParentBadParent) {
  trunks::TrunksFactoryForTest factory;
  TPM2UtilityImpl utility(&factory);
  std::string key_blob;
  SecureBlob auth_data;
  int key_handle;
  int parent_handle = 42;
  EXPECT_DEATH_IF_SUPPORTED(
      utility.LoadKeyWithParent(1, key_blob, auth_data, parent_handle,
                                &key_handle),
      "Check failed");
}

TEST_F(TPM2UtilityTest, InitSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  EXPECT_CALL(mock_tpm_state_, IsPlatformHierarchyEnabled())
      .WillOnce(Return(false));
  EXPECT_TRUE(utility.Init());
}

TEST_F(TPM2UtilityTest, InitTpmStateInitializationFail) {
  TPM2UtilityImpl utility(factory_.get());
  EXPECT_CALL(mock_tpm_state_, Initialize()).WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.Init());
}

TEST_F(TPM2UtilityTest, InitPlatformHierarchyEnabled) {
  TPM2UtilityImpl utility(factory_.get());
  EXPECT_CALL(mock_tpm_state_, IsPlatformHierarchyEnabled())
      .WillOnce(Return(true));
  EXPECT_FALSE(utility.Init());
}

TEST_F(TPM2UtilityTest, InitTpmNotOwned) {
  TPM2UtilityImpl utility(factory_.get());
  EXPECT_CALL(mock_tpm_state_, IsPlatformHierarchyEnabled())
      .WillOnce(Return(false));
  EXPECT_CALL(mock_tpm_state_, IsOwnerPasswordSet()).WillOnce(Return(false));
  EXPECT_FALSE(utility.Init());
}

#ifndef CHAPS_TPM2_USE_PER_OP_SESSIONS
TEST_F(TPM2UtilityTest, InitTpmNoSession) {
  TPM2UtilityImpl utility(factory_.get());
  EXPECT_CALL(mock_tpm_state_, IsPlatformHierarchyEnabled())
      .WillOnce(Return(false));
  EXPECT_CALL(mock_session_, StartUnboundSession(true, true))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.Init());
}
#endif

TEST_F(TPM2UtilityTest, IsTPMAvailable) {
  TPM2UtilityImpl utility(factory_.get());
  utility.is_enabled_ready_ = true;
  utility.is_enabled_ = true;
  EXPECT_TRUE(utility.IsTPMAvailable());

  utility.is_enabled_ready_ = true;
  utility.is_enabled_ = false;
  EXPECT_FALSE(utility.IsTPMAvailable());

  utility.is_initialized_ = true;
  utility.is_enabled_ready_ = false;
  EXPECT_TRUE(utility.IsTPMAvailable());
  EXPECT_EQ(utility.is_enabled_, true);
  EXPECT_EQ(utility.is_enabled_ready_, true);

  utility.is_initialized_ = false;
  utility.is_enabled_ready_ = false;
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.IsTPMAvailable());

  utility.is_initialized_ = false;
  utility.is_enabled_ready_ = false;
  EXPECT_CALL(mock_tpm_state_, Initialize())
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_state_, IsEnabled()).WillRepeatedly(Return(false));
  EXPECT_FALSE(utility.IsTPMAvailable());
  EXPECT_EQ(utility.is_enabled_, false);
  EXPECT_EQ(utility.is_enabled_ready_, true);
}

TEST_F(TPM2UtilityTest, AuthenticateSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  SecureBlob auth_data;
  SecureBlob new_master_key;
  std::string key_blob;
  std::string encrypted_master;
  EXPECT_TRUE(utility.Authenticate(1, auth_data, key_blob, encrypted_master,
                                   &new_master_key));
}

TEST_F(TPM2UtilityTest, AuthenticateLoadFail) {
  TPM2UtilityImpl utility(factory_.get());
  SecureBlob auth_data;
  SecureBlob new_master_key;
  std::string key_blob;
  std::string encrypted_master;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.Authenticate(1, auth_data, key_blob, encrypted_master,
                                    &new_master_key));
}

TEST_F(TPM2UtilityTest, AuthenticateUnbindFail) {
  TPM2UtilityImpl utility(factory_.get());
  SecureBlob auth_data;
  SecureBlob new_master_key;
  std::string key_blob;
  std::string encrypted_master;
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(_, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.Authenticate(1, auth_data, key_blob, encrypted_master,
                                    &new_master_key));
}

TEST_F(TPM2UtilityTest, ChangeAuthDataSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  SecureBlob old_auth;
  SecureBlob new_auth;
  std::string old_blob;
  std::string new_blob;
  EXPECT_CALL(mock_tpm_utility_, ChangeKeyAuthorizationData(_, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(
      utility.ChangeAuthData(1, old_auth, new_auth, old_blob, &new_blob));
}

TEST_F(TPM2UtilityTest, ChangeAuthDataLoadFail) {
  TPM2UtilityImpl utility(factory_.get());
  SecureBlob old_auth;
  SecureBlob new_auth;
  std::string old_blob;
  std::string new_blob;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(
      utility.ChangeAuthData(1, old_auth, new_auth, old_blob, &new_blob));
}

TEST_F(TPM2UtilityTest, ChangeAuthDataChangeAuthFail) {
  TPM2UtilityImpl utility(factory_.get());
  SecureBlob old_auth;
  SecureBlob new_auth;
  std::string old_blob;
  std::string new_blob;
  EXPECT_CALL(mock_tpm_utility_, ChangeKeyAuthorizationData(_, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(
      utility.ChangeAuthData(1, old_auth, new_auth, old_blob, &new_blob));
}

TEST_F(TPM2UtilityTest, ChangeAuthDataFlushContextFail) {
  TPM2UtilityImpl utility(factory_.get());
  SecureBlob old_auth;
  SecureBlob new_auth;
  std::string old_blob;
  std::string new_blob;
  trunks::TPM_HANDLE key_handle = trunks::TPM_RH_FIRST;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(key_handle), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_, FlushContextSync(key_handle, NULL))
      .WillRepeatedly(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(
      utility.ChangeAuthData(1, old_auth, new_auth, old_blob, &new_blob));
}

TEST_F(TPM2UtilityTest, GenerateRandomSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int num_bytes = 20;
  std::string random_data;
  std::string generated_data(20, 'a');
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(num_bytes, _, &random_data))
      .WillOnce(
          DoAll(SetArgPointee<2>(generated_data), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(utility.GenerateRandom(num_bytes, &random_data));
  EXPECT_EQ(random_data.size(), num_bytes);
}

TEST_F(TPM2UtilityTest, GenerateRandomFail) {
  TPM2UtilityImpl utility(factory_.get());
  int num_bytes = 20;
  std::string random_data;
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(num_bytes, _, &random_data))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.GenerateRandom(num_bytes, &random_data));
}

TEST_F(TPM2UtilityTest, StirRandomSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  std::string entropy_data;
  EXPECT_CALL(mock_tpm_utility_, StirRandom(entropy_data, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.StirRandom(entropy_data));
}

TEST_F(TPM2UtilityTest, StirRandomFail) {
  TPM2UtilityImpl utility(factory_.get());
  std::string entropy_data;
  EXPECT_CALL(mock_tpm_utility_, StirRandom(entropy_data, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.StirRandom(entropy_data));
}

TEST_F(TPM2UtilityTest, GenerateRSAKeySuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int modulus_bits = 2048;
  std::string exponent("\x01\x00\x01", 3);
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_,
              CreateRSAKeyPair(_, modulus_bits, _, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.GenerateRSAKey(1, modulus_bits, exponent, auth_data,
                                     &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, GenerateRSAKeyWrongExponent) {
  TPM2UtilityImpl utility(factory_.get());
  int modulus_bits = 2048;
  std::string exponent(10, 'a');
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_FALSE(utility.GenerateRSAKey(1, modulus_bits, exponent, auth_data,
                                      &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, GenerateRSAKeyModulusTooSmall) {
  TPM2UtilityImpl utility(factory_.get());
  int modulus_bits = 1;
  std::string exponent("\x01\x00\x01", 3);
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_FALSE(utility.GenerateRSAKey(1, modulus_bits, exponent, auth_data,
                                      &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, GenerateRSAKeyCreateFail) {
  TPM2UtilityImpl utility(factory_.get());
  int modulus_bits = 2048;
  std::string exponent("\x01\x00\x01", 3);
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_, CreateRSAKeyPair(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.GenerateRSAKey(1, modulus_bits, exponent, auth_data,
                                      &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, GenerateRSAKeyLoadFail) {
  TPM2UtilityImpl utility(factory_.get());
  int modulus_bits = 2048;
  std::string exponent("\x01\x00\x01", 3);
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.GenerateRSAKey(1, modulus_bits, exponent, auth_data,
                                      &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, GetRSAPublicKeySuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = trunks::TPM_RH_FIRST;
  std::string exponent;
  std::string modulus;
  std::string test_modulus("test");
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.unique.rsa = trunks::Make_TPM2B_PUBLIC_KEY_RSA(test_modulus);
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(utility.GetRSAPublicKey(key_handle, &exponent, &modulus));
  EXPECT_EQ(modulus.compare(test_modulus), 0);
}

TEST_F(TPM2UtilityTest, GetRSAPublicKeyFail) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = trunks::TPM_RH_FIRST;
  std::string exponent;
  std::string modulus;
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.GetRSAPublicKey(key_handle, &exponent, &modulus));
}

TEST_F(TPM2UtilityTest, WrapRSAKeySuccess) {
  TPM2UtilityImpl utility(factory_.get());
  std::string exponent("\x01\x00\x01", 3);
  std::string modulus(2048, 'a');
  std::string prime_factor;
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_,
              ImportRSAKey(_, modulus, _, prime_factor, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.WrapRSAKey(1, exponent, modulus, prime_factor, auth_data,
                                 &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, WrapRSAKeyWrongExponent) {
  TPM2UtilityImpl utility(factory_.get());
  std::string exponent(10, 'a');
  std::string modulus(2048, 'a');
  std::string prime_factor;
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_FALSE(utility.WrapRSAKey(1, exponent, modulus, prime_factor, auth_data,
                                  &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, WrapRSAKeyImportFail) {
  TPM2UtilityImpl utility(factory_.get());
  std::string exponent("\x01\x00\x01", 3);
  std::string modulus(2048, 'a');
  std::string prime_factor;
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_, ImportRSAKey(_, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.WrapRSAKey(1, exponent, modulus, prime_factor, auth_data,
                                  &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, WrapRSAKeyLoadFail) {
  TPM2UtilityImpl utility(factory_.get());
  std::string exponent("\x01\x00\x01", 3);
  std::string modulus(2048, 'a');
  std::string prime_factor;
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_, ImportRSAKey(_, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.WrapRSAKey(1, exponent, modulus, prime_factor, auth_data,
                                  &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, WrapECCKeySuccess) {
  TPM2UtilityImpl utility(factory_.get());
  const std::string public_point_x("public_point_x");
  const std::string public_point_y("public_point_y");
  const std::string private_value("private_value");
  const SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_,
              ImportECCKey(_, trunks::TPM_ECC_NIST_P256, public_point_x,
                           public_point_y, private_value, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.WrapECCKey(1, NID_X9_62_prime256v1, public_point_x,
                                 public_point_y, private_value, auth_data,
                                 &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, WrapECCKeyImportFail) {
  TPM2UtilityImpl utility(factory_.get());
  const std::string public_point_x;
  const std::string public_point_y;
  const std::string private_value;
  const SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_, ImportECCKey(_, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.WrapECCKey(1, NID_X9_62_prime256v1, public_point_x,
                                  public_point_y, private_value, auth_data,
                                  &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, WrapECCKeyLoadFail) {
  TPM2UtilityImpl utility(factory_.get());
  const std::string public_point_x;
  const std::string public_point_y;
  const std::string private_value;
  const SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_, ImportECCKey(_, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.WrapECCKey(1, NID_X9_62_prime256v1, public_point_x,
                                  public_point_y, private_value, auth_data,
                                  &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, LoadKeySuccess) {
  TPM2UtilityImpl utility(factory_.get());
  std::string key_blob;
  SecureBlob auth_data;
  int key_handle = 10;  // any value is acceptable.
  int slot = 1;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.LoadKey(slot, key_blob, auth_data, &key_handle));
  auto slot_set = utility.slot_handles_[slot];
  EXPECT_TRUE(slot_set.find(key_handle) != slot_set.end());
}

TEST_F(TPM2UtilityTest, LoadKeyFail) {
  TPM2UtilityImpl utility(factory_.get());
  std::string key_blob;
  SecureBlob auth_data;
  int key_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.LoadKey(1, key_blob, auth_data, &key_handle));
}

TEST_F(TPM2UtilityTest, LoadKeyParentSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  std::string key_blob;
  SecureBlob auth_data;
  int key_handle;
  int parent_handle = kStorageRootKey;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.LoadKeyWithParent(1, key_blob, auth_data, parent_handle,
                                        &key_handle));
}

TEST_F(TPM2UtilityTest, LoadKeyParentLoadFail) {
  TPM2UtilityImpl utility(factory_.get());
  std::string key_blob;
  SecureBlob auth_data;
  int key_handle;
  int parent_handle = kStorageRootKey;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.LoadKeyWithParent(1, key_blob, auth_data, parent_handle,
                                         &key_handle));
}

TEST_F(TPM2UtilityTest, LoadKeyParentNameFail) {
  TPM2UtilityImpl utility(factory_.get());
  std::string key_blob;
  SecureBlob auth_data;
  int key_handle = 32;
  int parent_handle = kStorageRootKey;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, GetKeyName(key_handle, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.LoadKeyWithParent(1, key_blob, auth_data, parent_handle,
                                         &key_handle));
}

TEST_F(TPM2UtilityTest, UnloadKeysTest) {
  TPM2UtilityImpl utility(factory_.get());
  int slot1 = 1;
  int slot2 = 2;
  int key_handle1 = 1;
  int key_handle2 = 2;
  int key_handle3 = 3;
  utility.slot_handles_[slot1].insert(key_handle1);
  utility.slot_handles_[slot1].insert(key_handle2);
  utility.slot_handles_[slot2].insert(key_handle3);
  EXPECT_CALL(mock_tpm_, FlushContextSync(_, _))
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_, FlushContextSync(key_handle1, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_, FlushContextSync(key_handle2, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  utility.UnloadKeysForSlot(slot1);
  auto slot1_set = utility.slot_handles_[slot1];
  auto slot2_set = utility.slot_handles_[slot2];
  EXPECT_TRUE(slot1_set.find(key_handle1) == slot1_set.end());
  EXPECT_TRUE(slot1_set.find(key_handle2) == slot1_set.end());
  EXPECT_TRUE(slot2_set.find(key_handle3) != slot2_set.end());
}

TEST_F(TPM2UtilityTest, BindSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input("input");
  std::string output;

  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(utility.Bind(key_handle, input, &output));
}

TEST_F(TPM2UtilityTest, UnbindSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input;
  std::string output;
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(key_handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.Unbind(key_handle, input, &output));
}

TEST_F(TPM2UtilityTest, UnbindFailure) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input;
  std::string output;
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(key_handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.Unbind(key_handle, input, &output));
}

TEST_F(TPM2UtilityTest, SignRsaSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "abcd";
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, Sign(key_handle, trunks::TPM_ALG_RSASSA,
                                      trunks::TPM_ALG_SHA1, input, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS, "", input, &output));
}

TEST_F(TPM2UtilityTest, SignSignOnlyRsaPssSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "01234567890123456789";
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, Sign(key_handle, trunks::TPM_ALG_RSAPSS,
                                      trunks::TPM_ALG_SHA1, input, false, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS_PSS,
                           GetRSAPSSParam(CKM_SHA_1, CKG_MGF1_SHA1, 20), input,
                           &output));
}

TEST_F(TPM2UtilityTest, SignSignOnlyRsaPssInvalidMGF) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "01234567890123456789";
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  // Sign only key doesn't support MGF with a hash algorithm that differs from
  // the signing mechanism.
  EXPECT_FALSE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS_PSS,
                            GetRSAPSSParam(CKM_SHA_1, CKG_MGF1_SHA256, 20),
                            input, &output));
}

TEST_F(TPM2UtilityTest, SignSignOnlyRsaPssInvalidSize) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "0";
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  // Size mismatch between the input data (size 1 byte) and the SHA1 algorithm
  // (20 bytes).
  EXPECT_FALSE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS_PSS,
                            GetRSAPSSParam(CKM_SHA_1, CKG_MGF1_SHA1, 20), input,
                            &output));
}

TEST_F(TPM2UtilityTest, SignUnrestrictedRsaPssSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "01234567890123456789";
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign | trunks::kDecrypt;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  std::string padded_input;
  EXPECT_CALL(mock_tpm_utility_,
              AsymmetricDecrypt(key_handle, trunks::TPM_ALG_NULL,
                                trunks::TPM_ALG_NULL, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&padded_input), Return(TPM_RC_SUCCESS)));

  EXPECT_TRUE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS_PSS,
                           GetRSAPSSParam(CKM_SHA_1, CKG_MGF1_SHA1, 20), input,
                           &output));

  // Check the input is correctly padded with RSA PSS.

  // Create RSA object for use with OpenSSL
  crypto::ScopedRSA rsa = utility.PublicAreaToScopedRsa(public_data);
  EXPECT_TRUE(rsa);
  EXPECT_EQ(RSA_verify_PKCS1_PSS_mgf1(
                rsa.get(), reinterpret_cast<const uint8_t*>(base::data(input)),
                EVP_sha1(), EVP_sha1(),
                reinterpret_cast<const uint8_t*>(base::data(padded_input)), 20),
            1);
}

TEST_F(TPM2UtilityTest, SignUnrestrictedGenericRsaPssSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "01234567890123456789";
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign | trunks::kDecrypt;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  std::string padded_input;
  EXPECT_CALL(mock_tpm_utility_,
              AsymmetricDecrypt(key_handle, trunks::TPM_ALG_NULL,
                                trunks::TPM_ALG_NULL, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&padded_input), Return(TPM_RC_SUCCESS)));

  EXPECT_TRUE(utility.Sign(key_handle, CKM_RSA_PKCS_PSS,
                           GetRSAPSSParam(CKM_SHA_1, CKG_MGF1_SHA1, 20), input,
                           &output));

  // Check the input is correctly padded with RSA PSS.

  // Create RSA object for use with OpenSSL
  crypto::ScopedRSA rsa = utility.PublicAreaToScopedRsa(public_data);
  EXPECT_TRUE(rsa);
  EXPECT_EQ(RSA_verify_PKCS1_PSS_mgf1(
                rsa.get(), reinterpret_cast<const uint8_t*>(base::data(input)),
                EVP_sha1(), EVP_sha1(),
                reinterpret_cast<const uint8_t*>(base::data(padded_input)), 20),
            1);
}

TEST_F(TPM2UtilityTest, SignUnrestrictedGenericRsaPssInvalidHashAlg) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "01234567890123456789";
  std::string output;

  // CKM_RC4 is not a valid signature mechanism, and the call should fail.
  EXPECT_FALSE(utility.Sign(key_handle, CKM_RSA_PKCS_PSS,
                            GetRSAPSSParam(CKM_RC4, CKG_MGF1_SHA1, 20), input,
                            &output));
}

TEST_F(TPM2UtilityTest, SignEccSuccess) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "abcd";
  std::string output;
  trunks::TPMT_PUBLIC public_data = GetValidECCPublicKey();

  trunks::TPMT_SIGNATURE tpm_signature = {};
  tpm_signature.sig_alg = trunks::TPM_ALG_ECDSA;
  tpm_signature.signature.ecdsa.signature_r =
      trunks::Make_TPM2B_ECC_PARAMETER("12");
  tpm_signature.signature.ecdsa.signature_s =
      trunks::Make_TPM2B_ECC_PARAMETER("34");
  std::string tpm_signature_str;
  Serialize_TPMT_SIGNATURE(tpm_signature, &tpm_signature_str);

  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, Sign(key_handle, trunks::TPM_ALG_ECDSA,
                                      trunks::TPM_ALG_SHA1, input, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<6>(tpm_signature_str), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_ECDSA_SHA1, "", input, &output));

  EXPECT_EQ(output, "1234");
}

TEST_F(TPM2UtilityTest, SignSuccessWithDecrypt) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "abcd";
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign | trunks::kDecrypt;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  std::string padded_input;
  EXPECT_CALL(mock_tpm_utility_,
              AsymmetricDecrypt(key_handle, trunks::TPM_ALG_NULL,
                                trunks::TPM_ALG_NULL, _, _, _))
      .WillOnce(DoAll(SaveArg<3>(&padded_input), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS, "", input, &output));

  // Check the input is PKCS1 padded.
  EXPECT_EQ(padded_input.size(), public_data.unique.rsa.size);
  // Check the input is already added DigestInfo
  EXPECT_TRUE(padded_input.find(GetDigestAlgorithmEncoding(
                  DigestAlgorithm::SHA1)) != std::string::npos);
  // Check the input still contains the original input
  EXPECT_TRUE(padded_input.find(input) != std::string::npos);
}

TEST_F(TPM2UtilityTest, SignFailure) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input;
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, Sign(key_handle, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS, "", input, &output));
}

TEST_F(TPM2UtilityTest, SignFailureWithDecrypt) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input;
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign | trunks::kDecrypt;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(key_handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS, "", input, &output));
}

TEST_F(TPM2UtilityTest, SignFailureBadKeySize) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input;
  std::string output;
  trunks::TPMT_PUBLIC public_data = {};
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.object_attributes = trunks::kSign | trunks::kDecrypt;
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, Sign(key_handle, _, _, _, _, _, _)).Times(0);
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(key_handle, _, _, _, _, _))
      .Times(0);
  EXPECT_FALSE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS, "", input, &output));
}

TEST_F(TPM2UtilityTest, SignFailurePublicArea) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input;
  std::string output;
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_CALL(mock_tpm_utility_, Sign(key_handle, _, _, _, _, _, _)).Times(0);
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(key_handle, _, _, _, _, _))
      .Times(0);
  EXPECT_FALSE(utility.Sign(key_handle, CKM_SHA1_RSA_PKCS, "", input, &output));
}

TEST_F(TPM2UtilityTest, SignSuccessWithUnknownAlgorithm) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string input = "test";
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));

  EXPECT_CALL(mock_tpm_utility_, Sign(key_handle, trunks::TPM_ALG_RSASSA,
                                      trunks::TPM_ALG_NULL, input, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_RSA_PKCS, "", input, &output));

  // For the digest algorithms which are not supported by TPM, we will prepend
  // DigestInfo and use Sign with NULL scheme
  const std::string kInputWithMd5DigestInfo =
      GetDigestAlgorithmEncoding(DigestAlgorithm::MD5) + input;
  EXPECT_CALL(mock_tpm_utility_,
              Sign(key_handle, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_NULL,
                   kInputWithMd5DigestInfo, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_MD5_RSA_PKCS, "", input, &output));
}

TEST_F(TPM2UtilityTest, SignSuccessWithPrependDigestForKnownAlgorithm) {
  TPM2UtilityImpl utility(factory_.get());
  int key_handle = 43;
  std::string output;
  trunks::TPMT_PUBLIC public_data;
  public_data.type = trunks::TPM_ALG_RSA;
  public_data.parameters.rsa_detail.exponent = 0x10001;
  public_data.object_attributes = trunks::kSign;
  public_data.unique.rsa = GetValidRSAPublicKey();
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(key_handle, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));

  // For prepended DigestInfo data, we will strip the digest if the algorithm is
  // supported by TPM
  // Test all SHA1, SHA256, SHA384, SHA512
  const std::string kDigestOfSHA1 = std::string(SHA1_DIGEST_SIZE, 'a');
  const std::string kInputWithSHA1DigestInfo =
      GetDigestAlgorithmEncoding(DigestAlgorithm::SHA1) + kDigestOfSHA1;
  EXPECT_CALL(mock_tpm_utility_,
              Sign(key_handle, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA1,
                   kDigestOfSHA1, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_RSA_PKCS, "",
                           kInputWithSHA1DigestInfo, &output));

  const std::string kDigestOfSHA256 = std::string(SHA256_DIGEST_SIZE, 'b');
  const std::string kInputWithSHA256DigestInfo =
      GetDigestAlgorithmEncoding(DigestAlgorithm::SHA256) + kDigestOfSHA256;
  EXPECT_CALL(mock_tpm_utility_,
              Sign(key_handle, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA256,
                   kDigestOfSHA256, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_RSA_PKCS, "",
                           kInputWithSHA256DigestInfo, &output));

  const std::string kDigestOfSHA384 = std::string(SHA384_DIGEST_SIZE, 'c');
  const std::string kInputWithSHA384DigestInfo =
      GetDigestAlgorithmEncoding(DigestAlgorithm::SHA384) + kDigestOfSHA384;
  EXPECT_CALL(mock_tpm_utility_,
              Sign(key_handle, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA384,
                   kDigestOfSHA384, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_RSA_PKCS, "",
                           kInputWithSHA384DigestInfo, &output));

  const std::string kDigestOfSHA512 = std::string(SHA512_DIGEST_SIZE, 'd');
  const std::string kInputWithSHA512DigestInfo =
      GetDigestAlgorithmEncoding(DigestAlgorithm::SHA512) + kDigestOfSHA512;
  EXPECT_CALL(mock_tpm_utility_,
              Sign(key_handle, trunks::TPM_ALG_RSASSA, trunks::TPM_ALG_SHA512,
                   kDigestOfSHA512, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(utility.Sign(key_handle, CKM_RSA_PKCS, "",
                           kInputWithSHA512DigestInfo, &output));
}

TEST_F(TPM2UtilityTest, GenerateECCKeySuccess) {
  TPM2UtilityImpl utility(factory_.get());
  constexpr int kTrunksCurveId = trunks::TPM_ECC_NIST_P256;
  EXPECT_CALL(mock_tpm_utility_,
              CreateECCKeyPair(_, kTrunksCurveId, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));

  constexpr int kSlot = 0;
  constexpr int kNid = NID_X9_62_prime256v1;
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_TRUE(
      utility.GenerateECCKey(kSlot, kNid, auth_data, &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, UsedNotSupportedECCurve) {
  TPM2UtilityImpl utility(factory_.get());
  constexpr int kSlot = 0;
  constexpr int kBadNid = 0;
  EXPECT_FALSE(utility.IsECCurveSupported(kBadNid));
  std::string key_blob;
  int key_handle;
  EXPECT_FALSE(utility.GenerateECCKey(kSlot, kBadNid, SecureBlob(), &key_blob,
                                      &key_handle));
}

TEST_F(TPM2UtilityTest, GenerateECCKeyCreateFail) {
  TPM2UtilityImpl utility(factory_.get());
  EXPECT_CALL(mock_tpm_utility_, CreateECCKeyPair(_, _, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  constexpr int kSlot = 0;
  constexpr int kNid = NID_X9_62_prime256v1;
  SecureBlob auth_data;
  std::string key_blob;
  int key_handle;
  EXPECT_FALSE(
      utility.GenerateECCKey(kSlot, kNid, auth_data, &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, GenerateECCKeyLoadFail) {
  TPM2UtilityImpl utility(factory_.get());
  std::string key_blob;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(key_blob, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  constexpr int kSlot = 0;
  constexpr int kNid = NID_X9_62_prime256v1;
  SecureBlob auth_data;
  int key_handle;
  EXPECT_FALSE(
      utility.GenerateECCKey(kSlot, kNid, auth_data, &key_blob, &key_handle));
}

TEST_F(TPM2UtilityTest, GetECCPublicKeySuccess) {
  TPM2UtilityImpl utility(factory_.get());
  constexpr int kKeyHandle = trunks::TPM_RH_FIRST;

  std::string der_encoded_public_point;
  trunks::TPMT_PUBLIC public_data =
      GetValidECCPublicKey(&der_encoded_public_point);

  std::string public_point;
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(kKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(utility.GetECCPublicKey(kKeyHandle, &public_point));

  EXPECT_EQ(base::HexEncode(public_point.data(), public_point.size()),
            der_encoded_public_point);
}

TEST_F(TPM2UtilityTest, GetECCPublicKeyFail) {
  TPM2UtilityImpl utility(factory_.get());
  constexpr int kKeyHandle = trunks::TPM_RH_FIRST;
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(kKeyHandle, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  std::string public_point;
  EXPECT_FALSE(utility.GetECCPublicKey(kKeyHandle, &public_point));
}

}  // namespace chaps
