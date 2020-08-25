// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <tpm_manager/common/mock_tpm_ownership_interface.h>
#include <trunks/mock_authorization_delegate.h>
#include <trunks/mock_policy_session.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/trunks_factory_for_test.h>

#include "sealed_storage/sealed_storage.h"

using testing::_;
using testing::AtLeast;
using testing::Expectation;
using testing::ExpectationSet;
using testing::InSequence;
using testing::Mock;
using testing::Return;
using testing::WithArgs;
using tpm_manager::TpmOwnershipInterface;

constexpr uint8_t kRandomByte = 0x12;
constexpr uint16_t kExpectedIVSize = 16; /* AES IV size */
constexpr uint8_t kDftPolicyFill = 0x23;
constexpr uint8_t kWrongPolicyFill = 0x45;
constexpr size_t kPolicyDigestSize = 32; /* SHA-256 digest */
constexpr size_t kPcrValueSize = 32;     /* SHA-256 digest */

bool operator==(const trunks::TPM2B_ECC_PARAMETER& rh,
                const trunks::TPM2B_ECC_PARAMETER& lh) {
  return rh.size == lh.size && memcmp(rh.buffer, lh.buffer, rh.size) == 0;
}
bool operator==(const trunks::TPMS_ECC_POINT& rh,
                const trunks::TPMS_ECC_POINT& lh) {
  return rh.x == lh.x && rh.y == lh.y;
}
bool operator==(const trunks::TPM2B_ECC_POINT& rh,
                const trunks::TPM2B_ECC_POINT& lh) {
  return rh.size == lh.size && rh.point == lh.point;
}

MATCHER_P(Equals, value, "") {
  return arg == value;
}

namespace sealed_storage {

class SealedStorageTest : public ::testing::Test {
 public:
  SealedStorageTest() = default;
  virtual ~SealedStorageTest() = default;

  static SecretData DftDataToSeal() { return SecretData("testdata"); }

  static std::string DftPolicyDigest() {
    return std::string(kPolicyDigestSize, kDftPolicyFill);
  }

  static std::string WrongPolicyDigest() {
    return std::string(kPolicyDigestSize, kWrongPolicyFill);
  }

  static trunks::TPM2B_ECC_POINT GetECPointWithFilledXY(uint8_t x_fill,
                                                        uint8_t y_fill) {
    const trunks::TPMS_ECC_POINT point = {
        trunks::Make_TPM2B_ECC_PARAMETER(
            std::string(MAX_ECC_KEY_BYTES, x_fill)),
        trunks::Make_TPM2B_ECC_PARAMETER(
            std::string(MAX_ECC_KEY_BYTES, y_fill))};
    return trunks::Make_TPM2B_ECC_POINT(point);
  }

  static trunks::TPM2B_ECC_POINT DftPubPoint() {
    return GetECPointWithFilledXY(0x11, 0x22);
  }

  static trunks::TPM2B_ECC_POINT DftZPoint() {
    return GetECPointWithFilledXY(0x33, 0x44);
  }

  static trunks::TPM2B_ECC_POINT WrongZPoint() {
    return GetECPointWithFilledXY(0x55, 0x66);
  }

  static Policy ConstructEmptyPolicy() { return {}; }

  static Policy ConstructPcrBoundPolicy() {
    Policy::PcrMap pcr_map;

    for (uint8_t pcr = 0; pcr < 10; pcr++) {
      pcr_map.emplace(pcr, ConstructPcrValue(pcr));
    }

    return {pcr_map};
  }

  static std::string ConstructPcrValue(uint8_t pcr) {
    return std::string(kPcrValueSize, pcr ^ 0xFF);
  }

  // Convert the sealed data blob of the default version produced by Seal()
  // into V1 blob.
  static void ConvertToV1(Data* sealed_data) {
    constexpr size_t kAdditionalV2DataSize =
        /* plain size */ sizeof(uint16_t) +
        /* policy digest */ sizeof(uint16_t) + kPolicyDigestSize;
    (*sealed_data)[0] = 0x01;
    sealed_data->erase(sealed_data->cbegin() + 1,
                       sealed_data->cbegin() + 1 + kAdditionalV2DataSize);
  }

  // Sets up a pair of ZPoints returned from KeyGen and ZGen with the following
  // properties: if you encrypt a particular data_to_seal with the first ZPoint
  // (returned from KeyGen) and then decrypt it with the second ZPoint (returned
  // from ZGen), decryption returns success as it produces valid padding (but
  // not the same data, of course).
  // Returns data_to_sign to be used with the setup ZPoints.
  const SecretData SetupWrongZPointWithGarbageData() {
    z_point_ = GetECPointWithFilledXY(0x11, 0x11);          // KeyGen
    z_gen_out_point_ = GetECPointWithFilledXY(0x0F, 0x00);  // ZGen
    return SecretData("testdata");
  }

  void SetUp() override {
    trunks_factory_.set_tpm(&tpm_);
    trunks_factory_.set_tpm_utility(&tpm_utility_);
    trunks_factory_.set_policy_session(&policy_session_);

    ON_CALL(tpm_ownership_, GetTpmStatus(_, _))
        .WillByDefault(
            WithArgs<1>(Invoke(this, &SealedStorageTest::GetTpmStatus)));

    ON_CALL(tpm_, CreatePrimarySyncShort(_, _, _, _, _, _, _, _, _, _))
        .WillByDefault(
            Invoke(this, &SealedStorageTest::CreatePrimarySyncShort));
    ON_CALL(tpm_, ECDH_KeyGenSync(_, _, _, _, _))
        .WillByDefault(Invoke(this, &SealedStorageTest::ECDH_KeyGenSync));
    ON_CALL(tpm_, ECDH_ZGenSync(_, _, _, _, _))
        .WillByDefault(Invoke(this, &SealedStorageTest::ECDH_ZGenSync));
    ON_CALL(tpm_, GetRandomSync(_, _, _))
        .WillByDefault(Invoke(this, &SealedStorageTest::GetRandomSync));
    ON_CALL(tpm_utility_, GetPolicyDigestForPcrValues(_, _, _))
        .WillByDefault(
            Invoke(this, &SealedStorageTest::GetPolicyDigestForPcrValues));
    ON_CALL(policy_session_, PolicyPCR(_))
        .WillByDefault(Invoke(this, &SealedStorageTest::PolicyPCR));
    ON_CALL(policy_session_, GetDelegate())
        .WillByDefault(Return(&auth_delegate_));
  }

  void GetTpmStatus(
      const TpmOwnershipInterface::GetTpmStatusCallback& callback) {
    tpm_manager::GetTpmStatusReply reply;
    reply.set_status(tpm_manager_result_);
    reply.mutable_local_data()->set_endorsement_password(endorsement_password_);
    callback.Run(reply);
  }

  trunks::TPM_RC CreatePrimarySyncShort(
      const trunks::TPMI_RH_HIERARCHY& primary_handle,
      const trunks::TPM2B_PUBLIC& in_public,
      const trunks::TPML_PCR_SELECTION& creation_pcr,
      trunks::TPM_HANDLE* object_handle,
      trunks::TPM2B_PUBLIC* out_public,
      trunks::TPM2B_CREATION_DATA* creation_data,
      trunks::TPM2B_DIGEST* creation_hash,
      trunks::TPMT_TK_CREATION* creation_ticket,
      trunks::TPM2B_NAME* name,
      trunks::AuthorizationDelegate* authorization_delegate) {
    create_primary_public_area_ = in_public;
    *object_handle = sealing_key_handle_;
    return create_primary_result_;
  }

  trunks::TPM_RC ECDH_KeyGenSync(
      const trunks::TPMI_DH_OBJECT& key_handle,
      const std::string& key_handle_name,
      trunks::TPM2B_ECC_POINT* z_point,
      trunks::TPM2B_ECC_POINT* pub_point,
      trunks::AuthorizationDelegate* authorization_delegate) {
    *z_point = z_point_;
    *pub_point = pub_point_;
    return key_gen_result_;
  }

  trunks::TPM_RC ECDH_ZGenSync(
      const trunks::TPMI_DH_OBJECT& key_handle,
      const std::string& key_handle_name,
      const trunks::TPM2B_ECC_POINT& in_point,
      trunks::TPM2B_ECC_POINT* out_point,
      trunks::AuthorizationDelegate* authorization_delegate) {
    *out_point = z_gen_out_point_;
    z_gen_in_point_ = in_point;
    return z_gen_result_;
  }

  trunks::TPM_RC GetPolicyDigestForPcrValues(
      const std::map<uint32_t, std::string>& pcr_map,
      bool use_auth_value,
      std::string* policy_digest) {
    *policy_digest = policy_digest_;
    return get_policy_digest_result_;
  }

  trunks::TPM_RC GetRandomSync(
      const trunks::UINT16& bytes_requested,
      trunks::TPM2B_DIGEST* random_bytes,
      trunks::AuthorizationDelegate* authorization_delegate) {
    if (random_.has_value()) {
      *random_bytes = trunks::Make_TPM2B_DIGEST(random_.value());
    } else {
      random_bytes->size = bytes_requested;
      memset(random_bytes->buffer, kRandomByte, bytes_requested);
    }
    return get_random_result_;
  }

  trunks::TPM_RC PolicyPCR(const std::map<uint32_t, std::string>& pcr_map) {
    return policy_pcr_result_;
  }

  void ExpectCommandSequence(bool do_seal = true, bool do_unseal = true) {
    bool use_empty_policy = policy_.pcr_map.empty();
    ExpectationSet seal_commands;

    /* Seal: Create policy digest */
    if (use_empty_policy) {
      EXPECT_CALL(tpm_utility_, GetPolicyDigestForPcrValues(_, _, _)).Times(0);
    } else {
      EXPECT_CALL(tpm_utility_,
                  GetPolicyDigestForPcrValues(policy_.pcr_map, _, _))
          .Times(AtLeast(1));
    }

    if (do_seal) {
      /* Seal: Create sealing key */
      Expectation status1 = EXPECT_CALL(tpm_ownership_, GetTpmStatus(_, _));
      seal_commands += status1;
      if (tpm_manager_result_ != tpm_manager::STATUS_SUCCESS) {
        /* In case of GetTpmStatus error... */
        EXPECT_CALL(tpm_, CreatePrimarySyncShort(_, _, _, _, _, _, _, _, _, _))
            .Times(0);
        return;
      }

      Expectation create_primary1 =
          EXPECT_CALL(tpm_, CreatePrimarySyncShort(trunks::TPM_RH_ENDORSEMENT,
                                                   _, _, _, _, _, _, _, _, _))
              .After(status1);
      seal_commands += create_primary1;
      if (create_primary_result_ != trunks::TPM_RC_SUCCESS) {
        /* In case of CreatePrimary error... */
        EXPECT_CALL(tpm_, ECDH_KeyGenSync(_, _, _, _, _)).Times(0);
        return;
      }

      /* Seal: Generate seeds */
      Expectation key_gen =
          EXPECT_CALL(tpm_, ECDH_KeyGenSync(sealing_key_handle_, _, _, _, _))
              .After(create_primary1);
      seal_commands += key_gen;
      Expectation get_random =
          EXPECT_CALL(tpm_, GetRandomSync(kExpectedIVSize, _, _));
      seal_commands += get_random;
    }

    if (do_unseal) {
      /* Unseal: Create sealing key */
      Expectation status2 =
          EXPECT_CALL(tpm_ownership_, GetTpmStatus(_, _)).After(seal_commands);
      if (tpm_manager_result_ != tpm_manager::STATUS_SUCCESS) {
        /* In case of GetTpmStatus error... */
        EXPECT_CALL(tpm_, CreatePrimarySyncShort(_, _, _, _, _, _, _, _, _, _))
            .Times(0);
        return;
      }

      Expectation create_primary2 =
          EXPECT_CALL(tpm_, CreatePrimarySyncShort(trunks::TPM_RH_ENDORSEMENT,
                                                   _, _, _, _, _, _, _, _, _))
              .After(status2);
      if (create_primary_result_ != trunks::TPM_RC_SUCCESS) {
        /* In case of CreatePrimary error... */
        EXPECT_CALL(tpm_, ECDH_ZGenSync(_, _, _, _, _)).Times(0);
        return;
      }

      /* Unseal: Restore seeds */
      Expectation start_session =
          EXPECT_CALL(policy_session_, StartUnboundSession(_, _))
              .After(seal_commands);
      if (use_empty_policy) {
        EXPECT_CALL(policy_session_, PolicyPCR(_)).Times(0);
      } else {
        EXPECT_CALL(policy_session_, PolicyPCR(policy_.pcr_map))
            .After(start_session);
        if (policy_pcr_result_ != trunks::TPM_RC_SUCCESS) {
          /* In case of CreatePrimary error... */
          EXPECT_CALL(tpm_, ECDH_ZGenSync(_, _, _, _, _)).Times(0);
          return;
        }
      }
      Expectation get_delegate =
          EXPECT_CALL(policy_session_, GetDelegate()).After(start_session);
      EXPECT_CALL(tpm_, ECDH_ZGenSync(sealing_key_handle_, _,
                                      Equals(pub_point_), _, &auth_delegate_))
          .After(create_primary2, get_delegate);
    }
  }

  void ResetMocks() {
    Mock::VerifyAndClearExpectations(&tpm_ownership_);
    Mock::VerifyAndClearExpectations(&tpm_utility_);
    Mock::VerifyAndClearExpectations(&tpm_);
    Mock::VerifyAndClearExpectations(&policy_session_);
  }

  void SealUnseal(bool expect_seal_success,
                  bool expect_unseal_success,
                  const SecretData& data_to_seal) {
    sealed_storage_.reset_policy(policy_);
    auto sealed_data = sealed_storage_.Seal(data_to_seal);
    EXPECT_EQ(sealed_data.has_value(), expect_seal_success);
    if (!sealed_data.has_value()) {
      return;
    }

    auto result = sealed_storage_.Unseal(sealed_data.value());
    EXPECT_EQ(result.has_value(), expect_unseal_success);
    if (expect_unseal_success && result.has_value()) {
      EXPECT_EQ(result.value(), data_to_seal);
    }
  }

 protected:
  base::MessageLoop loop_{base::MessageLoop::TYPE_IO};
  Policy policy_;
  trunks::MockTpm tpm_;
  trunks::MockTpmUtility tpm_utility_;
  trunks::MockAuthorizationDelegate auth_delegate_;
  trunks::MockPolicySession policy_session_;
  trunks::TrunksFactoryForTest trunks_factory_;
  testing::StrictMock<tpm_manager::MockTpmOwnershipInterface> tpm_ownership_;
  SealedStorage sealed_storage_{policy_, &trunks_factory_, &tpm_ownership_};

  tpm_manager::TpmManagerStatus tpm_manager_result_ =
      tpm_manager::STATUS_SUCCESS;
  std::string endorsement_password_ = "endorsement_password";

  trunks::TPM_RC create_primary_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM_HANDLE sealing_key_handle_ = trunks::TRANSIENT_FIRST;
  trunks::TPM2B_PUBLIC create_primary_public_area_ = {};

  trunks::TPM_RC key_gen_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM2B_ECC_POINT z_point_ = DftZPoint();
  trunks::TPM2B_ECC_POINT pub_point_ = DftPubPoint();

  trunks::TPM_RC z_gen_result_ = trunks::TPM_RC_SUCCESS;
  trunks::TPM2B_ECC_POINT z_gen_out_point_ = DftZPoint();
  trunks::TPM2B_ECC_POINT z_gen_in_point_ = {};

  trunks::TPM_RC get_random_result_ = trunks::TPM_RC_SUCCESS;
  base::Optional<std::string> random_ = {};

  trunks::TPM_RC get_policy_digest_result_ = trunks::TPM_RC_SUCCESS;
  std::string policy_digest_ = DftPolicyDigest();

  trunks::TPM_RC policy_pcr_result_ = trunks::TPM_RC_SUCCESS;

 private:
  DISALLOW_COPY_AND_ASSIGN(SealedStorageTest);
};

TEST_F(SealedStorageTest, TrivialPolicySuccess) {
  ExpectCommandSequence();
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageTest, VariousPlaintextSizesSuccess) {
  for (size_t data_size = 0; data_size <= 65; data_size++) {
    std::string data(data_size, 'x');
    ExpectCommandSequence();
    SealUnseal(true, true, SecretData(data));
    ResetMocks();
  }
}

TEST_F(SealedStorageTest, PcrBoundPolicySuccess) {
  policy_ = ConstructPcrBoundPolicy();
  ExpectCommandSequence();
  SealUnseal(true, true, DftDataToSeal());
}

TEST_F(SealedStorageTest, WrongRestoredZPointError) {
  z_gen_out_point_ = WrongZPoint();
  ExpectCommandSequence();
  SealUnseal(true, false, DftDataToSeal());
}

TEST_F(SealedStorageTest, WrongDeviceStateError) {
  policy_ = ConstructPcrBoundPolicy();
  policy_pcr_result_ = trunks::TPM_RC_VALUE;
  ExpectCommandSequence();
  SealUnseal(true, false, DftDataToSeal());
}

TEST_F(SealedStorageTest, WrongRestoredZPointGarbage) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructPcrBoundPolicy();
  ExpectCommandSequence();
  SealUnseal(true, false, data_to_seal);
}

TEST_F(SealedStorageTest, WrongPolicy) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructPcrBoundPolicy();
  sealed_storage_.reset_policy(policy_);

  // Set up sealed_data with some initial policy digest.
  policy_digest_ = DftPolicyDigest();
  ExpectCommandSequence(true, false);
  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  EXPECT_TRUE(sealed_data.has_value());
  ResetMocks();

  // Try unsealing with a different policy, resulting in a different digest.
  policy_digest_ = WrongPolicyDigest();
  EXPECT_CALL(tpm_ownership_, GetTpmStatus(_, _)).Times(AtLeast(0));
  EXPECT_CALL(tpm_utility_, GetPolicyDigestForPcrValues(_, _, _)).Times(1);
  EXPECT_CALL(tpm_, CreatePrimarySyncShort(_, _, _, _, _, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(tpm_, ECDH_ZGenSync(_, _, _, _, _)).Times(0);
  auto result = sealed_storage_.Unseal(sealed_data.value());
  EXPECT_FALSE(result.has_value());
}

TEST_F(SealedStorageTest, NonEmptySealEmptyUnsealPolicy) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructPcrBoundPolicy();
  sealed_storage_.reset_policy(policy_);

  // Set up sealed_data with some initial non-empty policy digest.
  policy_digest_ = DftPolicyDigest();
  ExpectCommandSequence(true, false);
  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  EXPECT_TRUE(sealed_data.has_value());
  ResetMocks();

  // Try unsealing with an empty policy.
  policy_ = ConstructEmptyPolicy();
  sealed_storage_.reset_policy(policy_);
  EXPECT_CALL(tpm_ownership_, GetTpmStatus(_, _)).Times(AtLeast(0));
  EXPECT_CALL(tpm_utility_, GetPolicyDigestForPcrValues(_, _, _)).Times(0);
  EXPECT_CALL(tpm_, CreatePrimarySyncShort(_, _, _, _, _, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(tpm_, ECDH_ZGenSync(_, _, _, _, _)).Times(0);
  auto result = sealed_storage_.Unseal(sealed_data.value());
  EXPECT_FALSE(result.has_value());
}

TEST_F(SealedStorageTest, EmptySealNonEmptyUnsealPolicy) {
  const auto data_to_seal = SetupWrongZPointWithGarbageData();
  policy_ = ConstructEmptyPolicy();
  sealed_storage_.reset_policy(policy_);

  // Set up sealed_data with initial empty policy.
  ExpectCommandSequence(true, false);
  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  EXPECT_TRUE(sealed_data.has_value());
  ResetMocks();

  // Try unsealing with some non-empty policy.
  policy_ = ConstructPcrBoundPolicy();
  sealed_storage_.reset_policy(policy_);
  EXPECT_CALL(tpm_ownership_, GetTpmStatus(_, _)).Times(AtLeast(0));
  EXPECT_CALL(tpm_utility_, GetPolicyDigestForPcrValues(_, _, _)).Times(1);
  EXPECT_CALL(tpm_, CreatePrimarySyncShort(_, _, _, _, _, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(tpm_, ECDH_ZGenSync(_, _, _, _, _)).Times(0);
  auto result = sealed_storage_.Unseal(sealed_data.value());
  EXPECT_FALSE(result.has_value());
}

TEST_F(SealedStorageTest, CanUnsealV1) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructPcrBoundPolicy();
  sealed_storage_.reset_policy(policy_);
  ExpectCommandSequence();

  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());
  ConvertToV1(&sealed_data.value());

  // Now set the correct expected plaintext size and unseal the V1 blob.
  sealed_storage_.set_plain_size_for_v1(data_to_seal.size());
  auto result = sealed_storage_.Unseal(sealed_data.value());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), data_to_seal);
}

TEST_F(SealedStorageTest, WrongSizeForV1) {
  const auto data_to_seal = DftDataToSeal();
  policy_ = ConstructPcrBoundPolicy();
  sealed_storage_.reset_policy(policy_);
  ExpectCommandSequence();

  auto sealed_data = sealed_storage_.Seal(data_to_seal);
  ASSERT_TRUE(sealed_data.has_value());
  ConvertToV1(&sealed_data.value());

  // Now set a wrong expected plaintext size and try unsealing the V1 blob.
  sealed_storage_.set_plain_size_for_v1(data_to_seal.size() + 10);
  auto result = sealed_storage_.Unseal(sealed_data.value());
  ASSERT_FALSE(result.has_value());
}

}  // namespace sealed_storage
