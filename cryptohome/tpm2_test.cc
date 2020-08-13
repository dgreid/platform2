// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Tpm2Impl.

#include "cryptohome/tpm2_impl.h"

#include <stdint.h>

#include <iterator>
#include <map>
#include <memory>
#include <set>

#include <base/bind.h>
#include <base/callback.h>
#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <base/run_loop.h>
#include <base/single_thread_task_runner.h>
#include <base/task/single_thread_task_executor.h>
#include <base/threading/thread_task_runner_handle.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <tpm_manager/common/mock_tpm_nvram_interface.h>
#include <tpm_manager/common/mock_tpm_ownership_interface.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>
#include <trunks/mock_authorization_delegate.h>
#include <trunks/mock_blob_parser.h>
#include <trunks/mock_hmac_session.h>
#include <trunks/mock_policy_session.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_state.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/tpm_constants.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory.h>
#include <trunks/trunks_factory_for_test.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/protobuf_test_utils.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::SecureBlob;
using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using testing::Values;
using testing::WithArg;
using tpm_manager::NVRAM_RESULT_IPC_ERROR;
using trunks::TPM_ALG_ID;
using trunks::TPM_RC;
using trunks::TPM_RC_FAILURE;
using trunks::TPM_RC_SUCCESS;
using trunks::TrunksFactory;

namespace {

const char kDefaultPassword[] = "password";

// Reset the |pcr_select| and set the bit corresponding to |index|.
void SetPcrSelectData(uint8_t* pcr_select, uint32_t index) {
  for (uint8_t i = 0; i < PCR_SELECT_MIN; ++i) {
    pcr_select[i] = 0;
  }
  pcr_select[index / 8] = 1 << (index % 8);
}

}  // namespace

namespace cryptohome {

class Tpm2Test : public testing::Test {
 public:
  Tpm2Test() {
    factory_.set_blob_parser(&mock_blob_parser_);
    factory_.set_tpm(&mock_tpm_);
    factory_.set_tpm_state(&mock_tpm_state_);
    factory_.set_tpm_utility(&mock_tpm_utility_);
    factory_.set_hmac_session(&mock_hmac_session_);
    factory_.set_policy_session(&mock_policy_session_);
    factory_.set_trial_session(&mock_trial_session_);
    tpm_ = std::make_unique<Tpm2Impl>(&factory_, &mock_tpm_owner_,
                                      &mock_tpm_nvram_);
    // Setup default status data.
    tpm_status_.set_status(tpm_manager::STATUS_SUCCESS);
    tpm_status_.set_enabled(true);
    tpm_status_.set_owned(true);
    tpm_status_.mutable_local_data()->set_owner_password(kDefaultPassword);
    ON_CALL(mock_tpm_owner_, GetTpmStatus(_, _))
        .WillByDefault(WithArg<1>(Invoke(this, &Tpm2Test::FakeGetTpmStatus)));
    ON_CALL(mock_tpm_owner_, GetVersionInfo(_, _))
        .WillByDefault(WithArg<1>(Invoke(this, &Tpm2Test::FakeGetVersionInfo)));
    ON_CALL(mock_tpm_owner_, GetDictionaryAttackInfo(_, _))
        .WillByDefault(
            WithArg<1>(Invoke(this, &Tpm2Test::FakeGetDictionaryAttackInfo)));
    ON_CALL(mock_tpm_owner_, ResetDictionaryAttackLock(_, _))
        .WillByDefault(
            WithArg<1>(Invoke(this, &Tpm2Test::FakeResetDictionaryAttackLock)));
    ON_CALL(mock_tpm_owner_, RemoveOwnerDependency(_, _))
        .WillByDefault(Invoke(this, &Tpm2Test::FakeRemoveOwnerDependency));
    ON_CALL(mock_tpm_owner_, ClearStoredOwnerPassword(_, _))
        .WillByDefault(Invoke(this, &Tpm2Test::FakeClearStoredOwnerPassword));
    SetupFakeNvram();
  }

 protected:
  tpm_manager::GetTpmStatusReply tpm_status_;
  tpm_manager::GetVersionInfoReply version_info_;
  tpm_manager::GetDictionaryAttackInfoReply da_info_;
  tpm_manager::DefineSpaceRequest last_define_space_request;
  tpm_manager::DestroySpaceRequest last_destroy_space_request;
  tpm_manager::WriteSpaceRequest last_write_space_request;
  tpm_manager::ReadSpaceRequest last_read_space_request;
  tpm_manager::LockSpaceRequest last_lock_space_request;
  tpm_manager::ListSpacesRequest last_list_spaces_request;
  tpm_manager::GetSpaceInfoRequest last_get_space_info_request;
  tpm_manager::RemoveOwnerDependencyRequest
      last_remove_owner_dependency_request;
  tpm_manager::DefineSpaceReply next_define_space_reply;
  tpm_manager::DestroySpaceReply next_destroy_space_reply;
  tpm_manager::WriteSpaceReply next_write_space_reply;
  tpm_manager::ReadSpaceReply next_read_space_reply;
  tpm_manager::LockSpaceReply next_lock_space_reply;
  tpm_manager::ListSpacesReply next_list_spaces_reply;
  tpm_manager::GetSpaceInfoReply next_get_space_info_reply;
  tpm_manager::RemoveOwnerDependencyReply next_remove_owner_dependency_reply;
  tpm_manager::ClearStoredOwnerPasswordReply next_clear_stored_password_reply;
  tpm_manager::ResetDictionaryAttackLockReply reset_da_lock_reply;

  std::unique_ptr<Tpm2Impl> tpm_;
  NiceMock<trunks::MockAuthorizationDelegate> mock_authorization_delegate_;
  NiceMock<trunks::MockBlobParser> mock_blob_parser_;
  NiceMock<trunks::MockTpm> mock_tpm_;
  NiceMock<trunks::MockTpmState> mock_tpm_state_;
  NiceMock<trunks::MockTpmUtility> mock_tpm_utility_;
  NiceMock<trunks::MockHmacSession> mock_hmac_session_;
  NiceMock<trunks::MockPolicySession> mock_policy_session_;
  NiceMock<trunks::MockPolicySession> mock_trial_session_;
  NiceMock<tpm_manager::MockTpmOwnershipInterface> mock_tpm_owner_;
  NiceMock<tpm_manager::MockTpmNvramInterface> mock_tpm_nvram_;

 private:
  void SetupFakeNvram() {
    ON_CALL(mock_tpm_nvram_, DefineSpace(_, _))
        .WillByDefault(Invoke(this, &Tpm2Test::FakeDefineSpace));
    ON_CALL(mock_tpm_nvram_, DestroySpace(_, _))
        .WillByDefault(Invoke(this, &Tpm2Test::FakeDestroySpace));
    ON_CALL(mock_tpm_nvram_, WriteSpace(_, _))
        .WillByDefault(Invoke(this, &Tpm2Test::FakeWriteSpace));
    ON_CALL(mock_tpm_nvram_, ReadSpace(_, _))
        .WillByDefault(Invoke(this, &Tpm2Test::FakeReadSpace));
    ON_CALL(mock_tpm_nvram_, LockSpace(_, _))
        .WillByDefault(Invoke(this, &Tpm2Test::FakeLockSpace));
    ON_CALL(mock_tpm_nvram_, ListSpaces(_, _))
        .WillByDefault(Invoke(this, &Tpm2Test::FakeListSpaces));
    ON_CALL(mock_tpm_nvram_, GetSpaceInfo(_, _))
        .WillByDefault(Invoke(this, &Tpm2Test::FakeGetSpaceInfo));
  }

  void FakeGetTpmStatus(
      const tpm_manager::TpmOwnershipInterface::GetTpmStatusCallback&
          callback) {
    callback.Run(tpm_status_);
  }

  void FakeGetVersionInfo(
      const tpm_manager::TpmOwnershipInterface::GetVersionInfoCallback&
      callback) {
    callback.Run(version_info_);
  }

  void FakeGetDictionaryAttackInfo(
      const tpm_manager::TpmOwnershipInterface::GetDictionaryAttackInfoCallback&
          callback) {
    callback.Run(da_info_);
  }

  void FakeResetDictionaryAttackLock(
      const tpm_manager::TpmOwnershipInterface::
          ResetDictionaryAttackLockCallback& callback) {
    callback.Run(reset_da_lock_reply);
  }

  void FakeRemoveOwnerDependency(
      const tpm_manager::RemoveOwnerDependencyRequest& request,
      const tpm_manager::TpmOwnershipInterface::RemoveOwnerDependencyCallback&
          callback) {
    last_remove_owner_dependency_request = request;
    callback.Run(next_remove_owner_dependency_reply);
  }

  void FakeClearStoredOwnerPassword(
      const tpm_manager::ClearStoredOwnerPasswordRequest& /* request */,
      const tpm_manager::TpmOwnershipInterface::
          ClearStoredOwnerPasswordCallback& callback) {
    callback.Run(next_clear_stored_password_reply);
  }

  void FakeDefineSpace(
      const tpm_manager::DefineSpaceRequest& request,
      const tpm_manager::TpmNvramInterface::DefineSpaceCallback& callback) {
    last_define_space_request = request;
    callback.Run(next_define_space_reply);
  }

  void FakeDestroySpace(
      const tpm_manager::DestroySpaceRequest& request,
      const tpm_manager::TpmNvramInterface::DestroySpaceCallback& callback) {
    last_destroy_space_request = request;
    callback.Run(next_destroy_space_reply);
  }

  void FakeWriteSpace(
      const tpm_manager::WriteSpaceRequest& request,
      const tpm_manager::TpmNvramInterface::WriteSpaceCallback& callback) {
    last_write_space_request = request;
    callback.Run(next_write_space_reply);
  }

  void FakeReadSpace(
      const tpm_manager::ReadSpaceRequest& request,
      const tpm_manager::TpmNvramInterface::ReadSpaceCallback& callback) {
    last_read_space_request = request;
    callback.Run(next_read_space_reply);
  }

  void FakeLockSpace(
      const tpm_manager::LockSpaceRequest& request,
      const tpm_manager::TpmNvramInterface::LockSpaceCallback& callback) {
    last_lock_space_request = request;
    callback.Run(next_lock_space_reply);
  }

  void FakeListSpaces(
      const tpm_manager::ListSpacesRequest& request,
      const tpm_manager::TpmNvramInterface::ListSpacesCallback& callback) {
    last_list_spaces_request = request;
    callback.Run(next_list_spaces_reply);
  }

  void FakeGetSpaceInfo(
      const tpm_manager::GetSpaceInfoRequest& request,
      const tpm_manager::TpmNvramInterface::GetSpaceInfoCallback& callback) {
    last_get_space_info_request = request;
    callback.Run(next_get_space_info_reply);
  }

  trunks::TrunksFactoryForTest factory_;
};

TEST_F(Tpm2Test, GetPcrMapNotExtended) {
  std::string obfuscated_username = "OBFUSCATED_USER";
  std::map<uint32_t, std::string> result =
      tpm_->GetPcrMap(obfuscated_username, /*use_extended_pcr=*/false);

  EXPECT_EQ(1, result.size());
  const std::string& result_str = result[kTpmSingleUserPCR];

  std::string expected_result(SHA256_DIGEST_LENGTH, 0);
  EXPECT_EQ(expected_result, result_str);
}

TEST_F(Tpm2Test, GetPcrMapExtended) {
  std::string obfuscated_username = "OBFUSCATED_USER";
  std::map<uint32_t, std::string> result =
      tpm_->GetPcrMap(obfuscated_username, /*use_extended_pcr=*/true);

  EXPECT_EQ(1, result.size());
  const std::string& result_str = result[kTpmSingleUserPCR];

  // Pre-calculated expected result.
  unsigned char expected_result_bytes[] = {
      0x2D, 0x5B, 0x86, 0xF2, 0xBE, 0xEE, 0xD1, 0xB7, 0x40, 0xC7, 0xCD,
      0xE3, 0x88, 0x25, 0xA6, 0xEE, 0xE3, 0x98, 0x69, 0xA4, 0x99, 0x4D,
      0x88, 0x09, 0x85, 0x6E, 0x0E, 0x11, 0x7A, 0x4E, 0xFD, 0x91};
  std::string expected_result(reinterpret_cast<char*>(expected_result_bytes),
                              sizeof(expected_result_bytes) / sizeof(uint8_t));
  EXPECT_EQ(expected_result, result_str);
}

TEST_F(Tpm2Test, GetOwnerPassword) {
  brillo::SecureBlob owner_password;
  EXPECT_TRUE(tpm_->GetOwnerPassword(&owner_password));
  EXPECT_EQ(kDefaultPassword, owner_password.to_string());
}

TEST_F(Tpm2Test, EnabledOwnedCheckSuccess) {
  bool enabled = false;
  bool owned = false;
  EXPECT_TRUE(tpm_->PerformEnabledOwnedCheck(&enabled, &owned));
  EXPECT_TRUE(enabled);
  EXPECT_TRUE(owned);
}

TEST_F(Tpm2Test, EnabledOwnedCheckStateError) {
  tpm_status_.set_status(tpm_manager::STATUS_NOT_AVAILABLE);
  bool enabled = false;
  bool owned = false;
  EXPECT_FALSE(tpm_->PerformEnabledOwnedCheck(&enabled, &owned));
  EXPECT_FALSE(enabled);
  EXPECT_FALSE(owned);
}

TEST_F(Tpm2Test, GetVersionInfo) {
  tpm_manager::GetVersionInfoRequest expected_request;
  EXPECT_CALL(mock_tpm_owner_,
              GetVersionInfo(ProtobufEquals(expected_request), _))
      .Times(1);

  version_info_.set_status(tpm_manager::STATUS_SUCCESS);
  version_info_.set_family(11);
  version_info_.set_spec_level(22);
  version_info_.set_manufacturer(33);
  version_info_.set_tpm_model(44);
  version_info_.set_firmware_version(55);
  version_info_.set_vendor_specific("abc");

  Tpm::TpmVersionInfo actual_info;

  // First call fetches version info from tpm manager.
  EXPECT_TRUE(tpm_->GetVersionInfo(&actual_info));
  EXPECT_EQ(11, actual_info.family);
  EXPECT_EQ(22, actual_info.spec_level);
  EXPECT_EQ(33, actual_info.manufacturer);
  EXPECT_EQ(44, actual_info.tpm_model);
  EXPECT_EQ(55, actual_info.firmware_version);
  EXPECT_EQ("abc", actual_info.vendor_specific);

  // Second call returns from cache directly.
  EXPECT_TRUE(tpm_->GetVersionInfo(&actual_info));
  EXPECT_EQ(11, actual_info.family);
  EXPECT_EQ(22, actual_info.spec_level);
  EXPECT_EQ(33, actual_info.manufacturer);
  EXPECT_EQ(44, actual_info.tpm_model);
  EXPECT_EQ(55, actual_info.firmware_version);
  EXPECT_EQ("abc", actual_info.vendor_specific);
}

TEST_F(Tpm2Test, GetVersionInfoError) {
  EXPECT_FALSE(tpm_->GetVersionInfo(nullptr));

  version_info_.set_status(tpm_manager::STATUS_DEVICE_ERROR);

  Tpm::TpmVersionInfo info;
  EXPECT_FALSE(tpm_->GetVersionInfo(&info));
}

TEST_F(Tpm2Test, GetDictionaryAttackInfo) {
  da_info_.set_status(tpm_manager::STATUS_SUCCESS);
  da_info_.set_dictionary_attack_counter(3);
  da_info_.set_dictionary_attack_threshold(4);
  da_info_.set_dictionary_attack_lockout_in_effect(true);
  da_info_.set_dictionary_attack_lockout_seconds_remaining(5);

  int counter;
  int threshold;
  bool lockout;
  int seconds_remaining;

  EXPECT_TRUE(tpm_->GetDictionaryAttackInfo(&counter,
                                            &threshold,
                                            &lockout,
                                            &seconds_remaining));
  EXPECT_EQ(3, counter);
  EXPECT_EQ(4, threshold);
  EXPECT_TRUE(lockout);
  EXPECT_EQ(5, seconds_remaining);
}

TEST_F(Tpm2Test, GetDictionaryAttackInfoError) {
  da_info_.set_status(tpm_manager::STATUS_DEVICE_ERROR);

  int counter;
  int threshold;
  bool lockout;
  int seconds_remaining;
  EXPECT_FALSE(tpm_->GetDictionaryAttackInfo(&counter,
                                             &threshold,
                                             &lockout,
                                             &seconds_remaining));
}

TEST_F(Tpm2Test, ResetDictionaryAttackMitigation) {
  base::SingleThreadTaskExecutor task_executor;
  base::RunLoop run_loop;
  const auto run_loop_quit_closure = base::Bind(
      [](base::Closure main_thread_quit_closure,
         scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner) {
        main_thread_task_runner->PostTask(FROM_HERE, main_thread_quit_closure);
      },
      run_loop.QuitClosure(), base::ThreadTaskRunnerHandle::Get());

  const tpm_manager::ResetDictionaryAttackLockRequest expected_request;
  EXPECT_CALL(mock_tpm_owner_,
              ResetDictionaryAttackLock(ProtobufEquals(expected_request), _))
      .WillOnce(InvokeWithoutArgs([&]() { run_loop_quit_closure.Run(); }));

  const brillo::Blob unused;
  EXPECT_TRUE(tpm_->ResetDictionaryAttackMitigation(unused, unused));

  // Wait till the mock ResetDictionaryAttackLock gets called.
  run_loop.Run();
}

TEST_F(Tpm2Test, GetRandomDataSuccess) {
  std::string random_data("random_data");
  size_t num_bytes = random_data.size();
  brillo::Blob data;
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(num_bytes, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(random_data), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(tpm_->GetRandomDataBlob(num_bytes, &data));
  EXPECT_EQ(data.size(), num_bytes);
  std::string tpm_data(data.begin(), data.end());
  EXPECT_EQ(tpm_data, random_data);
}

TEST_F(Tpm2Test, GetRandomDataFailure) {
  brillo::Blob data;
  size_t num_bytes = 5;
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(num_bytes, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->GetRandomDataBlob(num_bytes, &data));
}

TEST_F(Tpm2Test, GetRandomDataBadLength) {
  std::string random_data("random_data");
  brillo::Blob data;
  size_t num_bytes = random_data.size() + 1;
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(num_bytes, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(random_data), Return(TPM_RC_SUCCESS)));
  EXPECT_FALSE(tpm_->GetRandomDataBlob(num_bytes, &data));
}

TEST_F(Tpm2Test, DefineNvramSuccess) {
  uint32_t index = 2;
  size_t length = 5;
  EXPECT_TRUE(tpm_->DefineNvram(
      index, length, Tpm::kTpmNvramWriteDefine));
  EXPECT_EQ(index, last_define_space_request.index());
  EXPECT_EQ(length, last_define_space_request.size());
  ASSERT_EQ(1, last_define_space_request.attributes_size());
  EXPECT_EQ(tpm_manager::NVRAM_PERSISTENT_WRITE_LOCK,
            last_define_space_request.attributes(0));
  EXPECT_EQ(tpm_manager::NVRAM_POLICY_NONE, last_define_space_request.policy());
}

TEST_F(Tpm2Test, DefineNvramSuccessWithPolicy) {
  uint32_t index = 2;
  size_t length = 5;
  EXPECT_TRUE(tpm_->DefineNvram(
      index, length, Tpm::kTpmNvramWriteDefine | Tpm::kTpmNvramBindToPCR0));
  EXPECT_EQ(index, last_define_space_request.index());
  EXPECT_EQ(length, last_define_space_request.size());
  ASSERT_EQ(1, last_define_space_request.attributes_size());
  EXPECT_EQ(tpm_manager::NVRAM_PERSISTENT_WRITE_LOCK,
            last_define_space_request.attributes(0));
  EXPECT_EQ(tpm_manager::NVRAM_POLICY_PCR0, last_define_space_request.policy());
}

TEST_F(Tpm2Test, DefineNvramSuccessFirmwareReadable) {
  uint32_t index = 2;
  size_t length = 5;
  EXPECT_TRUE(tpm_->DefineNvram(
      index, length,
      Tpm::kTpmNvramWriteDefine | Tpm::kTpmNvramFirmwareReadable));
  EXPECT_EQ(index, last_define_space_request.index());
  EXPECT_EQ(length, last_define_space_request.size());
  ASSERT_EQ(2, last_define_space_request.attributes_size());
  EXPECT_EQ(tpm_manager::NVRAM_PERSISTENT_WRITE_LOCK,
            last_define_space_request.attributes(0));
  EXPECT_EQ(tpm_manager::NVRAM_PLATFORM_READ,
            last_define_space_request.attributes(1));
  EXPECT_EQ(tpm_manager::NVRAM_POLICY_NONE, last_define_space_request.policy());
}

TEST_F(Tpm2Test, DefineNvramFailure) {
  next_define_space_reply.set_result(NVRAM_RESULT_IPC_ERROR);
  EXPECT_FALSE(tpm_->DefineNvram(0, 0, 0));
}

TEST_F(Tpm2Test, DestroyNvramSuccess) {
  uint32_t index = 2;
  EXPECT_TRUE(tpm_->DestroyNvram(index));
  EXPECT_EQ(index, last_destroy_space_request.index());
}

TEST_F(Tpm2Test, DestroyNvramFailure) {
  next_destroy_space_reply.set_result(NVRAM_RESULT_IPC_ERROR);
  EXPECT_FALSE(tpm_->DestroyNvram(0));
}

TEST_F(Tpm2Test, WriteNvramSuccess) {
  uint32_t index = 2;
  std::string data("nvram_data");
  EXPECT_TRUE(tpm_->WriteNvram(index, SecureBlob(data)));
  EXPECT_EQ(index, last_write_space_request.index());
  EXPECT_EQ(data, last_write_space_request.data());
}

TEST_F(Tpm2Test, WriteNvramFailure) {
  next_write_space_reply.set_result(NVRAM_RESULT_IPC_ERROR);
  EXPECT_FALSE(tpm_->WriteNvram(0, SecureBlob()));
}

TEST_F(Tpm2Test, WriteLockNvramSuccess) {
  uint32_t index = 2;
  EXPECT_TRUE(tpm_->WriteLockNvram(index));
  EXPECT_EQ(index, last_lock_space_request.index());
  EXPECT_TRUE(last_lock_space_request.lock_write());
  EXPECT_FALSE(last_lock_space_request.lock_read());
}

TEST_F(Tpm2Test, WriteLockNvramFailure) {
  next_lock_space_reply.set_result(NVRAM_RESULT_IPC_ERROR);
  EXPECT_FALSE(tpm_->WriteLockNvram(0));
}

TEST_F(Tpm2Test, ReadNvramSuccess) {
  uint32_t index = 2;
  SecureBlob read_data;
  std::string nvram_data("nvram_data");
  next_read_space_reply.set_data(nvram_data);
  EXPECT_TRUE(tpm_->ReadNvram(index, &read_data));
  EXPECT_EQ(nvram_data, read_data.to_string());
  EXPECT_EQ(index, last_read_space_request.index());
}

TEST_F(Tpm2Test, ReadNvramFailure) {
  next_read_space_reply.set_result(NVRAM_RESULT_IPC_ERROR);
  SecureBlob read_data;
  EXPECT_FALSE(tpm_->ReadNvram(0, &read_data));
}

TEST_F(Tpm2Test, IsNvramDefinedSuccess) {
  uint32_t index = 2;
  next_list_spaces_reply.add_index_list(index);
  EXPECT_TRUE(tpm_->IsNvramDefined(index));
}

TEST_F(Tpm2Test, IsNvramDefinedFailure) {
  uint32_t index = 2;
  next_list_spaces_reply.set_result(NVRAM_RESULT_IPC_ERROR);
  next_list_spaces_reply.add_index_list(index);
  EXPECT_FALSE(tpm_->IsNvramDefined(index));
}

TEST_F(Tpm2Test, IsNvramDefinedUnknownHandle) {
  uint32_t index = 2;
  next_list_spaces_reply.add_index_list(index + 1);
  EXPECT_FALSE(tpm_->IsNvramDefined(index));
}

TEST_F(Tpm2Test, IsNvramLockedSuccess) {
  uint32_t index = 2;
  next_get_space_info_reply.set_is_write_locked(true);
  EXPECT_TRUE(tpm_->IsNvramLocked(index));
  EXPECT_EQ(index, last_get_space_info_request.index());
}

TEST_F(Tpm2Test, IsNvramLockedNotLocked) {
  next_get_space_info_reply.set_is_write_locked(false);
  EXPECT_FALSE(tpm_->IsNvramLocked(0));
}

TEST_F(Tpm2Test, IsNvramLockedFailure) {
  next_get_space_info_reply.set_is_write_locked(true);
  next_get_space_info_reply.set_result(NVRAM_RESULT_IPC_ERROR);
  EXPECT_FALSE(tpm_->IsNvramLocked(0));
}

TEST_F(Tpm2Test, GetNvramSizeSuccess) {
  uint32_t index = 2;
  unsigned int size = 42;
  next_get_space_info_reply.set_size(size);
  EXPECT_EQ(tpm_->GetNvramSize(index), size);
}

TEST_F(Tpm2Test, GetNvramSizeFailure) {
  uint32_t index = 2;
  unsigned int size = 42;
  next_get_space_info_reply.set_size(size);
  next_get_space_info_reply.set_result(NVRAM_RESULT_IPC_ERROR);
  EXPECT_EQ(tpm_->GetNvramSize(index), 0);
}

TEST_F(Tpm2Test, SealToPCR0Success) {
  SecureBlob value("value");
  SecureBlob sealed_value;
  std::string policy_digest("digest");
  std::map<uint32_t, std::string> pcr_map;
  EXPECT_CALL(mock_tpm_utility_, GetPolicyDigestForPcrValues(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(policy_digest), Return(TPM_RC_SUCCESS)));
  std::string data_to_seal;
  EXPECT_CALL(mock_tpm_utility_, SealData(_, policy_digest, "", _, _))
      .WillOnce(DoAll(SaveArg<0>(&data_to_seal), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(tpm_->SealToPCR0(value, &sealed_value));
  EXPECT_EQ(data_to_seal, value.to_string());
}

TEST_F(Tpm2Test, SealToPCR0PolicyFailure) {
  SecureBlob value("value");
  SecureBlob sealed_value;
  EXPECT_CALL(mock_tpm_utility_, GetPolicyDigestForPcrValues(_, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->SealToPCR0(value, &sealed_value));
}

TEST_F(Tpm2Test, SealToPCR0Failure) {
  SecureBlob value("value");
  SecureBlob sealed_value;
  EXPECT_CALL(mock_tpm_utility_, SealData(_, _, "", _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->SealToPCR0(value, &sealed_value));
}

TEST_F(Tpm2Test, UnsealSuccess) {
  SecureBlob sealed_value("sealed");
  SecureBlob value;
  std::string unsealed_data("unsealed");
  EXPECT_CALL(mock_tpm_utility_, UnsealData(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(unsealed_data), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(tpm_->Unseal(sealed_value, &value));
  EXPECT_EQ(unsealed_data, value.to_string());
}

TEST_F(Tpm2Test, UnsealStartPolicySessionFail) {
  SecureBlob sealed_value("sealed");
  SecureBlob value;
  EXPECT_CALL(mock_policy_session_, StartUnboundSession(true, false))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->Unseal(sealed_value, &value));
}

TEST_F(Tpm2Test, UnsealPolicyPCRFailure) {
  SecureBlob sealed_value("sealed");
  SecureBlob value;
  EXPECT_CALL(mock_policy_session_, PolicyPCR(_))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->Unseal(sealed_value, &value));
}

TEST_F(Tpm2Test, UnsealFailure) {
  SecureBlob sealed_value("sealed");
  SecureBlob value;
  EXPECT_CALL(mock_tpm_utility_, UnsealData(_, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->Unseal(sealed_value, &value));
}

TEST_F(Tpm2Test, SignPolicySuccess) {
  uint32_t pcr_index = 5;
  EXPECT_CALL(mock_policy_session_, PolicyPCR(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_policy_session_, GetDelegate())
      .WillOnce(Return(&mock_authorization_delegate_));
  std::string tpm_signature(32, 'b');
  EXPECT_CALL(mock_tpm_utility_,
              Sign(_, _, _, _, _, &mock_authorization_delegate_, _))
      .WillOnce(DoAll(SetArgPointee<6>(tpm_signature), Return(TPM_RC_SUCCESS)));
  SecureBlob signature;
  EXPECT_TRUE(tpm_->Sign(SecureBlob("key_blob"), SecureBlob("input"), pcr_index,
                         &signature));
  EXPECT_EQ(signature.to_string(), tpm_signature);
}

TEST_F(Tpm2Test, SignHmacSuccess) {
  EXPECT_CALL(mock_hmac_session_, GetDelegate())
      .WillOnce(Return(&mock_authorization_delegate_));
  std::string tpm_signature(32, 'b');
  EXPECT_CALL(mock_tpm_utility_,
              Sign(_, _, _, _, _, &mock_authorization_delegate_, _))
      .WillOnce(DoAll(SetArgPointee<6>(tpm_signature), Return(TPM_RC_SUCCESS)));

  SecureBlob signature;
  EXPECT_TRUE(tpm_->Sign(SecureBlob("key_blob"), SecureBlob("input"),
                         kNotBoundToPCR, &signature));
  EXPECT_EQ(signature.to_string(), tpm_signature);
}

TEST_F(Tpm2Test, SignLoadFailure) {
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillRepeatedly(Return(TPM_RC_FAILURE));

  SecureBlob signature;
  EXPECT_FALSE(tpm_->Sign(SecureBlob("key_blob"), SecureBlob("input"),
                          kNotBoundToPCR, &signature));
}

TEST_F(Tpm2Test, SignFailure) {
  uint32_t handle = 42;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(handle), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, Sign(handle, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));

  SecureBlob signature;
  EXPECT_FALSE(tpm_->Sign(SecureBlob("key_blob"), SecureBlob("input"),
                          kNotBoundToPCR, &signature));
}

TEST_F(Tpm2Test, CreatePCRBoundKeySuccess) {
  uint32_t index = 2;
  std::string pcr_value("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;
  uint32_t modulus = 2048;
  uint32_t exponent = 0x10001;
  EXPECT_CALL(mock_tpm_utility_,
              CreateRSAKeyPair(_, modulus, exponent, _, _, true, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_->CreatePCRBoundKey(
      std::map<uint32_t, std::string>({{index, pcr_value}}),
      trunks::TpmUtility::kDecryptKey, &key_blob, nullptr, &creation_blob));
}

TEST_F(Tpm2Test, CreatePCRBoundKeyPolicyFailure) {
  uint32_t index = 2;
  std::string pcr_value("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;
  EXPECT_CALL(mock_tpm_utility_, GetPolicyDigestForPcrValues(_, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->CreatePCRBoundKey(
      std::map<uint32_t, std::string>({{index, pcr_value}}),
      trunks::TpmUtility::kDecryptKey, &key_blob, nullptr, &creation_blob));
}

TEST_F(Tpm2Test, CreatePCRBoundKeyFailure) {
  uint32_t index = 2;
  std::string pcr_value("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;
  EXPECT_CALL(mock_tpm_utility_, CreateRSAKeyPair(_, _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->CreatePCRBoundKey(
      std::map<uint32_t, std::string>({{index, pcr_value}}),
      trunks::TpmUtility::kDecryptKey, &key_blob, nullptr, &creation_blob));
}

TEST_F(Tpm2Test, CreateMultiplePCRBoundKeySuccess) {
  std::map<uint32_t, std::string> pcr_map({{2, ""}, {5, ""}});
  SecureBlob key_blob;
  SecureBlob creation_blob;
  uint32_t modulus = 2048;
  uint32_t exponent = 0x10001;
  EXPECT_CALL(mock_tpm_utility_,
              CreateRSAKeyPair(_, modulus, exponent, _, _, true, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_->CreatePCRBoundKey(pcr_map,
                                      trunks::TpmUtility::kDecryptKey,
                                      &key_blob, nullptr, &creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeySuccess) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(
          CryptoLib::Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  std::string pcr_policy_value;
  std::map<uint32_t, std::string> pcr_map;
  EXPECT_CALL(mock_trial_session_, PolicyPCR(_))
      .WillOnce(DoAll(SaveArg<0>(&pcr_map),
                      Return(TPM_RC_SUCCESS)));
  std::string policy_digest(32, 'a');
  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_digest), Return(TPM_RC_SUCCESS)));
  trunks::TPMT_PUBLIC public_area;
  public_area.auth_policy.size = policy_digest.size();
  memcpy(public_area.auth_policy.buffer, policy_digest.data(),
         policy_digest.size());
  public_area.object_attributes &= (~trunks::kUserWithAuth);
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_area), Return(TPM_RC_SUCCESS)));
  ASSERT_TRUE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, std::string>({{index, BlobToString(pcr_value)}}),
      key_blob, creation_blob));
  EXPECT_EQ(pcr_map[index], BlobToString(pcr_value));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationBlob) {
  uint32_t index = 2;
  std::string pcr_value("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(
      tpm_->VerifyPCRBoundKey(
          std::map<uint32_t, std::string>({{index, pcr_value}}), key_blob,
          creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationDataCount) {
  uint32_t index = 2;
  std::string pcr_value("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  creation_data.creation_data.pcr_select.count = 0;
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  EXPECT_FALSE(
      tpm_->VerifyPCRBoundKey(
          std::map<uint32_t, std::string>({{index, pcr_value}}), key_blob,
          creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationPCRBank) {
  uint32_t index = 2;
  std::string pcr_value("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA1;
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  EXPECT_FALSE(
      tpm_->VerifyPCRBoundKey(
          std::map<uint32_t, std::string>({{index, pcr_value}}), key_blob,
          creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationPCR) {
  uint32_t index = 2;
  std::string pcr_value("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  pcr_select.pcr_selections[0].pcr_select[index / 8] = 0xFF;
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  EXPECT_FALSE(
      tpm_->VerifyPCRBoundKey(
          std::map<uint32_t, std::string>({{index, pcr_value}}), key_blob,
          creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadCreationPCRDigest) {
  uint32_t index = 2;
  std::string pcr_value("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(CryptoLib::Sha256(SecureBlob("")).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));
  EXPECT_FALSE(
      tpm_->VerifyPCRBoundKey(
          std::map<uint32_t, std::string>({{index, pcr_value}}), key_blob,
          creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyImportedKey) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(
          CryptoLib::Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  EXPECT_CALL(mock_tpm_utility_, CertifyCreation(_, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, std::string>({{index, BlobToString(pcr_value)}}),
      key_blob, creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadSession) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  for (size_t i = 0; i < PCR_SELECT_MIN; ++i) {
    pcr_select.pcr_selections[0].pcr_select[i] = 0;
  }
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(
          CryptoLib::Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  EXPECT_CALL(mock_trial_session_, StartUnboundSession(true, true))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, std::string>({{index, BlobToString(pcr_value)}}),
      key_blob, creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadPolicy) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  for (size_t i = 0; i < PCR_SELECT_MIN; ++i) {
    pcr_select.pcr_selections[0].pcr_select[i] = 0;
  }
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(
          CryptoLib::Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  EXPECT_CALL(mock_trial_session_, PolicyPCR(_))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, std::string>({{index, BlobToString(pcr_value)}}),
      key_blob, creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadDigest) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(
          CryptoLib::Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, std::string>({{index, BlobToString(pcr_value)}}),
      key_blob, creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadPolicyDigest) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(
          CryptoLib::Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  std::string policy_digest(32, 'a');
  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_digest), Return(TPM_RC_SUCCESS)));

  trunks::TPMT_PUBLIC public_area;
  public_area.auth_policy.size = 2;
  public_area.object_attributes &= (~trunks::kUserWithAuth);
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_area), Return(TPM_RC_SUCCESS)));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, std::string>({{index, BlobToString(pcr_value)}}),
      key_blob, creation_blob));
}

TEST_F(Tpm2Test, VerifyPCRBoundKeyBadAttributes) {
  uint32_t index = 2;
  const Blob pcr_value = BlobFromString("pcr_value");
  SecureBlob key_blob;
  SecureBlob creation_blob;

  trunks::TPM2B_CREATION_DATA creation_data;
  trunks::TPML_PCR_SELECTION& pcr_select =
      creation_data.creation_data.pcr_select;
  pcr_select.count = 1;
  pcr_select.pcr_selections[0].hash = trunks::TPM_ALG_SHA256;
  SetPcrSelectData(pcr_select.pcr_selections[0].pcr_select, index);
  creation_data.creation_data.pcr_digest =
      trunks::Make_TPM2B_DIGEST(
          CryptoLib::Sha256ToSecureBlob(pcr_value).to_string());
  EXPECT_CALL(mock_blob_parser_, ParseCreationBlob(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(creation_data), Return(true)));

  std::string policy_digest(32, 'a');
  EXPECT_CALL(mock_trial_session_, GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(policy_digest), Return(TPM_RC_SUCCESS)));

  trunks::TPMT_PUBLIC public_area;
  public_area.auth_policy.size = policy_digest.size();
  memcpy(public_area.auth_policy.buffer, policy_digest.data(),
         policy_digest.size());
  public_area.object_attributes = trunks::kUserWithAuth;
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_area), Return(TPM_RC_SUCCESS)));
  EXPECT_FALSE(tpm_->VerifyPCRBoundKey(
      std::map<uint32_t, std::string>({{index, BlobToString(pcr_value)}}),
      key_blob, creation_blob));
}

TEST_F(Tpm2Test, ExtendPCRSuccess) {
  uint32_t index = 5;
  const Blob extension = BlobFromString("extension");
  std::string pcr_value;
  EXPECT_CALL(mock_tpm_utility_, ExtendPCR(index, _, _))
      .WillOnce(DoAll(SaveArg<1>(&pcr_value), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(tpm_->ExtendPCR(index, extension));
  EXPECT_EQ(pcr_value, BlobToString(extension));
}

TEST_F(Tpm2Test, ExtendPCRFailure) {
  uint32_t index = 5;
  const Blob extension = BlobFromString("extension");
  EXPECT_CALL(mock_tpm_utility_, ExtendPCR(index, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->ExtendPCR(index, extension));
}

TEST_F(Tpm2Test, ReadPCRSuccess) {
  uint32_t index = 5;
  Blob pcr_value;
  std::string pcr_digest("digest");
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(index, _))
      .WillOnce(DoAll(SetArgPointee<1>(pcr_digest), Return(TPM_RC_SUCCESS)));
  EXPECT_TRUE(tpm_->ReadPCR(index, &pcr_value));
  EXPECT_EQ(BlobFromString(pcr_digest), pcr_value);
}

TEST_F(Tpm2Test, ReadPCRFailure) {
  uint32_t index = 5;
  Blob pcr_value;
  EXPECT_CALL(mock_tpm_utility_, ReadPCR(index, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->ReadPCR(index, &pcr_value));
}

TEST_F(Tpm2Test, WrapRsaKeySuccess) {
  std::string key_blob("key_blob");
  SecureBlob modulus;
  SecureBlob prime_factor;
  EXPECT_CALL(mock_tpm_utility_, ImportRSAKey(_, _, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<6>(key_blob), Return(TPM_RC_SUCCESS)));
  SecureBlob wrapped_key;
  EXPECT_TRUE(tpm_->WrapRsaKey(modulus, prime_factor, &wrapped_key));
  EXPECT_EQ(key_blob, wrapped_key.to_string());
}

TEST_F(Tpm2Test, WrapRsaKeyFailure) {
  SecureBlob wrapped_key;
  EXPECT_CALL(mock_tpm_utility_, ImportRSAKey(_, _, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_FALSE(tpm_->WrapRsaKey(SecureBlob(), SecureBlob(), &wrapped_key));
}

TEST_F(Tpm2Test, LoadWrappedKeySuccess) {
  SecureBlob wrapped_key("wrapped_key");
  trunks::TPM_HANDLE handle = trunks::TPM_RH_FIRST;
  std::string loaded_key;
  ScopedKeyHandle key_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(DoAll(SaveArg<0>(&loaded_key), SetArgPointee<2>(handle),
                      Return(TPM_RC_SUCCESS)));
  EXPECT_EQ(tpm_->LoadWrappedKey(wrapped_key, &key_handle), Tpm::kTpmRetryNone);
  EXPECT_EQ(handle, key_handle.value());
  EXPECT_EQ(loaded_key, wrapped_key.to_string());
}

TEST_F(Tpm2Test, LoadWrappedKeyFailure) {
  SecureBlob wrapped_key("wrapped_key");
  ScopedKeyHandle key_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  EXPECT_EQ(tpm_->LoadWrappedKey(wrapped_key, &key_handle),
            Tpm::kTpmRetryFailNoRetry);
}

TEST_F(Tpm2Test, LoadWrappedKeyTransientDevWriteFailure) {
  SecureBlob wrapped_key("wrapped_key");
  ScopedKeyHandle key_handle;
  EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
      .WillOnce(Return(trunks::TRUNKS_RC_WRITE_ERROR));
  EXPECT_EQ(tpm_->LoadWrappedKey(wrapped_key, &key_handle),
            Tpm::kTpmRetryCommFailure);
  EXPECT_TRUE(tpm_->IsTransient(Tpm::kTpmRetryCommFailure));
}

TEST_F(Tpm2Test, LoadWrappedKeyRetryActions) {
  constexpr TPM_RC error_code_fmt0 = trunks::TPM_RC_REFERENCE_H0;
  constexpr TPM_RC error_code_fmt1 = trunks::TPM_RC_HANDLE | trunks::TPM_RC_2;
  SecureBlob wrapped_key("wrapped_key");
  ScopedKeyHandle key_handle;
  // For hardware TPM and Resource Manager, should use the error number to
  // determine the corresponding retry action.
  for (TPM_RC layer_code : {trunks::kResourceManagerTpmErrorBase, TPM_RC(0)}) {
    EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
        .WillOnce(Return(error_code_fmt0 | layer_code))
        .WillOnce(Return(error_code_fmt1 | layer_code))
        .RetiresOnSaturation();
    EXPECT_EQ(tpm_->LoadWrappedKey(wrapped_key, &key_handle),
              Tpm::kTpmRetryInvalidHandle);
    EXPECT_EQ(tpm_->LoadWrappedKey(wrapped_key, &key_handle),
              Tpm::kTpmRetryInvalidHandle);
  }
  // For response codes produced by other layers (e.g. trunks, SAPI), should
  // always return FailNoRetry, even if lower 12 bits match hardware TPM errors.
  for (TPM_RC layer_code : {trunks::kSapiErrorBase, trunks::kTrunksErrorBase}) {
    EXPECT_CALL(mock_tpm_utility_, LoadKey(_, _, _))
        .WillOnce(Return(error_code_fmt0 | layer_code))
        .WillOnce(Return(error_code_fmt1 | layer_code))
        .RetiresOnSaturation();
    EXPECT_EQ(tpm_->LoadWrappedKey(wrapped_key, &key_handle),
              Tpm::kTpmRetryFailNoRetry);
    EXPECT_EQ(tpm_->LoadWrappedKey(wrapped_key, &key_handle),
              Tpm::kTpmRetryFailNoRetry);
  }
}

TEST_F(Tpm2Test, CloseHandle) {
  TpmKeyHandle key_handle = 42;
  EXPECT_CALL(mock_tpm_, FlushContextSync(key_handle, _))
      .WillRepeatedly(Return(TPM_RC_SUCCESS));
  tpm_->CloseHandle(key_handle);
}

TEST_F(Tpm2Test, EncryptBlobSuccess) {
  TpmKeyHandle handle = 42;
  std::string tpm_ciphertext(32, 'a');
  SecureBlob key(32, 'b');
  SecureBlob plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricEncrypt(handle, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(tpm_ciphertext), Return(TPM_RC_SUCCESS)));
  SecureBlob ciphertext;
  EXPECT_EQ(Tpm::kTpmRetryNone,
            tpm_->EncryptBlob(handle, plaintext, key, &ciphertext));
}

TEST_F(Tpm2Test, EncryptBlobBadAesKey) {
  TpmKeyHandle handle = 42;
  std::string tpm_ciphertext(32, 'a');
  SecureBlob key(16, 'b');
  SecureBlob plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricEncrypt(handle, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(tpm_ciphertext), Return(TPM_RC_SUCCESS)));
  SecureBlob ciphertext;
  EXPECT_EQ(Tpm::kTpmRetryFailNoRetry,
            tpm_->EncryptBlob(handle, plaintext, key, &ciphertext));
}

TEST_F(Tpm2Test, EncryptBlobBadTpmEncrypt) {
  TpmKeyHandle handle = 42;
  std::string tpm_ciphertext(16, 'a');
  SecureBlob key(32, 'b');
  SecureBlob plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricEncrypt(handle, _, _, _, _, _))
      .WillOnce(
          DoAll(SetArgPointee<5>(tpm_ciphertext), Return(TPM_RC_SUCCESS)));
  SecureBlob ciphertext;
  EXPECT_EQ(Tpm::kTpmRetryFailNoRetry,
            tpm_->EncryptBlob(handle, plaintext, key, &ciphertext));
}

TEST_F(Tpm2Test, EncryptBlobFailure) {
  TpmKeyHandle handle = 42;
  SecureBlob key(32, 'b');
  SecureBlob plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricEncrypt(handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  SecureBlob ciphertext;
  EXPECT_EQ(Tpm::kTpmRetryFailNoRetry,
            tpm_->EncryptBlob(handle, plaintext, key, &ciphertext));
}

TEST_F(Tpm2Test, DecryptBlobSuccess) {
  TpmKeyHandle handle = 42;
  SecureBlob key(32, 'a');
  SecureBlob ciphertext(32, 'b');
  std::string tpm_plaintext("plaintext");
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(handle, _, _, _, _, _))
      .WillOnce(DoAll(SetArgPointee<5>(tpm_plaintext), Return(TPM_RC_SUCCESS)));
  SecureBlob plaintext;
  EXPECT_EQ(Tpm::kTpmRetryNone,
            tpm_->DecryptBlob(handle, ciphertext, key,
                              std::map<uint32_t, std::string>(), &plaintext));
}

TEST_F(Tpm2Test, DecryptBlobBadAesKey) {
  TpmKeyHandle handle = 42;
  SecureBlob key(16, 'a');
  SecureBlob ciphertext(32, 'b');
  SecureBlob plaintext;
  EXPECT_EQ(Tpm::kTpmRetryFailNoRetry,
            tpm_->DecryptBlob(handle, ciphertext, key,
                              std::map<uint32_t, std::string>(), &plaintext));
}

TEST_F(Tpm2Test, DecryptBlobBadCiphertext) {
  TpmKeyHandle handle = 42;
  SecureBlob key(32, 'a');
  SecureBlob ciphertext(16, 'b');
  SecureBlob plaintext;
  EXPECT_EQ(Tpm::kTpmRetryFailNoRetry,
            tpm_->DecryptBlob(handle, ciphertext, key,
                              std::map<uint32_t, std::string>(), &plaintext));
}

TEST_F(Tpm2Test, DecryptBlobFailure) {
  TpmKeyHandle handle = 42;
  SecureBlob key(32, 'a');
  SecureBlob ciphertext(32, 'b');
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  SecureBlob plaintext;
  EXPECT_EQ(Tpm::kTpmRetryFailNoRetry,
            tpm_->DecryptBlob(handle, ciphertext, key,
                              std::map<uint32_t, std::string>(), &plaintext));
}

TEST_F(Tpm2Test, SealToPcrWithAuthorizationSuccess) {
  TpmKeyHandle handle = 42;
  SecureBlob auth_blob(256, 'a');
  SecureBlob plaintext(32, 'b');
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, SealData(plaintext.to_string(), _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  SecureBlob sealed_data;
  EXPECT_EQ(Tpm::kTpmRetryNone,
            tpm_->SealToPcrWithAuthorization(handle, plaintext, auth_blob,
                                             std::map<uint32_t, std::string>(),
                                             &sealed_data));
}

TEST_F(Tpm2Test, SealToPcrWithAuthorizationBadAuthSize) {
  TpmKeyHandle handle = 42;
  SecureBlob auth_blob(128, 'a');
  SecureBlob plaintext(32, 'b');
  SecureBlob sealed_data;
  EXPECT_EQ(Tpm::kTpmRetryFailNoRetry,
            tpm_->SealToPcrWithAuthorization(handle, plaintext, auth_blob,
                                             std::map<uint32_t, std::string>(),
                                             &sealed_data));
}

TEST_F(Tpm2Test, UnsealWithAuthorizationSuccess) {
  TpmKeyHandle handle = 42;
  SecureBlob auth_blob(256, 'a');
  SecureBlob sealed_data(32, 'b');
  EXPECT_CALL(mock_tpm_utility_, AsymmetricDecrypt(handle, _, _, _, _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_CALL(mock_tpm_utility_, UnsealData(sealed_data.to_string(), _, _))
      .WillOnce(Return(TPM_RC_SUCCESS));
  SecureBlob plaintext;
  EXPECT_EQ(Tpm::kTpmRetryNone,
            tpm_->UnsealWithAuthorization(handle, sealed_data, auth_blob,
                                          std::map<uint32_t, std::string>(),
                                          &plaintext));
}

TEST_F(Tpm2Test, UnsealWithAuthorizationBadAuthSize) {
  TpmKeyHandle handle = 42;
  SecureBlob auth_blob(128, 'a');
  SecureBlob sealed_data(32, 'b');
  SecureBlob plaintext;
  EXPECT_EQ(Tpm::kTpmRetryFailNoRetry,
            tpm_->UnsealWithAuthorization(handle, sealed_data, auth_blob,
                                          std::map<uint32_t, std::string>(),
                                          &plaintext));
}

TEST_F(Tpm2Test, GetPublicKeyHashSuccess) {
  TpmKeyHandle handle = 42;
  trunks::TPMT_PUBLIC public_data;
  SecureBlob public_key("hello");
  public_data.unique.rsa =
      trunks::Make_TPM2B_PUBLIC_KEY_RSA(public_key.to_string());
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(handle, _))
      .WillOnce(DoAll(SetArgPointee<1>(public_data), Return(TPM_RC_SUCCESS)));
  SecureBlob public_key_hash;
  EXPECT_EQ(Tpm::kTpmRetryNone,
            tpm_->GetPublicKeyHash(handle, &public_key_hash));
  SecureBlob expected_key_hash = CryptoLib::Sha256(public_key);
  EXPECT_EQ(expected_key_hash, public_key_hash);
}

TEST_F(Tpm2Test, GetPublicKeyHashFailure) {
  TpmKeyHandle handle = 42;
  EXPECT_CALL(mock_tpm_utility_, GetKeyPublicArea(handle, _))
      .WillOnce(Return(TPM_RC_FAILURE));
  SecureBlob public_key_hash;
  EXPECT_EQ(Tpm::kTpmRetryFailNoRetry,
            tpm_->GetPublicKeyHash(handle, &public_key_hash));
}

TEST_F(Tpm2Test, DeclareTpmFirmwareStable) {
  EXPECT_CALL(mock_tpm_utility_, DeclareTpmFirmwareStable())
      .Times(2)
      .WillOnce(Return(TPM_RC_FAILURE))
      .WillOnce(Return(TPM_RC_SUCCESS));
  // First attempt shall call TpmUtility since we haven't called it yet.
  tpm_->DeclareTpmFirmwareStable();
  // Second attempt shall call TpmUtility since the first attempt failed.
  tpm_->DeclareTpmFirmwareStable();
  // Subsequent attempts shall do nothing since we already succeeded on the
  // second attempt.
  tpm_->DeclareTpmFirmwareStable();
  tpm_->DeclareTpmFirmwareStable();
}

TEST_F(Tpm2Test, SetUserType) {
  // Setting user type to Owner results in allowing CCD password change.
  EXPECT_CALL(mock_tpm_utility_, ManageCCDPwd(true))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_->SetUserType(Tpm::UserType::Owner));
  testing::Mock::VerifyAndClearExpectations(&mock_tpm_utility_);
  // Setting user type to NonOwner results in prohibiting CCD password change.
  EXPECT_CALL(mock_tpm_utility_, ManageCCDPwd(false))
      .WillOnce(Return(TPM_RC_SUCCESS));
  EXPECT_TRUE(tpm_->SetUserType(Tpm::UserType::NonOwner));
}

TEST_F(Tpm2Test, SetUserTypeAfterNonOwner) {
  EXPECT_CALL(mock_tpm_utility_, ManageCCDPwd(_))
      .WillOnce(Return(TPM_RC_SUCCESS));
  // First attempt shall call TpmUtility since we haven't called it yet.
  EXPECT_TRUE(tpm_->SetUserType(Tpm::UserType::NonOwner));
  testing::Mock::VerifyAndClearExpectations(&mock_tpm_utility_);

  EXPECT_CALL(mock_tpm_utility_, ManageCCDPwd(_))
      .Times(0);
  // Second attempt shall not call TpmUtility since transitioning from NonOwner
  // is not possible.
  EXPECT_TRUE(tpm_->SetUserType(Tpm::UserType::Owner));
  // Third attempt shall not call TpmUtility since the current type is still
  // NonOwner.
  EXPECT_TRUE(tpm_->SetUserType(Tpm::UserType::NonOwner));
}

TEST_F(Tpm2Test, SetUserTypeCaching) {
  EXPECT_CALL(mock_tpm_utility_, ManageCCDPwd(_))
      .Times(2)
      .WillOnce(Return(TPM_RC_FAILURE))
      .WillOnce(Return(TPM_RC_SUCCESS));
  // First attempt shall call TpmUtility since we haven't called it yet, and
  // fail. Despite the failure in TpmUtility, SetUserType shall return success
  // since errors are ignored when transitioning to Owner.
  EXPECT_TRUE(tpm_->SetUserType(Tpm::UserType::Owner));
  // Second attempt shall call TpmUtility since the first attempt failed.
  EXPECT_TRUE(tpm_->SetUserType(Tpm::UserType::Owner));
  testing::Mock::VerifyAndClearExpectations(&mock_tpm_utility_);

  // Subsequent attempts shall do nothing since we already succeeded on the
  // second attempt.
  EXPECT_CALL(mock_tpm_utility_, ManageCCDPwd(_))
      .Times(0);
  EXPECT_TRUE(tpm_->SetUserType(Tpm::UserType::Owner));
}

TEST_F(Tpm2Test, RemoveOwnerDependencySuccess) {
  EXPECT_TRUE(tpm_->RemoveOwnerDependency(
      TpmPersistentState::TpmOwnerDependency::kInstallAttributes));
  EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Nvram,
            last_remove_owner_dependency_request.owner_dependency());
  EXPECT_TRUE(tpm_->RemoveOwnerDependency(
      TpmPersistentState::TpmOwnerDependency::kAttestation));
  EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Attestation,
            last_remove_owner_dependency_request.owner_dependency());
}

TEST_F(Tpm2Test, RemoveOwnerDependencyFailure) {
  next_remove_owner_dependency_reply.set_status(
      tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_FALSE(tpm_->RemoveOwnerDependency(
      TpmPersistentState::TpmOwnerDependency::kInstallAttributes));
  EXPECT_EQ(tpm_manager::kTpmOwnerDependency_Nvram,
            last_remove_owner_dependency_request.owner_dependency());
}

TEST_F(Tpm2Test, RemoveOwnerDependencyUnknown) {
  TpmPersistentState::TpmOwnerDependency unknown_dep =
      static_cast<TpmPersistentState::TpmOwnerDependency>(100);
  EXPECT_CALL(mock_tpm_owner_, RemoveOwnerDependency(_, _))
        .Times(0);
  EXPECT_TRUE(tpm_->RemoveOwnerDependency(unknown_dep));
}

TEST_F(Tpm2Test, ClearStoredPasswordSuccess) {
  EXPECT_CALL(mock_tpm_owner_, ClearStoredOwnerPassword(_, _))
      .Times(1);
  EXPECT_TRUE(tpm_->ClearStoredPassword());
}

TEST_F(Tpm2Test, ClearStoredPasswordFailure) {
  next_clear_stored_password_reply.set_status(
      tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_CALL(mock_tpm_owner_, ClearStoredOwnerPassword(_, _))
      .Times(1);
  EXPECT_FALSE(tpm_->ClearStoredPassword());
}

TEST_F(Tpm2Test, HandleOwnershipTakenEvent) {
  tpm_status_.set_owned(false);

  EXPECT_CALL(mock_tpm_owner_, GetTpmStatus(_, _)).Times(1);

  EXPECT_FALSE(tpm_->IsOwned());
  EXPECT_FALSE(tpm_->IsOwned());

  tpm_->HandleOwnershipTakenEvent();
  EXPECT_TRUE(tpm_->IsOwned());
  EXPECT_TRUE(tpm_->IsOwned());
}

namespace {

struct Tpm2RsaSignatureSecretSealingTestParam {
  Tpm2RsaSignatureSecretSealingTestParam(
      const std::vector<ChallengeSignatureAlgorithm>& supported_algorithms,
      ChallengeSignatureAlgorithm chosen_algorithm,
      TPM_ALG_ID chosen_scheme,
      TPM_ALG_ID chosen_hash_alg)
      : supported_algorithms(supported_algorithms),
        chosen_algorithm(chosen_algorithm),
        chosen_scheme(chosen_scheme),
        chosen_hash_alg(chosen_hash_alg) {}

  std::vector<ChallengeSignatureAlgorithm> supported_algorithms;
  ChallengeSignatureAlgorithm chosen_algorithm;
  TPM_ALG_ID chosen_scheme;
  TPM_ALG_ID chosen_hash_alg;
};

class Tpm2RsaSignatureSecretSealingTest
    : public Tpm2Test,
      public testing::WithParamInterface<
          Tpm2RsaSignatureSecretSealingTestParam> {
 protected:
  const int kKeySizeBits = 2048;
  const int kKeyPublicExponent = 65537;
  const std::vector<uint32_t> kPcrIndexes{0, 5};
  const std::string kSecretValue = std::string(32, '\1');
  const trunks::TPM_HANDLE kKeyHandle = trunks::TPM_RH_FIRST;
  const std::string kKeyName = std::string("fake key");
  const std::string kSealedSecretValue = std::string("sealed secret");

  Tpm2RsaSignatureSecretSealingTest() {
    crypto::ScopedBIGNUM e(BN_new());
    CHECK(e);
    EXPECT_TRUE(BN_set_word(e.get(), kKeyPublicExponent));
    crypto::ScopedRSA rsa(RSA_new());
    CHECK(rsa);
    EXPECT_TRUE(RSA_generate_key_ex(rsa.get(), kKeySizeBits, e.get(), nullptr));
    const crypto::ScopedEVP_PKEY pkey(EVP_PKEY_new());
    CHECK(pkey);
    EXPECT_TRUE(EVP_PKEY_set1_RSA(pkey.get(), rsa.get()));
    // Obtain the DER-encoded SubjectPublicKeyInfo.
    const int key_spki_der_length = i2d_PUBKEY(pkey.get(), nullptr);
    CHECK_GE(key_spki_der_length, 0);
    key_spki_der_.resize(key_spki_der_length);
    unsigned char* key_spki_der_buffer =
        reinterpret_cast<unsigned char*>(&key_spki_der_[0]);
    CHECK_EQ(key_spki_der_.size(),
             i2d_PUBKEY(pkey.get(), &key_spki_der_buffer));
    // Obtain the key modulus.
    key_modulus_.resize(RSA_size(rsa.get()));
    const BIGNUM* n;
    RSA_get0_key(rsa.get(), &n, nullptr, nullptr);
    CHECK_EQ(
        key_modulus_.length(),
        BN_bn2bin(n, reinterpret_cast<unsigned char*>(&key_modulus_[0])));
  }

  const std::vector<ChallengeSignatureAlgorithm>& supported_algorithms() const {
    return GetParam().supported_algorithms;
  }
  ChallengeSignatureAlgorithm chosen_algorithm() const {
    return GetParam().chosen_algorithm;
  }
  TPM_ALG_ID chosen_scheme() const { return GetParam().chosen_scheme; }
  TPM_ALG_ID chosen_hash_alg() const { return GetParam().chosen_hash_alg; }

  SignatureSealingBackend* signature_sealing_backend() {
    SignatureSealingBackend* result = tpm_->GetSignatureSealingBackend();
    CHECK(result);
    return result;
  }

  Blob key_spki_der_;
  std::string key_modulus_;
};

}  // namespace

TEST_P(Tpm2RsaSignatureSecretSealingTest, Seal) {
  const std::string kTrialPcrPolicyDigest(SHA256_DIGEST_LENGTH, '\1');
  const std::string kTrialPolicyDigest(SHA256_DIGEST_LENGTH, '\2');
  std::map<uint32_t, Blob> pcr_values;
  for (uint32_t pcr_index : kPcrIndexes)
    pcr_values[pcr_index] = BlobFromString("fake PCR");

  // Set up mock expectations for the secret creation.
  EXPECT_CALL(mock_tpm_utility_,
              LoadRSAPublicKey(trunks::TpmUtility::kSignKey, chosen_scheme(),
                               chosen_hash_alg(), key_modulus_,
                               kKeyPublicExponent, _, _))
      .WillOnce(DoAll(SetArgPointee<6>(kKeyHandle), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, GetKeyName(kKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kKeyName), Return(TPM_RC_SUCCESS)));
  trunks::TPMT_SIGNATURE tpmt_signature;
  memset(&tpmt_signature, 0, sizeof(trunks::TPMT_SIGNATURE));
  {
    InSequence s;
    EXPECT_CALL(mock_trial_session_, PolicyPCR(_))
        .WillOnce(Return(TPM_RC_SUCCESS));
    EXPECT_CALL(mock_trial_session_, GetDigest(_))
        .WillOnce(DoAll(SetArgPointee<0>(kTrialPcrPolicyDigest),
                        Return(TPM_RC_SUCCESS)));
    EXPECT_CALL(
        mock_trial_session_,
        PolicySigned(kKeyHandle, kKeyName, std::string() /* nonce */,
                     std::string() /* cp_hash */,
                     std::string() /* policy_ref */, 0 /* expiration */, _, _))
        .WillOnce(DoAll(SaveArg<6>(&tpmt_signature), Return(TPM_RC_SUCCESS)));
    EXPECT_CALL(mock_trial_session_, GetDigest(_))
        .WillOnce(DoAll(SetArgPointee<0>(kTrialPolicyDigest),
                        Return(TPM_RC_SUCCESS)));
  }
  EXPECT_CALL(mock_tpm_utility_, GenerateRandom(kSecretValue.size(), _, _))
      .WillOnce(DoAll(SetArgPointee<2>(kSecretValue), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_,
              SealData(kSecretValue, kTrialPolicyDigest, "", _, _))
      .WillOnce(
          DoAll(SetArgPointee<4>(kSealedSecretValue), Return(TPM_RC_SUCCESS)));

  // Trigger the secret creation.
  SecureBlob secret_value;
  SignatureSealedData sealed_data;
  EXPECT_TRUE(signature_sealing_backend()->CreateSealedSecret(
      key_spki_der_, supported_algorithms(), {pcr_values},
      Blob() /* delegate_blob */, Blob() /* delegate_secret */, &secret_value,
      &sealed_data));
  EXPECT_EQ(secret_value, SecureBlob(kSecretValue));
  ASSERT_TRUE(sealed_data.has_tpm2_policy_signed_data());
  const SignatureSealedData_Tpm2PolicySignedData& sealed_data_contents =
      sealed_data.tpm2_policy_signed_data();
  EXPECT_EQ(BlobToString(key_spki_der_),
            sealed_data_contents.public_key_spki_der());
  EXPECT_EQ(kSealedSecretValue, sealed_data_contents.srk_wrapped_secret());
  EXPECT_EQ(chosen_scheme(), sealed_data_contents.scheme());
  EXPECT_EQ(chosen_hash_alg(), sealed_data_contents.hash_alg());

  // Validate values passed to mocks.
  ASSERT_EQ(chosen_scheme(), tpmt_signature.sig_alg);
  EXPECT_EQ(chosen_hash_alg(), tpmt_signature.signature.rsassa.hash);
  EXPECT_EQ(0, tpmt_signature.signature.rsassa.sig.size);
}

TEST_P(Tpm2RsaSignatureSecretSealingTest, Unseal) {
  const std::string kTpmNonce(SHA1_DIGEST_SIZE, '\1');
  const std::string kChallengeValue(kTpmNonce +
                                    (std::string(4, '\0') /* expiration */));
  const std::string kSignatureValue("fake signature");
  const std::string kPolicyDigest("fake digest");
  const std::string kPcrValue("fake PCR");

  SignatureSealedData sealed_data;
  SignatureSealedData_Tpm2PolicySignedData* const sealed_data_contents =
      sealed_data.mutable_tpm2_policy_signed_data();
  sealed_data_contents->set_public_key_spki_der(BlobToString(key_spki_der_));
  sealed_data_contents->set_srk_wrapped_secret(kSealedSecretValue);
  sealed_data_contents->set_scheme(chosen_scheme());
  sealed_data_contents->set_hash_alg(chosen_hash_alg());
  SignatureSealedData_Tpm2PcrRestriction* const pcr_restriction =
      sealed_data_contents->add_pcr_restrictions();
  for (uint32_t pcr_index : kPcrIndexes) {
    SignatureSealedData_PcrValue* const pcr_values_item =
        pcr_restriction->add_pcr_values();
    pcr_values_item->set_pcr_index(pcr_index);
    pcr_values_item->set_pcr_value(kPcrValue);
  }
  pcr_restriction->set_policy_digest(std::string(SHA256_DIGEST_LENGTH, '\1'));

  // Set up mock expectations for the challenge generation.
  for (uint32_t pcr_index : kPcrIndexes) {
    EXPECT_CALL(mock_tpm_utility_, ReadPCR(pcr_index, _))
        .WillOnce(DoAll(SetArgPointee<1>(kPcrValue), Return(TPM_RC_SUCCESS)));
  }
  EXPECT_CALL(mock_policy_session_, GetDelegate())
      .WillRepeatedly(Return(&mock_authorization_delegate_));
  EXPECT_CALL(mock_authorization_delegate_, GetTpmNonce(_))
      .WillOnce(DoAll(SetArgPointee<0>(kTpmNonce), Return(true)));
  std::map<uint32_t, std::string> pcr_map;
  for (int pcr_index : kPcrIndexes) {
    pcr_map.emplace(pcr_index, std::string());
  }
  EXPECT_CALL(mock_policy_session_, PolicyPCR(pcr_map))
      .WillOnce(Return(TPM_RC_SUCCESS));

  // Trigger the challenge generation.
  std::unique_ptr<SignatureSealingBackend::UnsealingSession> unsealing_session(
      signature_sealing_backend()->CreateUnsealingSession(
          sealed_data, key_spki_der_, supported_algorithms(),
          Blob() /* delegate_blob */, Blob() /* delegate_secret */));
  ASSERT_TRUE(unsealing_session);
  EXPECT_EQ(chosen_algorithm(), unsealing_session->GetChallengeAlgorithm());
  EXPECT_EQ(BlobFromString(kChallengeValue),
            unsealing_session->GetChallengeValue());

  // Set up mock expectations for the unsealing.
  EXPECT_CALL(mock_tpm_utility_,
              LoadRSAPublicKey(trunks::TpmUtility::kSignKey, chosen_scheme(),
                               chosen_hash_alg(), key_modulus_,
                               kKeyPublicExponent, _, _))
      .WillOnce(DoAll(SetArgPointee<6>(kKeyHandle), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_, GetKeyName(kKeyHandle, _))
      .WillOnce(DoAll(SetArgPointee<1>(kKeyName), Return(TPM_RC_SUCCESS)));
  trunks::TPMT_SIGNATURE tpmt_signature;
  memset(&tpmt_signature, 0, sizeof(trunks::TPMT_SIGNATURE));
  EXPECT_CALL(
      mock_policy_session_,
      PolicySigned(kKeyHandle, kKeyName, kTpmNonce, std::string() /* cp_hash */,
                   std::string() /* policy_ref */, 0 /* expiration */, _, _))
      .WillOnce(DoAll(SaveArg<6>(&tpmt_signature), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_policy_session_, GetDigest(_))
      .WillOnce(DoAll(SetArgPointee<0>(kPolicyDigest), Return(TPM_RC_SUCCESS)));
  EXPECT_CALL(mock_tpm_utility_,
              UnsealData(kSealedSecretValue, &mock_authorization_delegate_, _))
      .WillOnce(DoAll(SetArgPointee<2>(kSecretValue), Return(TPM_RC_SUCCESS)));

  // Trigger the unsealing.
  SecureBlob unsealed_secret_value;
  EXPECT_TRUE(unsealing_session->Unseal(BlobFromString(kSignatureValue),
                                        &unsealed_secret_value));
  EXPECT_EQ(kSecretValue, unsealed_secret_value.to_string());

  // Validate values passed to mocks.
  ASSERT_EQ(chosen_scheme(), tpmt_signature.sig_alg);
  EXPECT_EQ(chosen_hash_alg(), tpmt_signature.signature.rsassa.hash);
  EXPECT_EQ(kSignatureValue,
            std::string(tpmt_signature.signature.rsassa.sig.buffer,
                        tpmt_signature.signature.rsassa.sig.buffer +
                            tpmt_signature.signature.rsassa.sig.size));
}

INSTANTIATE_TEST_SUITE_P(SingleAlgorithm,
                         Tpm2RsaSignatureSecretSealingTest,
                         Values(Tpm2RsaSignatureSecretSealingTestParam(
                                    {CHALLENGE_RSASSA_PKCS1_V1_5_SHA1},
                                    CHALLENGE_RSASSA_PKCS1_V1_5_SHA1,
                                    trunks::TPM_ALG_RSASSA,
                                    trunks::TPM_ALG_SHA1),
                                Tpm2RsaSignatureSecretSealingTestParam(
                                    {CHALLENGE_RSASSA_PKCS1_V1_5_SHA256},
                                    CHALLENGE_RSASSA_PKCS1_V1_5_SHA256,
                                    trunks::TPM_ALG_RSASSA,
                                    trunks::TPM_ALG_SHA256),
                                Tpm2RsaSignatureSecretSealingTestParam(
                                    {CHALLENGE_RSASSA_PKCS1_V1_5_SHA384},
                                    CHALLENGE_RSASSA_PKCS1_V1_5_SHA384,
                                    trunks::TPM_ALG_RSASSA,
                                    trunks::TPM_ALG_SHA384),
                                Tpm2RsaSignatureSecretSealingTestParam(
                                    {CHALLENGE_RSASSA_PKCS1_V1_5_SHA512},
                                    CHALLENGE_RSASSA_PKCS1_V1_5_SHA512,
                                    trunks::TPM_ALG_RSASSA,
                                    trunks::TPM_ALG_SHA512)));
INSTANTIATE_TEST_SUITE_P(MultipleAlgorithms,
                         Tpm2RsaSignatureSecretSealingTest,
                         Values(Tpm2RsaSignatureSecretSealingTestParam(
                                    {CHALLENGE_RSASSA_PKCS1_V1_5_SHA384,
                                     CHALLENGE_RSASSA_PKCS1_V1_5_SHA256,
                                     CHALLENGE_RSASSA_PKCS1_V1_5_SHA512},
                                    CHALLENGE_RSASSA_PKCS1_V1_5_SHA384,
                                    trunks::TPM_ALG_RSASSA,
                                    trunks::TPM_ALG_SHA384),
                                Tpm2RsaSignatureSecretSealingTestParam(
                                    {CHALLENGE_RSASSA_PKCS1_V1_5_SHA1,
                                     CHALLENGE_RSASSA_PKCS1_V1_5_SHA256},
                                    CHALLENGE_RSASSA_PKCS1_V1_5_SHA256,
                                    trunks::TPM_ALG_RSASSA,
                                    trunks::TPM_ALG_SHA256)));

}  // namespace cryptohome
