// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/client/tpm_manager_utility.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <tuple>

#include "tpm_manager/common/mock_tpm_nvram_interface.h"
#include "tpm_manager/common/mock_tpm_ownership_interface.h"

namespace {

using ::testing::_;
using ::testing::ByRef;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::SaveArg;
using ::testing::Test;
using ::testing::WithArg;

// testing::InvokeArgument<N> does not work with base::Callback, need to use
// |ACTION_TAMPLATE| along with predefined |args| tuple.
ACTION_TEMPLATE(InvokeCallbackArgument,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(p0)) {
  std::get<k>(args).Run(p0);
}

}  // namespace

namespace tpm_manager {

class TpmManagerUtilityTest : public Test {
 protected:
  // Uses contructor and SetUp at the same time so we can take advantage of
  // both sytles of initialization.
  TpmManagerUtilityTest()
      : tpm_manager_utility_(&mock_tpm_owner_, &mock_tpm_nvram_) {
    // for the following ON_CALL setup, pass the fake replies by reference so we
    // can change the contained data dynamically in a test item.
    ON_CALL(mock_tpm_owner_, TakeOwnership(_, _))
        .WillByDefault(InvokeCallbackArgument<1>(ByRef(take_ownership_reply_)));
    ON_CALL(mock_tpm_owner_, GetTpmStatus(_, _))
        .WillByDefault(InvokeCallbackArgument<1>(ByRef(get_tpm_status_reply_)));
    ON_CALL(mock_tpm_owner_, GetVersionInfo(_, _))
        .WillByDefault(
            InvokeCallbackArgument<1>(ByRef(get_version_info_reply_)));
    ON_CALL(mock_tpm_owner_, RemoveOwnerDependency(_, _))
        .WillByDefault(
            InvokeCallbackArgument<1>(ByRef(remove_owner_dependency_reply_)));
    ON_CALL(mock_tpm_owner_, ClearStoredOwnerPassword(_, _))
        .WillByDefault(InvokeCallbackArgument<1>(
            ByRef(clear_stored_owner_password_reply_)));
    ON_CALL(mock_tpm_owner_, GetDictionaryAttackInfo(_, _))
        .WillByDefault(InvokeCallbackArgument<1>(
            ByRef(get_dictionary_attack_info_reply_)));
    ON_CALL(mock_tpm_owner_, ResetDictionaryAttackLock(_, _))
        .WillByDefault(InvokeCallbackArgument<1>(
            ByRef(reset_dictionary_attack_lock_reply_)));
  }
  void SetUp() override { ASSERT_TRUE(tpm_manager_utility_.Initialize()); }

  void RunReadSpaceTest(bool use_owner_auth,
                        tpm_manager::NvramResult result,
                        base::Optional<std::string> value,
                        bool expect_success);

  NiceMock<tpm_manager::MockTpmOwnershipInterface> mock_tpm_owner_;
  NiceMock<tpm_manager::MockTpmNvramInterface> mock_tpm_nvram_;
  TpmManagerUtility tpm_manager_utility_;

  // fake replies from TpmManager
  tpm_manager::TakeOwnershipReply take_ownership_reply_;
  tpm_manager::GetTpmStatusReply get_tpm_status_reply_;
  tpm_manager::GetVersionInfoReply get_version_info_reply_;
  tpm_manager::RemoveOwnerDependencyReply remove_owner_dependency_reply_;
  tpm_manager::ClearStoredOwnerPasswordReply clear_stored_owner_password_reply_;
  tpm_manager::GetDictionaryAttackInfoReply get_dictionary_attack_info_reply_;
  tpm_manager::ResetDictionaryAttackLockReply
      reset_dictionary_attack_lock_reply_;
};

TEST_F(TpmManagerUtilityTest, TakeOwnership) {
  take_ownership_reply_.set_status(tpm_manager::STATUS_SUCCESS);
  EXPECT_TRUE(tpm_manager_utility_.TakeOwnership());
}

TEST_F(TpmManagerUtilityTest, TakeOwnershipFail) {
  take_ownership_reply_.set_status(tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_FALSE(tpm_manager_utility_.TakeOwnership());
  take_ownership_reply_.set_status(tpm_manager::STATUS_NOT_AVAILABLE);
  EXPECT_FALSE(tpm_manager_utility_.TakeOwnership());
}

TEST_F(TpmManagerUtilityTest, GetTpmStatus) {
  bool is_enabled = true;
  bool is_owned = true;
  tpm_manager::LocalData local_data;
  get_tpm_status_reply_.set_status(tpm_manager::STATUS_SUCCESS);

  get_tpm_status_reply_.set_enabled(false);
  get_tpm_status_reply_.set_owned(false);
  EXPECT_TRUE(
      tpm_manager_utility_.GetTpmStatus(&is_enabled, &is_owned, &local_data));
  EXPECT_EQ(is_enabled, get_tpm_status_reply_.enabled());
  EXPECT_EQ(is_owned, get_tpm_status_reply_.owned());

  get_tpm_status_reply_.set_enabled(true);
  get_tpm_status_reply_.set_owned(true);
  tpm_manager::LocalData expected_local_data;
  expected_local_data.set_owner_password("owner_password");
  expected_local_data.set_endorsement_password("endorsement_password");
  expected_local_data.set_lockout_password("lockout_password");
  expected_local_data.mutable_owner_delegate()->set_blob("blob");
  expected_local_data.mutable_owner_delegate()->set_secret("secret");
  get_tpm_status_reply_.mutable_local_data()->CopyFrom(expected_local_data);
  EXPECT_TRUE(
      tpm_manager_utility_.GetTpmStatus(&is_enabled, &is_owned, &local_data));
  EXPECT_EQ(is_enabled, get_tpm_status_reply_.enabled());
  EXPECT_EQ(is_owned, get_tpm_status_reply_.owned());
  EXPECT_EQ(expected_local_data.SerializeAsString(),
            local_data.SerializeAsString());
}

TEST_F(TpmManagerUtilityTest, GetTpmStatusFail) {
  bool is_enabled{false};
  bool is_owned{false};
  tpm_manager::LocalData local_data;

  get_tpm_status_reply_.set_status(tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_FALSE(
      tpm_manager_utility_.GetTpmStatus(&is_enabled, &is_owned, &local_data));

  get_tpm_status_reply_.set_status(tpm_manager::STATUS_NOT_AVAILABLE);
  EXPECT_FALSE(
      tpm_manager_utility_.GetTpmStatus(&is_enabled, &is_owned, &local_data));
}

TEST_F(TpmManagerUtilityTest, GetVersionInfo) {
  uint32_t family = 0;
  uint64_t spec_level = 0;
  uint32_t manufacturer = 0;
  uint32_t tpm_model = 0;
  uint64_t firmware_version = 0;
  std::string vendor_specific;

  get_version_info_reply_.set_status(tpm_manager::STATUS_SUCCESS);
  get_version_info_reply_.set_family(1);
  get_version_info_reply_.set_spec_level(2);
  get_version_info_reply_.set_manufacturer(3);
  get_version_info_reply_.set_tpm_model(4);
  get_version_info_reply_.set_firmware_version(5);
  get_version_info_reply_.set_vendor_specific("aa");

  EXPECT_TRUE(tpm_manager_utility_.GetVersionInfo(
      &family, &spec_level, &manufacturer, &tpm_model, &firmware_version,
      &vendor_specific));
  EXPECT_EQ(family, 1);
  EXPECT_EQ(spec_level, 2);
  EXPECT_EQ(manufacturer, 3);
  EXPECT_EQ(tpm_model, 4);
  EXPECT_EQ(firmware_version, 5);
  EXPECT_EQ(vendor_specific, "aa");
}

TEST_F(TpmManagerUtilityTest, GetVersionInfoFail) {
  EXPECT_FALSE(tpm_manager_utility_.GetVersionInfo(nullptr, nullptr, nullptr,
                                                   nullptr, nullptr, nullptr));

  uint32_t family;
  uint64_t spec_level;
  uint32_t manufacturer;
  uint32_t tpm_model;
  uint64_t firmware_version;
  std::string vendor_specific;

  get_version_info_reply_.set_status(tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_FALSE(tpm_manager_utility_.GetVersionInfo(
      &family, &spec_level, &manufacturer, &tpm_model, &firmware_version,
      &vendor_specific));

  get_version_info_reply_.set_status(tpm_manager::STATUS_NOT_AVAILABLE);
  EXPECT_FALSE(tpm_manager_utility_.GetVersionInfo(
      &family, &spec_level, &manufacturer, &tpm_model, &firmware_version,
      &vendor_specific));
}

TEST_F(TpmManagerUtilityTest, RemoveOwnerDependency) {
  remove_owner_dependency_reply_.set_status(tpm_manager::STATUS_SUCCESS);
  EXPECT_TRUE(tpm_manager_utility_.RemoveOwnerDependency(""));
}

TEST_F(TpmManagerUtilityTest, RemoveOwnerDependencyFail) {
  remove_owner_dependency_reply_.set_status(tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_FALSE(tpm_manager_utility_.RemoveOwnerDependency(""));

  remove_owner_dependency_reply_.set_status(tpm_manager::STATUS_NOT_AVAILABLE);
  EXPECT_FALSE(tpm_manager_utility_.RemoveOwnerDependency(""));
}

TEST_F(TpmManagerUtilityTest, GetDictionaryAttackInfo) {
  int counter = 0;
  int threshold = 0;
  bool lockout = false;
  int seconds_remaining = 0;
  get_dictionary_attack_info_reply_.set_status(tpm_manager::STATUS_SUCCESS);
  get_dictionary_attack_info_reply_.set_dictionary_attack_counter(123);
  get_dictionary_attack_info_reply_.set_dictionary_attack_threshold(456);
  get_dictionary_attack_info_reply_.set_dictionary_attack_lockout_in_effect(
      true);
  get_dictionary_attack_info_reply_
      .set_dictionary_attack_lockout_seconds_remaining(789);
  EXPECT_TRUE(tpm_manager_utility_.GetDictionaryAttackInfo(
      &counter, &threshold, &lockout, &seconds_remaining));
  EXPECT_EQ(counter,
            get_dictionary_attack_info_reply_.dictionary_attack_counter());
  EXPECT_EQ(threshold,
            get_dictionary_attack_info_reply_.dictionary_attack_threshold());
  EXPECT_EQ(
      lockout,
      get_dictionary_attack_info_reply_.dictionary_attack_lockout_in_effect());
  EXPECT_EQ(seconds_remaining,
            get_dictionary_attack_info_reply_
                .dictionary_attack_lockout_seconds_remaining());
}

TEST_F(TpmManagerUtilityTest, GetDictionaryAttackInfoFail) {
  int counter = 0;
  int threshold = 0;
  bool lockout = false;
  int seconds_remaining = 0;

  get_dictionary_attack_info_reply_.set_status(
      tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_FALSE(tpm_manager_utility_.GetDictionaryAttackInfo(
      &counter, &threshold, &lockout, &seconds_remaining));

  get_dictionary_attack_info_reply_.set_status(
      tpm_manager::STATUS_NOT_AVAILABLE);
  EXPECT_FALSE(tpm_manager_utility_.GetDictionaryAttackInfo(
      &counter, &threshold, &lockout, &seconds_remaining));
}

TEST_F(TpmManagerUtilityTest, ResetDictionaryAttackLock) {
  reset_dictionary_attack_lock_reply_.set_status(tpm_manager::STATUS_SUCCESS);
  EXPECT_TRUE(tpm_manager_utility_.ResetDictionaryAttackLock());
}

TEST_F(TpmManagerUtilityTest, ResetDictionaryAttackLockFail) {
  reset_dictionary_attack_lock_reply_.set_status(
      tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_FALSE(tpm_manager_utility_.ResetDictionaryAttackLock());

  reset_dictionary_attack_lock_reply_.set_status(
      tpm_manager::STATUS_NOT_AVAILABLE);
  EXPECT_FALSE(tpm_manager_utility_.ResetDictionaryAttackLock());
}

TEST_F(TpmManagerUtilityTest, ClearStoredOwnerPassword) {
  clear_stored_owner_password_reply_.set_status(tpm_manager::STATUS_SUCCESS);
  EXPECT_TRUE(tpm_manager_utility_.ClearStoredOwnerPassword());
}

TEST_F(TpmManagerUtilityTest, ClearStoredOwnerPasswordFail) {
  clear_stored_owner_password_reply_.set_status(
      tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_FALSE(tpm_manager_utility_.ClearStoredOwnerPassword());

  clear_stored_owner_password_reply_.set_status(
      tpm_manager::STATUS_NOT_AVAILABLE);
  EXPECT_FALSE(tpm_manager_utility_.ClearStoredOwnerPassword());
}

TEST_F(TpmManagerUtilityTest, ExtraInitializationCall) {
  EXPECT_TRUE(tpm_manager_utility_.Initialize());
}

TEST_F(TpmManagerUtilityTest, OwnershipTakenSignal) {
  bool result_is_successful;
  bool result_has_received;
  LocalData result_local_data;

  // Tests the initial state.
  EXPECT_FALSE(tpm_manager_utility_.GetOwnershipTakenSignalStatus(
      &result_is_successful, &result_has_received, &result_local_data));

  // Tests the signal connection failure.
  tpm_manager_utility_.OnSignalConnected("", "", false);
  EXPECT_TRUE(tpm_manager_utility_.GetOwnershipTakenSignalStatus(
      &result_is_successful, &result_has_received, &result_local_data));
  EXPECT_FALSE(result_is_successful);

  // Tests the signal connection success.
  tpm_manager_utility_.OnSignalConnected("", "", true);
  EXPECT_TRUE(tpm_manager_utility_.GetOwnershipTakenSignalStatus(
      &result_is_successful, &result_has_received, &result_local_data));
  EXPECT_TRUE(result_is_successful);
  EXPECT_FALSE(result_has_received);

  OwnershipTakenSignal signal;
  signal.mutable_local_data()->set_owner_password("owner password");
  signal.mutable_local_data()->set_endorsement_password("endorsement password");
  tpm_manager_utility_.OnOwnershipTaken(signal);
  EXPECT_TRUE(tpm_manager_utility_.GetOwnershipTakenSignalStatus(
      &result_is_successful, &result_has_received, &result_local_data));
  EXPECT_TRUE(result_is_successful);
  EXPECT_TRUE(result_has_received);
  EXPECT_EQ(result_local_data.SerializeAsString(),
            signal.local_data().SerializeAsString());

  // Tests if the null parameters break the code.
  EXPECT_TRUE(tpm_manager_utility_.GetOwnershipTakenSignalStatus(
      nullptr, nullptr, nullptr));
}

void TpmManagerUtilityTest::RunReadSpaceTest(bool use_owner_auth,
                                             tpm_manager::NvramResult result,
                                             base::Optional<std::string> value,
                                             bool expect_success) {
  constexpr uint32_t kNvIndex = 0x0123456;
  tpm_manager::ReadSpaceRequest request;

  tpm_manager::ReadSpaceReply reply;
  reply.set_result(result);
  if (value) {
    reply.set_data(*value);
  }

  EXPECT_CALL(mock_tpm_nvram_, ReadSpace(_, _))
      .WillOnce(
          DoAll(SaveArg<0>(&request), InvokeCallbackArgument<1>(ByRef(reply))));

  std::string output;
  EXPECT_EQ(expect_success,
            tpm_manager_utility_.ReadSpace(kNvIndex, use_owner_auth, &output));

  EXPECT_EQ(kNvIndex, request.index());
  EXPECT_EQ(use_owner_auth, request.use_owner_authorization());

  if (value) {
    EXPECT_EQ(output, *value);
  }
}

TEST_F(TpmManagerUtilityTest, ReadSpace) {
  RunReadSpaceTest(false /* owner auth */, tpm_manager::NVRAM_RESULT_SUCCESS,
                   "notarealnvramspace", true /* success */);
}

TEST_F(TpmManagerUtilityTest, ReadSpaceOwnerAuth) {
  RunReadSpaceTest(true /* owner auth */, tpm_manager::NVRAM_RESULT_SUCCESS,
                   "notarealnvramspace", true /* success */);
}

TEST_F(TpmManagerUtilityTest, ReadSpaceNvRamSpaceDoesNotExist) {
  RunReadSpaceTest(false /* owner auth */,
                   tpm_manager::NVRAM_RESULT_SPACE_DOES_NOT_EXIST, {},
                   false /* success */);
}

TEST_F(TpmManagerUtilityTest, ReadSpaceError) {
  RunReadSpaceTest(false /* owner auth */,
                   tpm_manager::NVRAM_RESULT_ACCESS_DENIED, {},
                   false /* success */);
}

}  // namespace tpm_manager
