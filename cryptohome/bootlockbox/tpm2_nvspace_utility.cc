// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <trunks/error_codes.h>
#include <trunks/password_authorization_delegate.h>
#include <trunks/tpm_constants.h>
#include <trunks/tpm_utility.h>

#include "cryptohome/bootlockbox/tpm2_nvspace_utility.h"
#include "cryptohome/bootlockbox/tpm_nvspace_interface.h"

using tpm_manager::NvramResult;

namespace cryptohome {

NVSpaceState MapTpmRc(trunks::TPM_RC rc) {
  switch (rc) {
    case trunks::TPM_RC_SUCCESS:
      return NVSpaceState::kNVSpaceNormal;
    case trunks::TPM_RC_HANDLE:
      return NVSpaceState::kNVSpaceUndefined;
    case trunks::TPM_RC_NV_UNINITIALIZED:
      return NVSpaceState::kNVSpaceUninitialized;
    case trunks::TPM_RC_NV_LOCKED:
      return NVSpaceState::kNVSpaceWriteLocked;
    default:
      return NVSpaceState::kNVSpaceError;
  }
}

std::string NvramResult2Str(NvramResult r) {
  switch (r) {
    case NvramResult::NVRAM_RESULT_SUCCESS:
      return "NVRAM_RESULT_SUCCESS";
    case NvramResult::NVRAM_RESULT_DEVICE_ERROR:
      return "NVRAM_RESULT_DEVICE_ERROR";
    case NvramResult::NVRAM_RESULT_ACCESS_DENIED:
      return "NVRAM_RESULT_ACCESS_DENIED";
    case NvramResult::NVRAM_RESULT_INVALID_PARAMETER:
      return "NVRAM_RESULT_INVALID_PARAMETER";
    case NvramResult::NVRAM_RESULT_SPACE_DOES_NOT_EXIST:
      return "NVRAM_RESULT_SPACE_DOES_NOT_EXIST";
    case NvramResult::NVRAM_RESULT_SPACE_ALREADY_EXISTS:
      return "NVRAM_RESULT_SPACE_ALREADY_EXISTS";
    case NvramResult::NVRAM_RESULT_OPERATION_DISABLED:
      return "NVRAM_RESULT_OPERATION_DISABLED";
    case NvramResult::NVRAM_RESULT_INSUFFICIENT_SPACE:
      return "NVRAM_RESULT_INSUFFICIENT_SPACE";
    case NvramResult::NVRAM_RESULT_IPC_ERROR:
      return "NVRAM_RESULT_IPC_ERROR";
  }
}

TPM2NVSpaceUtility::TPM2NVSpaceUtility(
    tpm_manager::TpmNvramInterface* tpm_nvram,
    trunks::TrunksFactory* trunks_factory) {
  tpm_nvram_ = tpm_nvram;
  trunks_factory_ = trunks_factory;
}

void TPM2NVSpaceUtility::InitializationTask(base::WaitableEvent* completion,
                                            bool* result) {
  CHECK(completion);
  CHECK(tpm_manager_thread_.task_runner()->RunsTasksInCurrentSequence());

  if (!tpm_nvram_) {
    default_tpm_nvram_ = std::make_unique<tpm_manager::TpmNvramDBusProxy>();
    if (default_tpm_nvram_->Initialize()) {
      tpm_nvram_ = default_tpm_nvram_.get();
    }
  }
  *result = false;
  if (tpm_nvram_)
    *result = true;
  completion->Signal();
}

void TPM2NVSpaceUtility::ShutdownTask() {
  tpm_nvram_ = nullptr;
  default_tpm_nvram_.reset(nullptr);
}

bool TPM2NVSpaceUtility::Initialize() {
  if (!tpm_manager_thread_.StartWithOptions(base::Thread::Options(
          base::MessagePumpType::IO, 0 /* Default stack size */))) {
    LOG(ERROR) << "Failed to start tpm manager thread";
    return false;
  }
  bool tpm_manager_init_result = false;
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  tpm_manager_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&TPM2NVSpaceUtility::InitializationTask,
                 base::Unretained(this), &event, &tpm_manager_init_result));
  event.Wait();
  if (!tpm_manager_init_result) {
    return false;
  }
  if (!trunks_factory_) {
    default_trunks_factory_ = std::make_unique<trunks::TrunksFactoryImpl>();
    if (!default_trunks_factory_->Initialize()) {
      LOG(ERROR) << "Failed to initialize trunks factory";
      return false;
    }
    trunks_factory_ = default_trunks_factory_.get();
  }
  return true;
}

template <typename ReplyProtoType, typename MethodType>
void TPM2NVSpaceUtility::SendTpmManagerRequestAndWait(
    const MethodType& method,
    ReplyProtoType* reply_proto) {
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  auto callback = base::Bind(
      [](ReplyProtoType* target, base::WaitableEvent* completion,
         const ReplyProtoType& reply) {
        *target = reply;
        completion->Signal();
      },
      reply_proto, &event);
  tpm_manager_thread_.task_runner()->PostTask(FROM_HERE,
                                              base::Bind(method, callback));
  event.Wait();
}

bool TPM2NVSpaceUtility::DefineNVSpace() {
  tpm_manager::DefineSpaceRequest request;
  request.set_index(kBootLockboxNVRamIndex);
  request.set_size(kNVSpaceSize);
  request.add_attributes(tpm_manager::NVRAM_READ_AUTHORIZATION);
  request.add_attributes(tpm_manager::NVRAM_BOOT_WRITE_LOCK);
  request.add_attributes(tpm_manager::NVRAM_WRITE_AUTHORIZATION);
  request.set_authorization_value(kWellKnownPassword);

  tpm_manager::DefineSpaceReply define_space_reply;

  SendTpmManagerRequestAndWait(
      base::Bind(&tpm_manager::TpmNvramInterface::DefineSpace,
                 base::Unretained(tpm_nvram_), request),
      &define_space_reply);

  if (define_space_reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << "Failed to define nvram space: "
               << NvramResult2Str(define_space_reply.result());
    return false;
  }
  // TODO(xzhou): notify tpm_managerd ready to drop key.
  return true;
}

bool TPM2NVSpaceUtility::DefineNVSpaceBeforeOwned() {
  auto pw_auth = trunks_factory_->GetPasswordAuthorization(kWellKnownPassword);
  trunks::TPMA_NV attributes = trunks::TPMA_NV_WRITE_STCLEAR |
                               trunks::TPMA_NV_AUTHREAD |
                               trunks::TPMA_NV_AUTHWRITE;
  trunks::TPM_RC result =
      trunks::GetFormatOneError(trunks_factory_->GetTpmUtility()->DefineNVSpace(
          kBootLockboxNVRamIndex, kNVSpaceSize, attributes, kWellKnownPassword,
          "", pw_auth.get()));
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error define nv space, error: "
               << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TPM2NVSpaceUtility::WriteNVSpace(const std::string& digest) {
  if (digest.size() != SHA256_DIGEST_LENGTH) {
    LOG(ERROR) << "Wrong digest size, expected: " << SHA256_DIGEST_LENGTH
               << " got: " << digest.size();
    return false;
  }

  BootLockboxNVSpace BootLockboxNVSpace;
  BootLockboxNVSpace.version = kNVSpaceVersion;
  BootLockboxNVSpace.flags = 0;
  memcpy(BootLockboxNVSpace.digest, digest.data(), SHA256_DIGEST_LENGTH);
  std::string nvram_data(reinterpret_cast<const char*>(&BootLockboxNVSpace),
                         kNVSpaceSize);
  auto pw_auth = trunks_factory_->GetPasswordAuthorization(kWellKnownPassword);
  trunks::TPM_RC result =
      trunks::GetFormatOneError(trunks_factory_->GetTpmUtility()->WriteNVSpace(
          kBootLockboxNVRamIndex, 0 /* offset */, nvram_data,
          false /* using_owner_authorization */, false /* extend */,
          pw_auth.get()));
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error writing nvram space, error: "
               << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TPM2NVSpaceUtility::ReadNVSpace(std::string* digest,
                                     NVSpaceState* result) {
  *result = NVSpaceState::kNVSpaceError;
  std::string nvram_data;
  auto pw_auth = trunks_factory_->GetPasswordAuthorization(kWellKnownPassword);
  trunks::TPM_RC rc =
      trunks::GetFormatOneError(trunks_factory_->GetTpmUtility()->ReadNVSpace(
          kBootLockboxNVRamIndex, 0 /* offset */, kNVSpaceSize,
          false /* using owner authorization */, &nvram_data, pw_auth.get()));
  if (rc != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading nvram space, error: "
               << trunks::GetErrorString(rc);
    *result = MapTpmRc(rc);
    return false;
  }
  if (nvram_data.size() != kNVSpaceSize) {
    LOG(ERROR) << "Error reading nvram space, invalid data length, expected:"
               << kNVSpaceSize << ", got " << nvram_data.size();
    return false;
  }
  BootLockboxNVSpace BootLockboxNVSpace;
  memcpy(&BootLockboxNVSpace, nvram_data.data(), kNVSpaceSize);
  if (BootLockboxNVSpace.version != kNVSpaceVersion) {
    LOG(ERROR) << "Error reading nvram space, invalid version";
    return false;
  }
  digest->assign(reinterpret_cast<const char*>(BootLockboxNVSpace.digest),
                 SHA256_DIGEST_LENGTH);
  *result = NVSpaceState::kNVSpaceNormal;
  return true;
}

bool TPM2NVSpaceUtility::LockNVSpace() {
  auto pw_auth = trunks_factory_->GetPasswordAuthorization(kWellKnownPassword);
  trunks::TPM_RC result =
      trunks::GetFormatOneError(trunks_factory_->GetTpmUtility()->LockNVSpace(
          kBootLockboxNVRamIndex, false /* lock read */, true /* lock write */,
          false /* using owner authorization */, pw_auth.get()));
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error locking nvspace, error: "
               << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

}  // namespace cryptohome
