// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TpmInit - public interface class for initializing the TPM
#ifndef CRYPTOHOME_TPM_INIT_H_
#define CRYPTOHOME_TPM_INIT_H_

#include <memory>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest_prod.h>

#include "cryptohome/tpm.h"
#include "cryptohome/tpm_persistent_state.h"

namespace cryptohome {

class Platform;
class TpmInitTask;

class TpmInit {
  // Friend class TpmInitTask as it is a glue class to allow ThreadMain to be
  // called on a separate thread without inheriting from
  // PlatformThread::Delegate.
  friend class TpmInitTask;

 public:
  using OwnershipCallback =
      base::Callback<void(bool status, bool took_ownership)>;

  TpmInit(Tpm* tpm, Platform* platform);
  TpmInit(const TpmInit&) = delete;
  TpmInit& operator=(const TpmInit&) = delete;

  virtual ~TpmInit();

  virtual void Init(OwnershipCallback ownership_callback);

  // Sets the TPM to the state where we last left it in. This must be called
  // before the *TakeOwnership functions below, if we need to.
  //
  // Parameters:
  //   load_key - TRUE to load load Cryptohome key.
  //
  // Returns false if the instance has already been setup.
  virtual bool SetupTpm(bool load_key);

  // Asynchronously takes ownership of the TPM. The TPM is initialized following
  // these steps:
  //
  //   1. The TPM is owned with default owner password.
  //   2. The SRK password is cleared. The SRK is then unrestricted.
  //   3. New owner password is established.
  //   4. (This new password WILL be wiped later when all owner dependencies
  //       have been removed.)
  //
  // At each step in the process, a status file is updated so that we can
  // resume the initialization later (see SetupTpm above).
  //
  // The initialization is usually done asynchronously. Attestation and
  // InstallAttributes must remove themselves from owner dependency list so that
  // the owner password can be cleared.
  //
  // Returns true if a thread was spawn to do the actual initialization.
  virtual bool AsyncTakeOwnership();

  // Synchronously takes ownership of the TPM.
  //
  // Returns true if the TPM initialization process (as outlined above) is
  // completed at step 3.
  virtual bool TakeOwnership(bool* OUT_took_ownership);

  // Returns true if the TPM is initialized and ready for use.
  virtual bool IsTpmReady();

  // Returns true if the TPM is enabled.
  virtual bool IsTpmEnabled();

  // Returns true if the TPM is owned.
  virtual bool IsTpmOwned();

  // Marks the TPM as being owned.
  virtual void SetTpmOwned(bool owned);

  // Returns true if the TPM is being owned.
  virtual bool IsTpmBeingOwned();

  // Marks the TPM as being or not being been owned.
  virtual void SetTpmBeingOwned(bool being_owned);

  // Returns true if AsyncTakeOwnership() has been called to request TPM
  // ownership to be established.
  virtual bool OwnershipRequested();

  // Gets the TPM password if the TPM initialization took ownership.
  //
  // Parameters
  //   password (OUT) - The owner password used for the TPM
  virtual bool GetTpmPassword(brillo::SecureBlob* password);

  // Clears the TPM password from memory and disk.
  virtual void ClearStoredTpmPassword();

  // Removes the given owner dependency. When all dependencies have been removed
  // the owner password can be cleared.
  //
  // Parameters
  //   dependency - The dependency (on TPM ownership) to be removed
  virtual void RemoveTpmOwnerDependency(
      TpmPersistentState::TpmOwnerDependency dependency);

  virtual void set_tpm(Tpm* value);

  virtual Tpm* get_tpm();

  virtual bool HasCryptohomeKey();

  virtual TpmKeyHandle GetCryptohomeKey();

  virtual bool ReloadCryptohomeKey();

  virtual bool GetVersion(Tpm::TpmVersionInfo* version_info);

  virtual bool ShallInitialize();

 private:
  FRIEND_TEST(TpmInitTest, ContinueInterruptedInitializeSrk);

  virtual void ThreadMain();

  // Invoked by SetupTpm to restore TPM state from saved state in storage.
  void RestoreTpmStateFromStorage();

  // Creates a random owner password.
  //
  // Parameters
  //   password (OUT) - the generated password
  void CreateOwnerPassword(brillo::SecureBlob* password);

  // Retrieves the TPM owner password.
  bool LoadOwnerPassword(brillo::SecureBlob* owner_password);

  // Returns whether or not the TPM is enabled by checking a flag in the TPM's
  // entry in either /sys/class/misc or /sys/class/tpm.
  bool IsEnabledCheckViaSysfs(const base::FilePath& enabled_file);

  // Returns whether or not the TPM is owned by checking a flag in the TPM's
  // entry in either /sys/class/misc or /sys/class/tpm.
  bool IsOwnedCheckViaSysfs(const base::FilePath& owned_file);

  bool SaveCryptohomeKey(const brillo::SecureBlob& wrapped_key);

  Tpm::TpmRetryAction LoadCryptohomeKey(ScopedKeyHandle* key_handle);

  bool CreateCryptohomeKey();

  bool LoadOrCreateCryptohomeKey(ScopedKeyHandle* key_handle);

  // Returns true if the first byte of the file |file_name| is "1".
  bool CheckSysfsForOne(const base::FilePath& file_name) const;

  // The background task for initializing the TPM, implemented as a
  // PlatformThread::Delegate.
  std::unique_ptr<TpmInitTask> tpm_init_task_;
  base::PlatformThreadHandle init_thread_;

  OwnershipCallback ownership_callback_;

  bool take_ownership_called_ = false;
  bool took_ownership_ = false;
  int64_t initialization_time_ = 0;
  Platform* platform_ = nullptr;
  TpmPersistentState tpm_persistent_state_;
  ScopedKeyHandle cryptohome_key_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_INIT_H_
