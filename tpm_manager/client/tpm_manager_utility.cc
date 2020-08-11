// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/client/tpm_manager_utility.h"

#include <string>

#include <base/bind.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>

namespace tpm_manager {

namespace {
// Singleton and lock for TpmManagerUtility.
TpmManagerUtility* singleton;
base::Lock singleton_lock;
}  // namespace

TpmManagerUtility::TpmManagerUtility(
    tpm_manager::TpmOwnershipInterface* tpm_owner,
    tpm_manager::TpmNvramInterface* tpm_nvram)
    : tpm_owner_(tpm_owner), tpm_nvram_(tpm_nvram) {}

bool TpmManagerUtility::Initialize() {
  if (!tpm_manager_thread_.IsRunning() &&
      !tpm_manager_thread_.StartWithOptions(base::Thread::Options(
          base::MessagePumpType::IO, 0 /* Uses default stack size. */))) {
    LOG(ERROR) << "Failed to start tpm_manager thread.";
    return false;
  }
  if (!tpm_owner_ || !tpm_nvram_) {
    base::WaitableEvent event(base::WaitableEvent::ResetPolicy::MANUAL,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);
    tpm_manager_thread_.task_runner()->PostTask(
        FROM_HERE, base::Bind(&TpmManagerUtility::InitializationTask,
                              base::Unretained(this), &event));
    event.Wait();
  }
  if (!tpm_owner_ || !tpm_nvram_) {
    LOG(ERROR) << "Failed to initialize tpm_managerd clients.";
    return false;
  }

  return true;
}

bool TpmManagerUtility::TakeOwnership() {
  tpm_manager::TakeOwnershipReply reply;
  tpm_manager::TakeOwnershipRequest request;
  SendTpmManagerRequestAndWait(
      base::Bind(&tpm_manager::TpmOwnershipInterface::TakeOwnership,
                 base::Unretained(tpm_owner_), request),
      &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to take ownership.";
    return false;
  }
  return true;
}

bool TpmManagerUtility::GetTpmStatus(bool* is_enabled,
                                     bool* is_owned,
                                     LocalData* local_data) {
  tpm_manager::GetTpmStatusReply tpm_status;
  SendTpmManagerRequestAndWait(
      base::Bind(&tpm_manager::TpmOwnershipInterface::GetTpmStatus,
                 base::Unretained(tpm_owner_),
                 tpm_manager::GetTpmStatusRequest()),
      &tpm_status);
  if (tpm_status.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to read TPM state from tpm_managerd.";
    return false;
  }
  *is_enabled = tpm_status.enabled();
  *is_owned = tpm_status.owned();
  tpm_status.mutable_local_data()->Swap(local_data);
  return true;
}

bool TpmManagerUtility::GetVersionInfo(uint32_t* family,
                                       uint64_t* spec_level,
                                       uint32_t* manufacturer,
                                       uint32_t* tpm_model,
                                       uint64_t* firmware_version,
                                       std::string* vendor_specific) {
  if (!family || !spec_level || !manufacturer || !tpm_model ||
      !firmware_version || !vendor_specific) {
    LOG(ERROR) << __func__ << ": some input args are not initialized.";
    return false;
  }

  tpm_manager::GetVersionInfoReply version_info;
  SendTpmManagerRequestAndWait(
      base::Bind(&tpm_manager::TpmOwnershipInterface::GetVersionInfo,
                 base::Unretained(tpm_owner_),
                 tpm_manager::GetVersionInfoRequest()),
      &version_info);
  if (version_info.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": failed to get version info from tpm_managerd.";
    return false;
  }

  *family = version_info.family();
  *spec_level = version_info.spec_level();
  *manufacturer = version_info.manufacturer();
  *tpm_model = version_info.tpm_model();
  *firmware_version = version_info.firmware_version();
  *vendor_specific = version_info.vendor_specific();
  return true;
}

bool TpmManagerUtility::RemoveOwnerDependency(const std::string& dependency) {
  tpm_manager::RemoveOwnerDependencyReply reply;
  tpm_manager::RemoveOwnerDependencyRequest request;
  request.set_owner_dependency(dependency);
  SendTpmManagerRequestAndWait(
      base::Bind(&tpm_manager::TpmOwnershipInterface::RemoveOwnerDependency,
                 base::Unretained(tpm_owner_), request),
      &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to remove the dependency of "
               << dependency << ".";
    return false;
  }
  return true;
}

bool TpmManagerUtility::ClearStoredOwnerPassword() {
  tpm_manager::ClearStoredOwnerPasswordReply reply;
  tpm_manager::ClearStoredOwnerPasswordRequest request;
  SendTpmManagerRequestAndWait(
      base::Bind(&tpm_manager::TpmOwnershipInterface::ClearStoredOwnerPassword,
                 base::Unretained(tpm_owner_), request),
      &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to clear owner password. ";
    return false;
  }
  return true;
}

void TpmManagerUtility::InitializationTask(base::WaitableEvent* completion) {
  CHECK(completion);
  CHECK(tpm_manager_thread_.task_runner()->BelongsToCurrentThread());

  default_tpm_owner_ = std::make_unique<tpm_manager::TpmOwnershipDBusProxy>();
  default_tpm_nvram_ = std::make_unique<tpm_manager::TpmNvramDBusProxy>();
  if (default_tpm_owner_->Initialize()) {
    default_tpm_owner_->ConnectToSignal(this);
    tpm_owner_ = default_tpm_owner_.get();
  }
  if (default_tpm_nvram_->Initialize()) {
    tpm_nvram_ = default_tpm_nvram_.get();
  }
  completion->Signal();
}

template <typename ReplyProtoType, typename MethodType>
void TpmManagerUtility::SendTpmManagerRequestAndWait(
    const MethodType& method, ReplyProtoType* reply_proto) {
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

void TpmManagerUtility::ShutdownTask() {
  tpm_owner_ = nullptr;
  tpm_nvram_ = nullptr;
  default_tpm_owner_.reset(nullptr);
  default_tpm_nvram_.reset(nullptr);
}

bool TpmManagerUtility::GetDictionaryAttackInfo(int* counter,
                                                int* threshold,
                                                bool* lockout,
                                                int* seconds_remaining) {
  tpm_manager::GetDictionaryAttackInfoReply reply;
  tpm_manager::GetDictionaryAttackInfoRequest request;
  SendTpmManagerRequestAndWait(
      base::Bind(&tpm_manager::TpmOwnershipInterface::GetDictionaryAttackInfo,
                 base::Unretained(tpm_owner_), request),
      &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__
               << ": Failed to retreive the dictionary attack information.";
    return false;
  }

  *counter = reply.dictionary_attack_counter();
  *threshold = reply.dictionary_attack_threshold();
  *lockout = reply.dictionary_attack_lockout_in_effect();
  *seconds_remaining = reply.dictionary_attack_lockout_seconds_remaining();

  return true;
}

bool TpmManagerUtility::ResetDictionaryAttackLock() {
  tpm_manager::ResetDictionaryAttackLockReply reply;
  tpm_manager::ResetDictionaryAttackLockRequest request;
  SendTpmManagerRequestAndWait(
      base::Bind(&tpm_manager::TpmOwnershipInterface::ResetDictionaryAttackLock,
                 base::Unretained(tpm_owner_), request),
      &reply);
  if (reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to reset DA lock.";
    return false;
  }
  return true;
}

bool TpmManagerUtility::ReadSpace(uint32_t index,
                                  bool use_owner_auth,
                                  std::string* output) {
  tpm_manager::ReadSpaceRequest request;
  request.set_index(index);
  request.set_use_owner_authorization(use_owner_auth);
  tpm_manager::ReadSpaceReply response;
  SendTpmManagerRequestAndWait(
      base::Bind(&tpm_manager::TpmNvramInterface::ReadSpace,
                 base::Unretained(tpm_nvram_), request),
      &response);
  if (response.result() == tpm_manager::NVRAM_RESULT_SPACE_DOES_NOT_EXIST) {
    LOG(ERROR) << __func__ << ": NV Index [" << index << "] does not exist.";
    return false;
  }
  if (response.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << __func__ << ": Failed to read NV index [" << index << "]"
               << response.result();
    return false;
  }
  *output = response.data();
  return true;
}

bool TpmManagerUtility::GetOwnershipTakenSignalStatus(bool* is_successful,
                                                      bool* has_received,
                                                      LocalData* local_data) {
  base::AutoLock lock(ownership_signal_lock_);
  if (!is_connected_) {
    return false;
  }
  if (is_successful) {
    *is_successful = is_connection_successful_;
  }
  if (has_received) {
    *has_received = static_cast<bool>(ownership_taken_signal_);
  }
  // Copies |LocalData| when both the data source and destination is ready.
  if (ownership_taken_signal_ && local_data) {
    *local_data = ownership_taken_signal_->local_data();
  }
  return true;
}

void TpmManagerUtility::AddOwnershipCallback(
    OwnershipCallback ownership_callback) {
  base::AutoLock lock(ownership_callback_lock_);
  ownership_callbacks_.push_back(ownership_callback);
}

void TpmManagerUtility::OnOwnershipTaken(const OwnershipTakenSignal& signal) {
  LOG(INFO) << __func__ << ": Received |OwnershipTakenSignal|.";
  {
    base::AutoLock lock(ownership_signal_lock_);
    ownership_taken_signal_ = signal;
  }
  base::AutoLock lock(ownership_callback_lock_);
  for (auto& callback : ownership_callbacks_) {
    callback.Run();
  }
}

void TpmManagerUtility::OnSignalConnected(const std::string& /*interface_name*/,
                                          const std::string& /*signal_name*/,
                                          bool is_successful) {
  if (!is_successful) {
    LOG(ERROR) << __func__ << ": Failed to connect dbus signal.";
  } else {
    LOG(INFO) << __func__ << ": Connected dbus signal successfully.";
  }
  base::AutoLock lock(ownership_signal_lock_);
  is_connected_ = true;
  is_connection_successful_ = is_successful;
}

TpmManagerUtility* TpmManagerUtility::GetSingleton() {
  base::AutoLock lock(singleton_lock);
  if (!singleton) {
    singleton = new TpmManagerUtility();
    if (!singleton->Initialize()) {
      delete singleton;
      singleton = nullptr;
    }
  }
  return singleton;
}

}  // namespace tpm_manager
