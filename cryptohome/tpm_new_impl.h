// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_NEW_IMPL_H_
#define CRYPTOHOME_TPM_NEW_IMPL_H_

#include <memory>

#include <base/optional.h>
#include <tpm_manager/client/tpm_manager_utility.h>

#include "cryptohome/tpm_impl.h"

// This class should be squashed into |TpmImpl| now that the transition from
// monilithic mode to distributed mode is done.
//
// TODO(b/169388941): Remove this class after merge it back.

namespace cryptohome {

// |TpmNewImpl| is derived from |TpmImpl| and refines a set of interfaces with
// the data coming from |tpm_managerd|. In particular, the logic which should
// belong to only |tpm_managerd| (e.g. the ownership operation, owner password,
// etc.) are overwritted in this class and the corresponding setters should take
// no effect. Once |ServiceMonolithic| is obsoleted, the implementation of this
// class should be backported to |TpmImpl| and this class should be removed at
// the same time.
class TpmNewImpl : public TpmImpl {
 public:
  TpmNewImpl() = default;
  virtual ~TpmNewImpl() = default;
  bool GetOwnerPassword(brillo::SecureBlob* owner_password) override;
  void SetOwnerPassword(const brillo::SecureBlob& owner_password) override;
  bool IsEnabled() override;
  void SetIsEnabled(bool enabled) override;
  bool IsOwned() override;
  void SetIsOwned(bool owned) override;
  bool TakeOwnership(int max_timeout_tries,
                     const brillo::SecureBlob& owner_password) override;
  bool GetDelegate(brillo::Blob* blob,
                   brillo::Blob* secret,
                   bool* has_reset_lock_permissions) override;
  bool DoesUseTpmManager() override;
  bool GetDictionaryAttackInfo(int* counter,
                               int* threshold,
                               bool* lockout,
                               int* seconds_remaining) override;
  bool ResetDictionaryAttackMitigation(
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret) override;
  bool RemoveOwnerDependency(
      TpmPersistentState::TpmOwnerDependency dependency) override;
  bool ClearStoredPassword() override;
  bool GetVersionInfo(TpmVersionInfo* version_info) override;
  base::Optional<bool> IsDelegateBoundToPcr() override;
  bool DelegateCanResetDACounter() override;

 private:
  // Initializes |tpm_manager_utility_|; returns |true| iff successful.
  bool InitializeTpmManagerUtility();

  // Calls |TpmManagerUtility::GetTpmStatus| and stores the result into
  // |is_enabled_|, |is_owned_|, and |last_tpm_manager_data_| for later use.
  bool CacheTpmManagerStatus();

  // Attempts to get |tpm_manager::LocalData| from signal or by explicitly
  // querying it. Returns |true| iff either approach succeeds. Behind the scene,
  // the function attempts to update the local data when it's available from
  // ownership taken signal. Otherwise, for any reason why we don't have it from
  // ownership taken signal, it actively query tpm status by a dbus request.
  // This intuitive way can be seen an  overkill sometimes(e.g. The signal is
  // waiting to be connected); however this conservative approach can avoid the
  // data loss due to some potential issues (e.g. unexpectedly long waiting time
  // until the signal is confirmed to be connected).
  bool UpdateLocalDataFromTpmManager();

  // Gets delegate from tpm manager and call feed the value to |SetDelegate|.
  bool SetDelegateDataFromTpmManager();

  //  wrapped tpm_manager proxy to get information from |tpm_manager|.
  tpm_manager::TpmManagerUtility* tpm_manager_utility_{nullptr};

  // Gives |TpmNewImpl| a new set of members of status from tpm manager so we
  // can touch the already working code as little as possible. Otherwise need to
  // move data members in |TpmImpl| to |protected| field.
  bool is_enabled_{false};
  bool is_owned_{false};

  // Indicates if the delegate has been set and the parent class |TpmImpl|
  // already has the information about the owner delegate we have from tpm
  // manager.
  bool has_set_delegate_data_ = false;

  // This flag indicates |CacheTpmManagerStatus| shall be called when the
  // ownership taken signal is confirmed to be connected.
  bool shall_cache_tpm_manager_status_{true};

  // Records |LocalData| from tpm_manager last time we query, either by
  // explicitly requesting the update or from dbus signal.
  tpm_manager::LocalData last_tpm_manager_data_;

  // Cache of TPM version info, base::nullopt if cache doesn't exist.
  base::Optional<TpmVersionInfo> version_info_;

  // The following fields are for testing purpose.
  friend class TpmNewImplTest;
  explicit TpmNewImpl(tpm_manager::TpmManagerUtility* tpm_manager_utility);

  DISALLOW_COPY_AND_ASSIGN(TpmNewImpl);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_NEW_IMPL_H_
