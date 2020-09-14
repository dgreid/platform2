// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_new_impl.h"

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec/test_utils/tpm1/test_fixture.h>
#include <tpm_manager/client/mock_tpm_manager_utility.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>

namespace {

using ::testing::_;
using ::testing::ByRef;
using ::testing::DoAll;
using ::testing::ElementsAreArray;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

using brillo::Blob;
using brillo::BlobToString;
using brillo::SecureBlob;
using tpm_manager::LocalData;
using tpm_manager::MockTpmManagerUtility;

}  // namespace

namespace cryptohome {

class TpmNewImplTest : public ::hwsec::Tpm1HwsecTest {
 public:
  TpmNewImplTest() = default;
  ~TpmNewImplTest() override = default;

 protected:
  NiceMock<MockTpmManagerUtility> mock_tpm_manager_utility_;
  TpmNewImpl tpm_{&mock_tpm_manager_utility_};
  Tpm* GetTpm() { return &tpm_; }
};

TEST_F(TpmNewImplTest, TakeOwnership) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_manager_utility_, TakeOwnership())
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->TakeOwnership(0, SecureBlob{}));
  EXPECT_CALL(mock_tpm_manager_utility_, TakeOwnership())
      .WillOnce(Return(true));
  EXPECT_TRUE(GetTpm()->TakeOwnership(0, SecureBlob{}));

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(true), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, TakeOwnership()).Times(0);
  EXPECT_TRUE(GetTpm()->TakeOwnership(0, SecureBlob{}));
}

TEST_F(TpmNewImplTest, Enabled) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(0);
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->IsEnabled());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(false), Return(true)));
  EXPECT_FALSE(GetTpm()->IsEnabled());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(true), Return(true)));
  EXPECT_TRUE(GetTpm()->IsEnabled());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(GetTpm()->IsEnabled());
}

TEST_F(TpmNewImplTest, OwnedWithoutSignal) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->IsOwned());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(false), Return(true)));
  EXPECT_FALSE(GetTpm()->IsOwned());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(true), Return(true)));
  EXPECT_TRUE(GetTpm()->IsOwned());

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(GetTpm()->IsOwned());
}

TEST_F(TpmNewImplTest, GetOwnerPasswordWithoutSignal) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillRepeatedly(Return(false));
  SecureBlob result_owner_password;
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->GetOwnerPassword(&result_owner_password));
  LocalData expected_local_data;
  expected_local_data.set_owner_password("owner password");
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(true), SetArgPointee<1>(true),
                      SetArgPointee<2>(expected_local_data), Return(true)));
  EXPECT_TRUE(GetTpm()->GetOwnerPassword(&result_owner_password));
  EXPECT_EQ(result_owner_password.to_string(),
            expected_local_data.owner_password());

  result_owner_password.clear();
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(GetTpm()->GetOwnerPassword(&result_owner_password));
  EXPECT_EQ(result_owner_password.to_string(),
            expected_local_data.owner_password());
}

TEST_F(TpmNewImplTest, GetOwnerPasswordEmpty) {
  SecureBlob result_owner_password;
  EXPECT_FALSE(GetTpm()->GetOwnerPassword(&result_owner_password));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(true), SetArgPointee<1>(true),
                      SetArgPointee<2>(LocalData{}), Return(true)));
  EXPECT_FALSE(GetTpm()->GetOwnerPassword(&result_owner_password));
}

TEST_F(TpmNewImplTest, GetDelegateWithoutSignal) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillRepeatedly(Return(false));
  Blob result_blob;
  Blob result_secret;
  bool result_has_reset_lock_permissions = false;
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));
  LocalData expected_local_data;

  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<0>(true), SetArgPointee<1>(true),
                            SetArgPointee<2>(ByRef(expected_local_data)),
                            Return(true)));
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));

  expected_local_data.mutable_owner_delegate()->set_blob("blob");
  expected_local_data.mutable_owner_delegate()->set_secret("secret");
  expected_local_data.mutable_owner_delegate()->set_has_reset_lock_permissions(
      true);
  EXPECT_TRUE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                    &result_has_reset_lock_permissions));
  EXPECT_THAT(result_blob,
              ElementsAreArray(expected_local_data.owner_delegate().blob()));
  EXPECT_THAT(result_secret,
              ElementsAreArray(expected_local_data.owner_delegate().secret()));
  EXPECT_TRUE(result_has_reset_lock_permissions);
}

TEST_F(TpmNewImplTest, GetDictionaryAttackInfo) {
  int result_counter = 0;
  int result_threshold = 0;
  bool result_lockout = false;
  int result_seconds_remaining = 0;
  EXPECT_CALL(mock_tpm_manager_utility_, GetDictionaryAttackInfo(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->GetDictionaryAttackInfo(
      &result_counter, &result_threshold, &result_lockout,
      &result_seconds_remaining));

  EXPECT_CALL(mock_tpm_manager_utility_, GetDictionaryAttackInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(123), SetArgPointee<1>(456),
                      SetArgPointee<2>(true), SetArgPointee<3>(789),
                      Return(true)));
  EXPECT_TRUE(GetTpm()->GetDictionaryAttackInfo(
      &result_counter, &result_threshold, &result_lockout,
      &result_seconds_remaining));
  EXPECT_EQ(result_counter, 123);
  EXPECT_EQ(result_threshold, 456);
  EXPECT_TRUE(result_lockout);
  EXPECT_EQ(result_seconds_remaining, 789);
}

TEST_F(TpmNewImplTest, ResetDictionaryAttackMitigation) {
  EXPECT_CALL(mock_tpm_manager_utility_, ResetDictionaryAttackLock())
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->ResetDictionaryAttackMitigation(Blob{}, Blob{}));
  EXPECT_CALL(mock_tpm_manager_utility_, ResetDictionaryAttackLock())
      .WillOnce(Return(true));
  EXPECT_TRUE(GetTpm()->ResetDictionaryAttackMitigation(Blob{}, Blob{}));
}

TEST_F(TpmNewImplTest, SignalCache) {
  brillo::SecureBlob result_owner_password;
  brillo::Blob result_blob, result_secret;
  bool result_has_reset_lock_permissions;
  ON_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _))
      .WillByDefault(Return(false));

  ON_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillByDefault(Return(false));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(2);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(2);
  EXPECT_FALSE(GetTpm()->GetOwnerPassword(&result_owner_password));
  EXPECT_FALSE(GetTpm()->IsOwned());

  // |GetDelegate| doesn't fully rely on the signal. Thus, expects to call
  // |GetTpmStatus| but not |GetOwnershipTakenSignalStatus| when the auth
  // delegate is not found.
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(0);
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));

  ON_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<0>(false), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(3);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(2);
  EXPECT_FALSE(GetTpm()->GetOwnerPassword(&result_owner_password));
  EXPECT_FALSE(GetTpm()->IsOwned());
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));

  ON_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillByDefault(
          DoAll(SetArgPointee<0>(true), SetArgPointee<1>(false), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .Times(2);
  EXPECT_FALSE(GetTpm()->IsOwned());
  EXPECT_FALSE(GetTpm()->GetOwnerPassword(&result_owner_password));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(1);
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));

  tpm_manager::LocalData expected_local_data;
  expected_local_data.set_owner_password("owner password");
  expected_local_data.mutable_owner_delegate()->set_blob("blob");
  expected_local_data.mutable_owner_delegate()->set_secret("secret");
  expected_local_data.mutable_owner_delegate()->set_has_reset_lock_permissions(
      true);
  EXPECT_CALL(mock_tpm_manager_utility_, GetOwnershipTakenSignalStatus(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(true), SetArgPointee<1>(true),
                      SetArgPointee<2>(expected_local_data), Return(true)));
  EXPECT_CALL(mock_tpm_manager_utility_, GetTpmStatus(_, _, _)).Times(0);
  EXPECT_TRUE(GetTpm()->IsOwned());
  EXPECT_TRUE(GetTpm()->IsEnabled());
  EXPECT_TRUE(GetTpm()->GetOwnerPassword(&result_owner_password));
  EXPECT_TRUE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                    &result_has_reset_lock_permissions));
  EXPECT_THAT(result_owner_password,
              ElementsAreArray(expected_local_data.owner_password()));
  EXPECT_THAT(result_blob,
              ElementsAreArray(expected_local_data.owner_delegate().blob()));
  EXPECT_THAT(result_secret,
              ElementsAreArray(expected_local_data.owner_delegate().secret()));
  EXPECT_EQ(result_has_reset_lock_permissions,
            expected_local_data.owner_delegate().has_reset_lock_permissions());
}

TEST_F(TpmNewImplTest, RemoveTpmOwnerDependency) {
  EXPECT_CALL(mock_tpm_manager_utility_,
              RemoveOwnerDependency(tpm_manager::kTpmOwnerDependency_Nvram))
      .WillOnce(Return(true));
  EXPECT_TRUE(GetTpm()->RemoveOwnerDependency(
      TpmPersistentState::TpmOwnerDependency::kInstallAttributes));
  EXPECT_CALL(
      mock_tpm_manager_utility_,
      RemoveOwnerDependency(tpm_manager::kTpmOwnerDependency_Attestation))
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->RemoveOwnerDependency(
      TpmPersistentState::TpmOwnerDependency::kAttestation));
}

TEST_F(TpmNewImplTest, RemoveTpmOwnerDependencyInvalidEnum) {
  EXPECT_DEBUG_DEATH(
      GetTpm()->RemoveOwnerDependency(
          static_cast<TpmPersistentState::TpmOwnerDependency>(999)),
      ".*Unexpected enum class value: 999");
}

TEST_F(TpmNewImplTest, ClearStoredPassword) {
  EXPECT_CALL(mock_tpm_manager_utility_, ClearStoredOwnerPassword())
      .WillOnce(Return(true));
  EXPECT_TRUE(GetTpm()->ClearStoredPassword());
  EXPECT_CALL(mock_tpm_manager_utility_, ClearStoredOwnerPassword())
      .WillOnce(Return(false));
  EXPECT_FALSE(GetTpm()->ClearStoredPassword());
}

TEST_F(TpmNewImplTest, GetVersionInfoCache) {
  Tpm::TpmVersionInfo expected_version_info;
  expected_version_info.family = 1;
  expected_version_info.spec_level = 2;
  expected_version_info.manufacturer = 3;
  expected_version_info.tpm_model = 4;
  expected_version_info.firmware_version = 5;
  expected_version_info.vendor_specific = "aa";

  EXPECT_CALL(mock_tpm_manager_utility_, GetVersionInfo(_, _, _, _, _, _))
      .WillOnce(Return(false))
      .WillOnce(DoAll(SetArgPointee<0>(expected_version_info.family),
                      SetArgPointee<1>(expected_version_info.spec_level),
                      SetArgPointee<2>(expected_version_info.manufacturer),
                      SetArgPointee<3>(expected_version_info.tpm_model),
                      SetArgPointee<4>(expected_version_info.firmware_version),
                      SetArgPointee<5>(expected_version_info.vendor_specific),
                      Return(true)));

  Tpm::TpmVersionInfo actual_version_info;
  // Requests from tpm_manager, failed, not cached
  EXPECT_FALSE(GetTpm()->GetVersionInfo(&actual_version_info));

  // Requests from tpm_manager, succeeded, cached
  EXPECT_TRUE(GetTpm()->GetVersionInfo(&actual_version_info));
  EXPECT_EQ(expected_version_info.GetFingerprint(),
            actual_version_info.GetFingerprint());

  // Returns from cache
  EXPECT_TRUE(GetTpm()->GetVersionInfo(&actual_version_info));
  EXPECT_EQ(expected_version_info.GetFingerprint(),
            actual_version_info.GetFingerprint());
}

TEST_F(TpmNewImplTest, GetVersionInfoBadInput) {
  EXPECT_CALL(mock_tpm_manager_utility_, GetVersionInfo(_, _, _, _, _, _))
      .Times(0);
  EXPECT_FALSE(GetTpm()->GetVersionInfo(nullptr));
}

TEST_F(TpmNewImplTest, BadTpmManagerUtility) {
  EXPECT_CALL(mock_tpm_manager_utility_, Initialize())
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(GetTpm()->TakeOwnership(0, SecureBlob{}));
  SecureBlob result_owner_password;
  EXPECT_FALSE(GetTpm()->GetOwnerPassword(&result_owner_password));
  EXPECT_FALSE(GetTpm()->IsEnabled());
  EXPECT_FALSE(GetTpm()->IsOwned());
  EXPECT_FALSE(GetTpm()->ResetDictionaryAttackMitigation(Blob{}, Blob{}));
  int result_counter;
  int result_threshold;
  bool result_lockout;
  int result_seconds_remaining;
  EXPECT_FALSE(GetTpm()->GetDictionaryAttackInfo(
      &result_counter, &result_threshold, &result_lockout,
      &result_seconds_remaining));
  Blob result_blob;
  Blob result_secret;
  bool result_has_reset_lock_permissions;
  EXPECT_FALSE(GetTpm()->GetDelegate(&result_blob, &result_secret,
                                     &result_has_reset_lock_permissions));
}

}  // namespace cryptohome
