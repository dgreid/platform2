// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_manager_service.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/command_line.h>
#include <base/message_loop/message_pump_type.h>
#include <base/synchronization/lock.h>

namespace {

constexpr int kDictionaryAttackResetPeriodInHours = 1;

#if USE_TPM2
// Timeout waiting for Trunks daemon readiness.
constexpr base::TimeDelta kTrunksDaemonTimeout =
    base::TimeDelta::FromSeconds(30);
// Delay between subsequent attempts to initialize connection to Trunks daemon.
constexpr base::TimeDelta kTrunksDaemonInitAttemptDelay =
    base::TimeDelta::FromMicroseconds(300);
#endif

// Clears owner password in |local_data| if all dependencies have been removed
// and it has not yet been cleared.
// Returns true if |local_data| has been modified, false otherwise.
bool ClearOwnerPasswordIfPossible(tpm_manager::LocalData* local_data) {
  if (local_data->has_owner_password() &&
      local_data->owner_dependency().empty()) {
    local_data->clear_owner_password();
    return true;
  }
  return false;
}

}  // namespace

namespace tpm_manager {

TpmManagerService::TpmManagerService(bool wait_for_ownership,
                                     bool perform_preinit,
                                     LocalDataStore* local_data_store)
    : TpmManagerService(wait_for_ownership,
                        perform_preinit,
                        local_data_store,
                        nullptr,
                        nullptr,
                        nullptr,
                        &default_tpm_manager_metrics_) {
  CHECK(local_data_store_);
}

TpmManagerService::TpmManagerService(bool wait_for_ownership,
                                     bool perform_preinit,
                                     LocalDataStore* local_data_store,
                                     TpmStatus* tpm_status,
                                     TpmInitializer* tpm_initializer,
                                     TpmNvram* tpm_nvram,
                                     TpmManagerMetrics* tpm_manager_metrics)
    : dictionary_attack_timer_(
          base::TimeDelta::FromHours(kDictionaryAttackResetPeriodInHours)),
      local_data_store_(local_data_store),
      tpm_status_(tpm_status),
      tpm_initializer_(tpm_initializer),
      tpm_nvram_(tpm_nvram),
      tpm_manager_metrics_(tpm_manager_metrics),
      wait_for_ownership_(wait_for_ownership),
      perform_preinit_(perform_preinit) {}

TpmManagerService::~TpmManagerService() {
  worker_thread_->Stop();
}

bool TpmManagerService::Initialize() {
  worker_thread_.reset(
      new ServiceWorkerThread("TpmManager Service Worker", this));
  worker_thread_->StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  base::Closure task =
      base::Bind(&TpmManagerService::InitializeTask, base::Unretained(this));
  worker_thread_->task_runner()->PostNonNestableTask(FROM_HERE, task);
  VLOG(1) << "Worker thread started.";
  return true;
}

void TpmManagerService::InitializeTask() {
  VLOG(1) << "Initializing service...";

  if (!tpm_status_ || !tpm_initializer_ || !tpm_nvram_) {
    // Setup default objects.
#if USE_TPM2
    default_trunks_factory_ = std::make_unique<trunks::TrunksFactoryImpl>();
    // Tolerate some delay in trunksd being up and ready.
    base::TimeTicks deadline = base::TimeTicks::Now() + kTrunksDaemonTimeout;
    while (!default_trunks_factory_->Initialize() &&
           base::TimeTicks::Now() < deadline) {
      base::PlatformThread::Sleep(kTrunksDaemonInitAttemptDelay);
    }
    default_tpm_status_ = std::make_unique<Tpm2StatusImpl>(
        *default_trunks_factory_, ownership_taken_callback_);
    tpm_status_ = default_tpm_status_.get();
    default_tpm_initializer_ = std::make_unique<Tpm2InitializerImpl>(
        *default_trunks_factory_, local_data_store_, tpm_status_,
        ownership_taken_callback_);
    tpm_initializer_ = default_tpm_initializer_.get();
    default_tpm_nvram_ = std::make_unique<Tpm2NvramImpl>(
        *default_trunks_factory_, local_data_store_, tpm_status_);
    tpm_nvram_ = default_tpm_nvram_.get();
#else
    default_tpm_status_ =
        std::make_unique<TpmStatusImpl>(ownership_taken_callback_);
    tpm_status_ = default_tpm_status_.get();
    default_tpm_initializer_ = std::make_unique<TpmInitializerImpl>(
        local_data_store_, tpm_status_, ownership_taken_callback_);
    tpm_initializer_ = default_tpm_initializer_.get();
    default_tpm_nvram_ = std::make_unique<TpmNvramImpl>(local_data_store_);
    tpm_nvram_ = default_tpm_nvram_.get();
#endif
  }
  if (!tpm_status_->IsTpmEnabled()) {
    LOG(WARNING) << __func__ << ": TPM is disabled.";
    return;
  }
  tpm_initializer_->VerifiedBootHelper();

  // CheckAndNotifyIfTpmOwned() sends a signal if the TPM is already owned at
  // boot time and needs to be called no matter what value wait_for_ownership_
  // is.
  TpmStatus::TpmOwnershipStatus ownership_status;
  if (!tpm_status_->CheckAndNotifyIfTpmOwned(&ownership_status)) {
    LOG(ERROR) << __func__
               << ": failed to get tpm ownership status, maybe it's the "
                  "dictionary attack lockout.";
    // GetStatus could fail because the TPM is under DA lockout, so we'll try to
    // reset lockout then try again.
    ResetDictionaryAttackCounterIfNeeded();
    if (!tpm_status_->CheckAndNotifyIfTpmOwned(&ownership_status)) {
      LOG(ERROR) << __func__
                 << ": get tpm ownership status still failed. Giving up.";
      return;
    }
    LOG(INFO) << __func__
              << ": get tpm ownership status suceeded after dictionary attack "
                 "lockout reset.";
  }
  // The precondition of DA reset is not satisfied; resets the timer so it
  // doesn't get triggered immediately.
  if (ownership_status != TpmStatus::kTpmOwned && wait_for_ownership_) {
    dictionary_attack_timer_.Reset();
  }
  worker_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&TpmManagerService::PeriodicResetDictionaryAttackCounterTask,
                 base::Unretained(this)));

  if (ownership_status == TpmStatus::kTpmOwned) {
    VLOG(1) << "Tpm is already owned.";
    if (!tpm_initializer_->EnsurePersistentOwnerDelegate()) {
      // Only treat the failure as a warning because the daemon can be partly
      // operational still.
      LOG(WARNING)
          << __func__
          << ": Failed to ensure owner delegate is ready with ownership taken.";
    }
    return;
  }

  // TPM is not fully owned yet. There might be stale data in the local data
  // store. Checks and removes them if needed.
  tpm_initializer_->PruneStoredPasswords();
  tpm_nvram_->PrunePolicies();

  if (!wait_for_ownership_) {
    VLOG(1) << "Initializing TPM.";
    if (!tpm_initializer_->InitializeTpm()) {
      LOG(WARNING) << __func__ << ": TPM initialization failed.";
      dictionary_attack_timer_.Reset();
      return;
    }
  } else if (perform_preinit_) {
    VLOG(1) << "Pre-initializing TPM.";
    tpm_initializer_->PreInitializeTpm();
  }
}

void TpmManagerService::GetTpmStatus(const GetTpmStatusRequest& request,
                                     const GetTpmStatusCallback& callback) {
  PostTaskToWorkerThread<GetTpmStatusReply>(
      request, callback, &TpmManagerService::GetTpmStatusTask);
}

void TpmManagerService::GetTpmStatusTask(
    const GetTpmStatusRequest& request,
    const std::shared_ptr<GetTpmStatusReply>& reply) {
  VLOG(1) << __func__;

  if (!tpm_status_) {
    LOG(ERROR) << __func__ << ": tpm status is uninitialized.";
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }

  reply->set_enabled(tpm_status_->IsTpmEnabled());

  TpmStatus::TpmOwnershipStatus ownership_status;
  if (!tpm_status_->CheckAndNotifyIfTpmOwned(&ownership_status)) {
    LOG(ERROR) << __func__ << ": failed to get tpm ownership status";
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  reply->set_owned(TpmStatus::kTpmOwned == ownership_status);

  LocalData local_data;
  if (local_data_store_ && local_data_store_->Read(&local_data)) {
    *reply->mutable_local_data() = local_data;
  }

  reply->set_status(STATUS_SUCCESS);
}

void TpmManagerService::GetVersionInfo(const GetVersionInfoRequest& request,
                                       const GetVersionInfoCallback& callback) {
  {
    base::AutoLock lock(version_info_cache_lock_);
    if (version_info_cache_) {
      callback.Run(*version_info_cache_);
      return;
    }
  }

  PostTaskToWorkerThread<GetVersionInfoReply>(
      request, callback, &TpmManagerService::GetVersionInfoTask);
}

void TpmManagerService::GetVersionInfoTask(
    const GetVersionInfoRequest& request,
    const std::shared_ptr<GetVersionInfoReply>& reply) {
  VLOG(1) << __func__;

  // It's possible that cache was not available when the request came to the
  // main thread but became available when the task is being processed here.
  // Checks the cache again to save one TPM call.
  if (version_info_cache_) {
    *reply = *version_info_cache_;
    return;
  }

  if (!tpm_status_) {
    LOG(ERROR) << __func__ << ": tpm status is uninitialized.";
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }

  uint32_t family;
  uint64_t spec_level;
  uint32_t manufacturer;
  uint32_t tpm_model;
  uint64_t firmware_version;
  std::vector<uint8_t> vendor_specific;
  if (!tpm_status_->GetVersionInfo(&family, &spec_level, &manufacturer,
                                   &tpm_model, &firmware_version,
                                   &vendor_specific)) {
    LOG(ERROR) << __func__ << ": failed to get version info from tpm status.";
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }

  reply->set_family(family);
  reply->set_spec_level(spec_level);
  reply->set_manufacturer(manufacturer);
  reply->set_tpm_model(tpm_model);
  reply->set_firmware_version(firmware_version);
  reply->set_vendor_specific(reinterpret_cast<char*>(vendor_specific.data()),
                             vendor_specific.size());
  reply->set_status(STATUS_SUCCESS);

  {
    base::AutoLock lock(version_info_cache_lock_);
    version_info_cache_ = *reply;
  }
}

void TpmManagerService::GetDictionaryAttackInfo(
    const GetDictionaryAttackInfoRequest& request,
    const GetDictionaryAttackInfoCallback& callback) {
  PostTaskToWorkerThread<GetDictionaryAttackInfoReply>(
      request, callback, &TpmManagerService::GetDictionaryAttackInfoTask);
}

void TpmManagerService::GetDictionaryAttackInfoTask(
    const GetDictionaryAttackInfoRequest& request,
    const std::shared_ptr<GetDictionaryAttackInfoReply>& reply) {
  VLOG(1) << __func__;

  if (!tpm_status_) {
    LOG(ERROR) << __func__ << ": tpm status is uninitialized.";
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }

  uint32_t counter;
  uint32_t threshold;
  bool lockout;
  uint32_t lockout_time_remaining;
  if (!tpm_status_->GetDictionaryAttackInfo(&counter, &threshold, &lockout,
                                            &lockout_time_remaining)) {
    LOG(ERROR) << __func__ << ": failed to get DA info";
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }

  reply->set_dictionary_attack_counter(counter);
  reply->set_dictionary_attack_threshold(threshold);
  reply->set_dictionary_attack_lockout_in_effect(lockout);
  reply->set_dictionary_attack_lockout_seconds_remaining(
      lockout_time_remaining);
  reply->set_status(STATUS_SUCCESS);
}

void TpmManagerService::ResetDictionaryAttackLock(
    const ResetDictionaryAttackLockRequest& request,
    const ResetDictionaryAttackLockCallback& callback) {
  PostTaskToWorkerThread<ResetDictionaryAttackLockReply>(
      request, callback, &TpmManagerService::ResetDictionaryAttackLockTask);
}

void TpmManagerService::ResetDictionaryAttackLockTask(
    const ResetDictionaryAttackLockRequest& request,
    const std::shared_ptr<ResetDictionaryAttackLockReply>& reply) {
  VLOG(1) << __func__;

  if (!tpm_initializer_) {
    LOG(ERROR) << __func__ << ": request received before tpm manager service "
               << "is initialized.";
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }
  if (!ResetDictionaryAttackCounterIfNeeded()) {
    LOG(ERROR) << __func__ << ": failed to reset DA lock.";
    reply->set_status(STATUS_DEVICE_ERROR);
  } else {
    reply->set_status(STATUS_SUCCESS);
  }
  dictionary_attack_timer_.Reset();
}

void TpmManagerService::TakeOwnership(const TakeOwnershipRequest& request,
                                      const TakeOwnershipCallback& callback) {
  PostTaskToWorkerThread<TakeOwnershipReply>(
      request, callback, &TpmManagerService::TakeOwnershipTask);
}

void TpmManagerService::TakeOwnershipTask(
    const TakeOwnershipRequest& request,
    const std::shared_ptr<TakeOwnershipReply>& reply) {
  VLOG(1) << __func__;
  if (!tpm_status_->IsTpmEnabled()) {
    reply->set_status(STATUS_NOT_AVAILABLE);
    return;
  }
  if (!tpm_initializer_->InitializeTpm()) {
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  if (!ResetDictionaryAttackCounterIfNeeded()) {
    LOG(WARNING) << __func__ << ": DA reset failed after taking ownership.";
  }
  dictionary_attack_timer_.Reset();
  reply->set_status(STATUS_SUCCESS);
}

void TpmManagerService::RemoveOwnerDependency(
    const RemoveOwnerDependencyRequest& request,
    const RemoveOwnerDependencyCallback& callback) {
  PostTaskToWorkerThread<RemoveOwnerDependencyReply>(
      request, callback, &TpmManagerService::RemoveOwnerDependencyTask);
}

void TpmManagerService::RemoveOwnerDependencyTask(
    const RemoveOwnerDependencyRequest& request,
    const std::shared_ptr<RemoveOwnerDependencyReply>& reply) {
  VLOG(1) << __func__;
  LocalData local_data;
  if (!local_data_store_->Read(&local_data)) {
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  RemoveOwnerDependencyFromLocalData(request.owner_dependency(), &local_data);
  if (auto_clear_stored_owner_password_) {
    ClearOwnerPasswordIfPossible(&local_data);
  }
  if (!local_data_store_->Write(local_data)) {
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  reply->set_status(STATUS_SUCCESS);
}

void TpmManagerService::RemoveOwnerDependencyFromLocalData(
    const std::string& owner_dependency, LocalData* local_data) {
  google::protobuf::RepeatedPtrField<std::string>* dependencies =
      local_data->mutable_owner_dependency();
  for (int i = 0; i < dependencies->size(); i++) {
    if (dependencies->Get(i) == owner_dependency) {
      dependencies->SwapElements(i, (dependencies->size() - 1));
      dependencies->RemoveLast();
      break;
    }
  }
}

void TpmManagerService::ClearStoredOwnerPassword(
    const ClearStoredOwnerPasswordRequest& request,
    const ClearStoredOwnerPasswordCallback& callback) {
  PostTaskToWorkerThread<ClearStoredOwnerPasswordReply>(
      request, callback, &TpmManagerService::ClearStoredOwnerPasswordTask);
}

void TpmManagerService::ClearStoredOwnerPasswordTask(
    const ClearStoredOwnerPasswordRequest& request,
    const std::shared_ptr<ClearStoredOwnerPasswordReply>& reply) {
  VLOG(1) << __func__;
  LocalData local_data;
  if (!local_data_store_->Read(&local_data)) {
    reply->set_status(STATUS_DEVICE_ERROR);
    return;
  }
  if (ClearOwnerPasswordIfPossible(&local_data)) {
    if (!local_data_store_->Write(local_data)) {
      reply->set_status(STATUS_DEVICE_ERROR);
      return;
    }
  }
  reply->set_status(STATUS_SUCCESS);
}

void TpmManagerService::DefineSpace(const DefineSpaceRequest& request,
                                    const DefineSpaceCallback& callback) {
  PostTaskToWorkerThread<DefineSpaceReply>(request, callback,
                                           &TpmManagerService::DefineSpaceTask);
}

void TpmManagerService::DefineSpaceTask(
    const DefineSpaceRequest& request,
    const std::shared_ptr<DefineSpaceReply>& reply) {
  VLOG(1) << __func__;
  std::vector<NvramSpaceAttribute> attributes;
  for (int i = 0; i < request.attributes_size(); ++i) {
    attributes.push_back(request.attributes(i));
  }
  reply->set_result(
      tpm_nvram_->DefineSpace(request.index(), request.size(), attributes,
                              request.authorization_value(), request.policy()));
}

void TpmManagerService::DestroySpace(const DestroySpaceRequest& request,
                                     const DestroySpaceCallback& callback) {
  PostTaskToWorkerThread<DestroySpaceReply>(
      request, callback, &TpmManagerService::DestroySpaceTask);
}

void TpmManagerService::DestroySpaceTask(
    const DestroySpaceRequest& request,
    const std::shared_ptr<DestroySpaceReply>& reply) {
  VLOG(1) << __func__;
  reply->set_result(tpm_nvram_->DestroySpace(request.index()));
}

void TpmManagerService::WriteSpace(const WriteSpaceRequest& request,
                                   const WriteSpaceCallback& callback) {
  PostTaskToWorkerThread<WriteSpaceReply>(request, callback,
                                          &TpmManagerService::WriteSpaceTask);
}

void TpmManagerService::WriteSpaceTask(
    const WriteSpaceRequest& request,
    const std::shared_ptr<WriteSpaceReply>& reply) {
  VLOG(1) << __func__;
  std::string authorization_value = request.authorization_value();
  if (request.use_owner_authorization()) {
    authorization_value = GetOwnerPassword();
    if (authorization_value.empty()) {
      reply->set_result(NVRAM_RESULT_ACCESS_DENIED);
      return;
    }
  }
  reply->set_result(tpm_nvram_->WriteSpace(request.index(), request.data(),
                                           authorization_value));
}

void TpmManagerService::ReadSpace(const ReadSpaceRequest& request,
                                  const ReadSpaceCallback& callback) {
  PostTaskToWorkerThread<ReadSpaceReply>(request, callback,
                                         &TpmManagerService::ReadSpaceTask);
}

void TpmManagerService::ReadSpaceTask(
    const ReadSpaceRequest& request,
    const std::shared_ptr<ReadSpaceReply>& reply) {
  VLOG(1) << __func__;
  std::string authorization_value = request.authorization_value();
  if (request.use_owner_authorization()) {
    authorization_value = GetOwnerPassword();
    if (authorization_value.empty()) {
      reply->set_result(NVRAM_RESULT_ACCESS_DENIED);
      return;
    }
  }
  reply->set_result(tpm_nvram_->ReadSpace(
      request.index(), reply->mutable_data(), authorization_value));
}

void TpmManagerService::LockSpace(const LockSpaceRequest& request,
                                  const LockSpaceCallback& callback) {
  PostTaskToWorkerThread<LockSpaceReply>(request, callback,
                                         &TpmManagerService::LockSpaceTask);
}

void TpmManagerService::LockSpaceTask(
    const LockSpaceRequest& request,
    const std::shared_ptr<LockSpaceReply>& reply) {
  VLOG(1) << __func__;
  std::string authorization_value = request.authorization_value();
  if (request.use_owner_authorization()) {
    authorization_value = GetOwnerPassword();
    if (authorization_value.empty()) {
      reply->set_result(NVRAM_RESULT_ACCESS_DENIED);
      return;
    }
  }
  reply->set_result(tpm_nvram_->LockSpace(request.index(), request.lock_read(),
                                          request.lock_write(),
                                          authorization_value));
}

void TpmManagerService::ListSpaces(const ListSpacesRequest& request,
                                   const ListSpacesCallback& callback) {
  PostTaskToWorkerThread<ListSpacesReply>(request, callback,
                                          &TpmManagerService::ListSpacesTask);
}

void TpmManagerService::ListSpacesTask(
    const ListSpacesRequest& request,
    const std::shared_ptr<ListSpacesReply>& reply) {
  VLOG(1) << __func__;
  std::vector<uint32_t> index_list;
  reply->set_result(tpm_nvram_->ListSpaces(&index_list));
  if (reply->result() == NVRAM_RESULT_SUCCESS) {
    for (auto index : index_list) {
      reply->add_index_list(index);
    }
  }
}

void TpmManagerService::GetSpaceInfo(const GetSpaceInfoRequest& request,
                                     const GetSpaceInfoCallback& callback) {
  PostTaskToWorkerThread<GetSpaceInfoReply>(
      request, callback, &TpmManagerService::GetSpaceInfoTask);
}

void TpmManagerService::GetSpaceInfoTask(
    const GetSpaceInfoRequest& request,
    const std::shared_ptr<GetSpaceInfoReply>& reply) {
  VLOG(1) << __func__;
  std::vector<NvramSpaceAttribute> attributes;
  uint32_t size = 0;
  bool is_read_locked = false;
  bool is_write_locked = false;
  NvramSpacePolicy policy = NVRAM_POLICY_NONE;
  reply->set_result(tpm_nvram_->GetSpaceInfo(request.index(), &size,
                                             &is_read_locked, &is_write_locked,
                                             &attributes, &policy));
  if (reply->result() == NVRAM_RESULT_SUCCESS) {
    reply->set_size(size);
    reply->set_is_read_locked(is_read_locked);
    reply->set_is_write_locked(is_write_locked);
    for (auto attribute : attributes) {
      reply->add_attributes(attribute);
    }
    reply->set_policy(policy);
  }
}

std::string TpmManagerService::GetOwnerPassword() {
  LocalData local_data;
  if (local_data_store_->Read(&local_data)) {
    return local_data.owner_password();
  }
  LOG(ERROR) << "TPM owner password requested but not available.";
  return std::string();
}

bool TpmManagerService::ResetDictionaryAttackCounterIfNeeded() {
  uint32_t counter = 0;
  uint32_t threshold = 0;
  bool lockout = false;
  uint32_t time_remaining = 0;
  if (!tpm_status_->GetDictionaryAttackInfo(&counter, &threshold, &lockout,
                                            &time_remaining)) {
    // Reports the metrics but no early return since reset itself might work.
    tpm_manager_metrics_->ReportDictionaryAttackResetStatus(
        DictionaryAttackResetStatus::kCounterQueryFailed);
  } else {
    tpm_manager_metrics_->ReportDictionaryAttackCounter(counter);
    if (counter == 0) {
      tpm_manager_metrics_->ReportDictionaryAttackResetStatus(
          DictionaryAttackResetStatus::kResetNotNecessary);
      return true;
    }
  }
  auto status = tpm_initializer_->ResetDictionaryAttackLock();
  tpm_manager_metrics_->ReportDictionaryAttackResetStatus(status);
  return status == DictionaryAttackResetStatus::kResetAttemptSucceeded;
}

void TpmManagerService::PeriodicResetDictionaryAttackCounterTask() {
  VLOG(1) << __func__;
  base::TimeDelta time_remaining = dictionary_attack_timer_.TimeRemaining();
  // if the timer is up, run the task and reset the timer.
  if (time_remaining.is_zero()) {
    if (!ResetDictionaryAttackCounterIfNeeded()) {
      LOG(WARNING) << __func__ << ": DA reset failed.";
    } else {
      LOG(INFO) << __func__ << ": DA reset succeeded.";
    }
    dictionary_attack_timer_.Reset();
    time_remaining = dictionary_attack_timer_.TimeRemaining();
  } else {
    LOG(INFO) << __func__ << ": Time is not up yet.";
  }
  worker_thread_->task_runner()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&TpmManagerService::PeriodicResetDictionaryAttackCounterTask,
                 base::Unretained(this)),
      time_remaining);
}

void TpmManagerService::ShutdownTask() {
  default_tpm_status_.reset();
  default_tpm_initializer_.reset();
  default_tpm_nvram_.reset();
#if USE_TPM2
  // Resets |default_trunks_factory_| last because other components hold its
  // reference.
  default_trunks_factory_.reset();
#endif
}

template <typename ReplyProtobufType>
void TpmManagerService::TaskRelayCallback(
    const base::Callback<void(const ReplyProtobufType&)> callback,
    const std::shared_ptr<ReplyProtobufType>& reply) {
  callback.Run(*reply);
}

template <typename ReplyProtobufType,
          typename RequestProtobufType,
          typename ReplyCallbackType,
          typename TaskType>
void TpmManagerService::PostTaskToWorkerThread(
    const RequestProtobufType& request,
    const ReplyCallbackType& callback,
    TaskType task) {
  auto result = std::make_shared<ReplyProtobufType>();
  base::Closure background_task =
      base::Bind(task, base::Unretained(this), request, result);
  base::Closure reply =
      base::Bind(&TpmManagerService::TaskRelayCallback<ReplyProtobufType>,
                 weak_factory_.GetWeakPtr(), callback, result);
  worker_thread_->task_runner()->PostTaskAndReply(FROM_HERE, background_task,
                                                  reply);
}

}  // namespace tpm_manager
