// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_TPM_MANAGER_SERVICE_H_
#define TPM_MANAGER_SERVER_TPM_MANAGER_SERVICE_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/ptr_util.h>
#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>

#include "tpm_manager/common/tpm_nvram_interface.h"
#include "tpm_manager/common/tpm_ownership_interface.h"
#include "tpm_manager/common/typedefs.h"
#include "tpm_manager/server/local_data_store.h"
#include "tpm_manager/server/passive_timer.h"
#include "tpm_manager/server/tpm_initializer.h"
#include "tpm_manager/server/tpm_manager_metrics.h"
#include "tpm_manager/server/tpm_nvram.h"
#include "tpm_manager/server/tpm_status.h"
#if USE_TPM2
#include "tpm_manager/server/tpm2_initializer_impl.h"
#include "tpm_manager/server/tpm2_nvram_impl.h"
#include "tpm_manager/server/tpm2_status_impl.h"
#include "trunks/trunks_factory.h"
#include "trunks/trunks_factory_impl.h"
#else
#include "tpm_manager/server/tpm_initializer_impl.h"
#include "tpm_manager/server/tpm_nvram_impl.h"
#include "tpm_manager/server/tpm_status_impl.h"
#endif

namespace tpm_manager {

// This class implements the core tpm_manager service. All Tpm access is
// asynchronous, except for the initial setup in Initialize().
// Usage:
//   std::unique_ptr<TpmManagerService> tpm_manager = new TpmManagerService();
//   CHECK(tpm_manager->Initialize());
//   tpm_manager->GetTpmStatus(...);
//
// THREADING NOTES:
// This class runs a worker thread and delegates all calls to it. This keeps the
// public methods non-blocking while allowing complex implementation details
// with dependencies on the TPM, network, and filesystem to be coded in a more
// readable way. It also serves to serialize method execution which reduces
// complexity with TPM state.
//
// Tasks that run on the worker thread are bound with base::Unretained which is
// safe because the thread is owned by this class (so it is guaranteed not to
// process a task after destruction). Weak pointers are used to post replies
// back to the main thread.
class TpmManagerService : public TpmNvramInterface,
                          public TpmOwnershipInterface {
 public:
  // If |wait_for_ownership| is set, TPM initialization will be postponed until
  // an explicit TakeOwnership request is received. If |perform_preinit| is
  // additionally set, TPM pre-initialization will be performed in case TPM
  // initialization is postponed.
  //
  // This instance doesn't take the ownership of |local_data_store|, and it must
  // be initialized and remain valid for the lifetime of this instance.
  explicit TpmManagerService(bool wait_for_ownership,
                             bool perform_preinit,
                             LocalDataStore* local_data_store);

  // If |wait_for_ownership| is set, TPM initialization will be postponed until
  // an explicit TakeOwnership request is received. If |perform_preinit| is
  // additionally set, TPM pre-initialization will be performed in case TPM
  // initialization is postponed.
  // Does not take ownership of |local_data_store|, |tpm_status|,
  // |tpm_initializer|, |tpm_nvram|, or |tpm_manager_metrics|.
  TpmManagerService(bool wait_for_ownership,
                    bool perform_preinit,
                    LocalDataStore* local_data_store,
                    TpmStatus* tpm_status,
                    TpmInitializer* tpm_initializer,
                    TpmNvram* tpm_nvram,
                    TpmManagerMetrics* tpm_manager_metrics);

  ~TpmManagerService() override;

  // Performs initialization tasks. This method must be called before calling
  // any other method in this class. Returns true on success.
  bool Initialize();

  void ReportVersionFingerprint();

  // TpmOwnershipInterface methods.
  void GetTpmStatus(const GetTpmStatusRequest& request,
                    const GetTpmStatusCallback& callback) override;
  void GetVersionInfo(const GetVersionInfoRequest& request,
                      const GetVersionInfoCallback& callback) override;
  void GetDictionaryAttackInfo(
      const GetDictionaryAttackInfoRequest& request,
      const GetDictionaryAttackInfoCallback& callback) override;
  void ResetDictionaryAttackLock(
      const ResetDictionaryAttackLockRequest& request,
      const ResetDictionaryAttackLockCallback& callback) override;
  void TakeOwnership(const TakeOwnershipRequest& request,
                     const TakeOwnershipCallback& callback) override;
  void RemoveOwnerDependency(
      const RemoveOwnerDependencyRequest& request,
      const RemoveOwnerDependencyCallback& callback) override;
  void ClearStoredOwnerPassword(
      const ClearStoredOwnerPasswordRequest& request,
      const ClearStoredOwnerPasswordCallback& callback) override;

  // TpmNvramInterface methods.
  void DefineSpace(const DefineSpaceRequest& request,
                   const DefineSpaceCallback& callback) override;
  void DestroySpace(const DestroySpaceRequest& request,
                    const DestroySpaceCallback& callback) override;
  void WriteSpace(const WriteSpaceRequest& request,
                  const WriteSpaceCallback& callback) override;
  void ReadSpace(const ReadSpaceRequest& request,
                 const ReadSpaceCallback& callback) override;
  void LockSpace(const LockSpaceRequest& request,
                 const LockSpaceCallback& callback) override;
  void ListSpaces(const ListSpacesRequest& request,
                  const ListSpacesCallback& callback) override;
  void GetSpaceInfo(const GetSpaceInfoRequest& request,
                    const GetSpaceInfoCallback& callback) override;

  inline void SetOwnershipTakenCallback(OwnershipTakenCallBack callback) {
    ownership_taken_callback_ = callback;
  }

  void set_dictionary_attack_reset_timer_for_testing(
      const PassiveTimer& timer) {
    dictionary_attack_timer_ = timer;
  }

 private:
  // A relay callback which allows the use of weak pointer semantics for a reply
  // to TaskRunner::PostTaskAndReply.
  template <typename ReplyProtobufType>
  void TaskRelayCallback(
      const base::Callback<void(const ReplyProtobufType&)> callback,
      const std::shared_ptr<ReplyProtobufType>& reply);

  // This templated method posts the provided |TaskType| to the background
  // thread with the provided |RequestProtobufType|. When |TaskType| finishes
  // executing, the |ReplyCallbackType| is called with the |ReplyProtobufType|.
  template <typename ReplyProtobufType,
            typename RequestProtobufType,
            typename ReplyCallbackType,
            typename TaskType>
  void PostTaskToWorkerThread(const RequestProtobufType& request,
                              const ReplyCallbackType& callback,
                              TaskType task);

  // Synchronously initializes the TPM according to the current configuration.
  // If an initialization process was interrupted it will be continued. If the
  // TPM is already initialized or cannot yet be initialized, this method has no
  // effect.
  void InitializeTask();

  // Blocking implementation of GetTpmStatus that can be executed on the
  // background worker thread.
  void GetTpmStatusTask(const GetTpmStatusRequest& request,
                        const std::shared_ptr<GetTpmStatusReply>& result);

  // Blocking implementation of GetVersionInfo that can be executed on the
  // background worker thread.
  void GetVersionInfoTask(const GetVersionInfoRequest& request,
                          const std::shared_ptr<GetVersionInfoReply>& result);

  // Blocking implementation of GetDictionaryAttackInfo that can be executed on
  // the background worker thread.
  void GetDictionaryAttackInfoTask(
      const GetDictionaryAttackInfoRequest& request,
      const std::shared_ptr<GetDictionaryAttackInfoReply>& result);

  // Blocking implementation of ResetDictionaryAttackLock that can be executed
  // on the background worker thread.
  void ResetDictionaryAttackLockTask(
      const ResetDictionaryAttackLockRequest& request,
      const std::shared_ptr<ResetDictionaryAttackLockReply>& result);

  // Blocking implementation of TakeOwnership that can be executed on the
  // background worker thread.
  void TakeOwnershipTask(const TakeOwnershipRequest& request,
                         const std::shared_ptr<TakeOwnershipReply>& result);

  // Blocking implementation of RemoveOwnerDependency that can be executed on
  // the background worker thread.
  void RemoveOwnerDependencyTask(
      const RemoveOwnerDependencyRequest& request,
      const std::shared_ptr<RemoveOwnerDependencyReply>& result);

  // Removes a |owner_dependency| from the list of owner dependencies in
  // |local_data|. If |owner_dependency| is not present in |local_data|,
  // this method does nothing.
  static void RemoveOwnerDependencyFromLocalData(
      const std::string& owner_dependency, LocalData* local_data);

  // Blocking implementation of ClearStoredOwnerPassword that can be executed
  // on the background worker thread.
  void ClearStoredOwnerPasswordTask(
      const ClearStoredOwnerPasswordRequest& request,
      const std::shared_ptr<ClearStoredOwnerPasswordReply>& result);

  // Blocking implementation of DefineSpace that can be executed on the
  // background worker thread.
  void DefineSpaceTask(const DefineSpaceRequest& request,
                       const std::shared_ptr<DefineSpaceReply>& result);

  // Blocking implementation of DestroySpace that can be executed on the
  // background worker thread.
  void DestroySpaceTask(const DestroySpaceRequest& request,
                        const std::shared_ptr<DestroySpaceReply>& result);

  // Blocking implementation of WriteSpace that can be executed on the
  // background worker thread.
  void WriteSpaceTask(const WriteSpaceRequest& request,
                      const std::shared_ptr<WriteSpaceReply>& result);

  // Blocking implementation of ReadSpace that can be executed on the
  // background worker thread.
  void ReadSpaceTask(const ReadSpaceRequest& request,
                     const std::shared_ptr<ReadSpaceReply>& result);

  // Blocking implementation of LockSpace that can be executed on the
  // background worker thread.
  void LockSpaceTask(const LockSpaceRequest& request,
                     const std::shared_ptr<LockSpaceReply>& result);

  // Blocking implementation of ListSpaces that can be executed on the
  // background worker thread.
  void ListSpacesTask(const ListSpacesRequest& request,
                      const std::shared_ptr<ListSpacesReply>& result);

  // Blocking implementation of GetSpaceInfo that can be executed on the
  // background worker thread.
  void GetSpaceInfoTask(const GetSpaceInfoRequest& request,
                        const std::shared_ptr<GetSpaceInfoReply>& result);

  // Gets the owner password from local storage. Returns an empty string if the
  // owner password is not available.
  std::string GetOwnerPassword();

  // Resets DA counter if the DA information query indicates the counter is not
  // zero; returns true iff the DA counter is confirmed to be reset or no need
  // for reset.
  bool ResetDictionaryAttackCounterIfNeeded();

  // This task performs the DA reset and posts itself with the delay determined
  // by |dictionary_attack_timer_|.
  void PeriodicResetDictionaryAttackCounterTask();
  // This timer determines if the periodic DA reset should be triggered. Upon
  // any source of DA reset this timer should be reset.
  PassiveTimer dictionary_attack_timer_;

  // Shutdown to be run on the worker thread.
  void ShutdownTask();

  LocalDataStore* local_data_store_;
  TpmStatus* tpm_status_ = nullptr;
  TpmInitializer* tpm_initializer_ = nullptr;
  TpmNvram* tpm_nvram_ = nullptr;

  TpmManagerMetrics default_tpm_manager_metrics_;
  TpmManagerMetrics* tpm_manager_metrics_{nullptr};

  // Cache of TPM version info, base::nullopt if cache doesn't exist.
  base::Optional<GetVersionInfoReply> version_info_cache_;

  // Lock for |version_info_cache_|, which might be accessed from both the main
  // and worker threads.
  base::Lock version_info_cache_lock_;

  // base::Thread subclass so we can implement CleanUp.
  class ServiceWorkerThread : public base::Thread {
   public:
    explicit ServiceWorkerThread(const std::string& name,
                                 TpmManagerService* service)
        : base::Thread(name), service_(service) {
      DCHECK(service_);
    }
    ~ServiceWorkerThread() override { Stop(); }

   private:
    void CleanUp() override { service_->ShutdownTask(); }

    TpmManagerService* const service_;

    DISALLOW_COPY_AND_ASSIGN(ServiceWorkerThread);
  };

#if USE_TPM2
  std::unique_ptr<trunks::TrunksFactoryImpl> default_trunks_factory_;
  std::unique_ptr<Tpm2StatusImpl> default_tpm_status_;
  std::unique_ptr<Tpm2InitializerImpl> default_tpm_initializer_;
  std::unique_ptr<Tpm2NvramImpl> default_tpm_nvram_;
#else
  std::unique_ptr<TpmStatusImpl> default_tpm_status_;
  std::unique_ptr<TpmInitializerImpl> default_tpm_initializer_;
  std::unique_ptr<TpmNvramImpl> default_tpm_nvram_;
#endif

  // Whether to clear the stored owner password automatically upon removing all
  // dependencies.
  bool auto_clear_stored_owner_password_ = false;
  // Whether to wait for an explicit call to 'TakeOwnership' before initializing
  // the TPM. Normally tracks the --wait_for_ownership command line option.
  bool wait_for_ownership_;
  // Whether to perform pre-initialization (where available) if initialization
  // itself needs to wait for 'TakeOwnership' first.
  bool perform_preinit_;
  // Background thread to allow processing of potentially lengthy TPM requests
  // in the background.
  std::unique_ptr<ServiceWorkerThread> worker_thread_;
  // Declared last so any weak pointers are destroyed first.
  base::WeakPtrFactory<TpmManagerService> weak_factory_{this};

  // Function that's called after TPM ownership is taken by tpm_initializer_.
  // It's value should be set by SetOwnershipTakenCallback() before being used.
  OwnershipTakenCallBack ownership_taken_callback_;

  DISALLOW_COPY_AND_ASSIGN(TpmManagerService);
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_TPM_MANAGER_SERVICE_H_
