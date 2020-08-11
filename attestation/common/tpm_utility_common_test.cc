// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/common/tpm_utility_common.h"

#if !USE_TPM2
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

template <typename TpmUtilityType>
class TpmUtilityCommonTest : public ::testing::Test {
 public:
  ~TpmUtilityCommonTest() override = default;
  void SetUp() override {
    tpm_manager_utility_backup_ = &mock_tpm_manager_utility_;
    std::swap(tpm_utility_.tpm_manager_utility_, tpm_manager_utility_backup_);
  }
  void TearDown() override {
    std::swap(tpm_utility_.tpm_manager_utility_, tpm_manager_utility_backup_);
  }

 protected:
  // Checks if GetTpmStatus sets up the private data member.
  void VerifyAgainstExpectedLocalData(const tpm_manager::LocalData local_data) {
    EXPECT_EQ(tpm_utility_.owner_password_, local_data.owner_password());
    EXPECT_EQ(tpm_utility_.endorsement_password_,
              local_data.endorsement_password());
    EXPECT_EQ(tpm_utility_.delegate_blob_, local_data.owner_delegate().blob());
    EXPECT_EQ(tpm_utility_.delegate_secret_,
              local_data.owner_delegate().secret());
  }
  TpmUtilityType tpm_utility_;
  NiceMock<tpm_manager::MockTpmManagerUtility> mock_tpm_manager_utility_;
  tpm_manager::TpmManagerUtility* tpm_manager_utility_backup_;
};

#if USE_TPM2
using TpmUtilitiesUnderTest = Types<>;
#else
using TpmUtilitiesUnderTest = Types<TpmUtilityV1>;
#endif

TYPED_TEST_SUITE(TpmUtilityCommonTest, TpmUtilitiesUnderTest);

TYPED_TEST(TpmUtilityCommonTest, HasTpmManagerUtility) {
  EXPECT_THAT(this->tpm_manager_utility_backup_, NotNull());
}

TYPED_TEST(TpmUtilityCommonTest, IsTpmReady) {
  EXPECT_CALL(this->mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false))
      .WillOnce(
          DoAll(SetArgPointee<0>(false), SetArgPointee<1>(false), Return(true)))
      .WillOnce(
          DoAll(SetArgPointee<0>(true), SetArgPointee<1>(false), Return(true)));
  EXPECT_FALSE(this->tpm_utility_.IsTpmReady());
  EXPECT_FALSE(this->tpm_utility_.IsTpmReady());
  EXPECT_FALSE(this->tpm_utility_.IsTpmReady());

  EXPECT_CALL(this->mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(
          DoAll(SetArgPointee<0>(true), SetArgPointee<1>(true), Return(true)));
  EXPECT_TRUE(this->tpm_utility_.IsTpmReady());
}

TYPED_TEST(TpmUtilityCommonTest, IsTpmReadyCallsCacheTpmState) {
  tpm_manager::LocalData expected_local_data;
  expected_local_data.set_owner_password("Uvuvwevwevwe");
  expected_local_data.set_endorsement_password("Onyetenyevwe");
  expected_local_data.mutable_owner_delegate()->set_blob("Ugwemuhwem");
  expected_local_data.mutable_owner_delegate()->set_secret("Osas");
  EXPECT_CALL(this->mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(expected_local_data), Return(true)));
  this->tpm_utility_.IsTpmReady();
  this->VerifyAgainstExpectedLocalData(expected_local_data);
}

TYPED_TEST(TpmUtilityCommonTest, RemoveOwnerDependency) {
  EXPECT_CALL(
      this->mock_tpm_manager_utility_,
      RemoveOwnerDependency(tpm_manager::kTpmOwnerDependency_Attestation))
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_FALSE(this->tpm_utility_.RemoveOwnerDependency());
  EXPECT_TRUE(this->tpm_utility_.RemoveOwnerDependency());
}

}  // namespace attestation
