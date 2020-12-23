// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/common/tpm_utility_common.h"

#if USE_TPM2
#include "attestation/common/tpm_utility_v2.h"
#include "trunks/trunks_factory_for_test.h"
#else
#include "attestation/common/tpm_utility_v1.h"
#endif

#include <utility>
#include <vector>

#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <tpm_manager/client/mock_tpm_manager_utility.h>

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::Types;
}  // namespace

namespace attestation {

template <typename TpmUtilityDataType>
std::unique_ptr<TpmUtilityCommon> GetTpmUtility(
    tpm_manager::TpmManagerUtility* tpm_manager_utility,
    TpmUtilityDataType* utility_data);

template <typename TpmUtilityDataType>
class TpmUtilityCommonTest : public ::testing::Test {
 public:
  ~TpmUtilityCommonTest() override = default;
  void SetUp() override {
    tpm_utility_ = GetTpmUtility(&mock_tpm_manager_utility_, &utility_data_);
  }

 protected:
  // Checks if GetTpmStatus sets up the private data member.
  void VerifyAgainstExpectedLocalData(const tpm_manager::LocalData local_data) {
    EXPECT_EQ(tpm_utility_->owner_password_, local_data.owner_password());
    EXPECT_EQ(tpm_utility_->endorsement_password_,
              local_data.endorsement_password());
    EXPECT_EQ(tpm_utility_->delegate_blob_, local_data.owner_delegate().blob());
    EXPECT_EQ(tpm_utility_->delegate_secret_,
              local_data.owner_delegate().secret());
  }

  NiceMock<tpm_manager::MockTpmManagerUtility> mock_tpm_manager_utility_;
  TpmUtilityDataType utility_data_;
  std::unique_ptr<TpmUtilityCommon> tpm_utility_;
};

#if USE_TPM2

struct TpmUtilityDataV2 {
  trunks::TrunksFactoryForTest trunks_factory_for_test_;
};

template <>
std::unique_ptr<TpmUtilityCommon> GetTpmUtility<TpmUtilityDataV2>(
    tpm_manager::TpmManagerUtility* tpm_manager_utility,
    TpmUtilityDataV2* utility_data) {
  return std::make_unique<TpmUtilityV2>(
      tpm_manager_utility, &utility_data->trunks_factory_for_test_);
}

TYPED_TEST_SUITE(TpmUtilityCommonTest, Types<TpmUtilityDataV2>);

#else

struct TpmUtilityDataV1 {
  // Nothing
};

template <>
std::unique_ptr<TpmUtilityCommon> GetTpmUtility<TpmUtilityDataV1>(
    tpm_manager::TpmManagerUtility* tpm_manager_utility,
    TpmUtilityDataV1* /* utility_data */) {
  return std::make_unique<TpmUtilityV1>(tpm_manager_utility);
}

TYPED_TEST_SUITE(TpmUtilityCommonTest, Types<TpmUtilityDataV1>);

#endif

TYPED_TEST(TpmUtilityCommonTest, IsTpmReady) {
  EXPECT_CALL(this->mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false))
      .WillOnce(
          DoAll(SetArgPointee<0>(false), SetArgPointee<1>(false), Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(true), SetArgPointee<1>(false), Return(true)));
  EXPECT_FALSE(this->tpm_utility_->IsTpmReady());
  EXPECT_FALSE(this->tpm_utility_->IsTpmReady());
  EXPECT_FALSE(this->tpm_utility_->IsTpmReady());

  EXPECT_CALL(this->mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(
          DoAll(SetArgPointee<0>(true), SetArgPointee<1>(true), Return(true)));
  EXPECT_TRUE(this->tpm_utility_->IsTpmReady());
}

TYPED_TEST(TpmUtilityCommonTest, IsTpmReadyCallsCacheTpmState) {
  tpm_manager::LocalData expected_local_data;
  expected_local_data.set_owner_password("Uvuvwevwevwe");
  expected_local_data.set_endorsement_password("Onyetenyevwe");
  expected_local_data.mutable_owner_delegate()->set_blob("Ugwemuhwem");
  expected_local_data.mutable_owner_delegate()->set_secret("Osas");
  EXPECT_CALL(this->mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(expected_local_data), Return(true)));
  this->tpm_utility_->IsTpmReady();
  this->VerifyAgainstExpectedLocalData(expected_local_data);
}

TYPED_TEST(TpmUtilityCommonTest, RemoveOwnerDependency) {
  EXPECT_CALL(
      this->mock_tpm_manager_utility_,
      RemoveOwnerDependency(tpm_manager::kTpmOwnerDependency_Attestation))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_FALSE(this->tpm_utility_->RemoveOwnerDependency());
  EXPECT_TRUE(this->tpm_utility_->RemoveOwnerDependency());
}

}  // namespace attestation
