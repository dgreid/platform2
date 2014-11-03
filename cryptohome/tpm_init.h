// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TpmInit - public interface class for initializing the TPM
#ifndef CRYPTOHOME_TPM_INIT_H_
#define CRYPTOHOME_TPM_INIT_H_

#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <chromeos/secure_blob.h>
#include <trousers/scoped_tss_type.h>

#include "cryptohome/tpm.h"

#include "tpm_status.pb.h"  // NOLINT(build/include)

namespace cryptohome {

class TpmInitTask;
class Platform;

class TpmInit {
  // Friend class TpmInitTask as it is a glue class to allow ThreadMain to be
  // called on a separate thread without inheriting from
  // PlatformThread::Delegate
  friend class TpmInitTask;
 public:
  enum TpmOwnerDependency {
      kInstallAttributes,
      kAttestation
  };

  class TpmInitCallback {
   public:
    virtual void InitializeTpmComplete(bool status, bool took_ownership) = 0;
  };

  // Default constructor
  TpmInit(Tpm* tpm, Platform* platform);

  virtual ~TpmInit();

  virtual void Init(TpmInitCallback* notify_callback);

  // Sets the TPM to the state where we last left it in. This must be called
  // before the *InitializeTpm functions below, if we need to.
  //
  // Parameters:
  //   load_key - TRUE to load load Cryptohome key.
  //
  // Returns false if the instance has already been setup.
  virtual bool SetupTpm(bool load_key);

  // Asynchronously initializes the TPM. The TPM is initialized following these
  // steps:
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
  virtual bool AsyncInitializeTpm();

  // Synchronously initializes the TPM.
  //
  // Returns true if the TPM initialization process (as outlined above) is
  // completed at step 3.
  virtual bool InitializeTpm(bool* OUT_took_ownership);

  // Returns true if the TPM is initialized and ready for use
  virtual bool IsTpmReady();

  // Returns true if the TPM is enabled
  virtual bool IsTpmEnabled();

  // Returns true if the TPM is owned
  virtual bool IsTpmOwned();

  // Marks the TPM as being owned
  virtual void SetTpmOwned(bool owned);

  // Returns true if the TPM is being owned
  virtual bool IsTpmBeingOwned();

  // Marks the TPM as being or not being been owned
  virtual void SetTpmBeingOwned(bool being_owned);

  // Returns true if initialization has been called
  virtual bool HasInitializeBeenCalled();

  // Gets the TPM password if the TPM initialization took ownership
  //
  // Parameters
  //   password (OUT) - The owner password used for the TPM
  virtual bool GetTpmPassword(chromeos::Blob* password);

  // Clears the TPM password from memory and disk
  virtual void ClearStoredTpmPassword();

  // Removes the given owner dependency. When all dependencies have been removed
  // the owner password can be cleared.
  //
  // Parameters
  //   dependency - The dependency (on TPM ownership) to be removed
  virtual void RemoveTpmOwnerDependency(TpmOwnerDependency dependency);

  virtual void set_tpm(Tpm* value);

  virtual Tpm* get_tpm();

  virtual bool HasCryptohomeKey();

  virtual TSS_HCONTEXT GetCryptohomeContext();

  virtual TSS_HKEY GetCryptohomeKey();

  virtual bool ReloadCryptohomeKey();

 private:
  virtual void ThreadMain();

  // Loads the TpmStatus object
  bool LoadTpmStatus(TpmStatus* serialized);

  // Saves the TpmStatus object
  bool StoreTpmStatus(const TpmStatus& serialized);

  // Creates a random owner password
  //
  // Parameters
  //   password (OUT) - the generated password
  void CreateOwnerPassword(chromeos::SecureBlob* password);

  // Stores the TPM owner password to the TpmStatus object
  bool StoreOwnerPassword(const chromeos::Blob& owner_password,
                          TpmStatus* tpm_status);

  // Retrieves the TPM owner password
  bool LoadOwnerPassword(const TpmStatus& tpm_status,
                         chromeos::Blob* owner_password);

  // Migrate any TPM status files from old location to new location.
  void MigrateStatusFiles();

  // Returns whether or not the TPM is enabled by checking a flag in the TPM's
  // entry in /sys/class/misc
  bool IsEnabledCheckViaSysfs();

  // Returns whether or not the TPM is owned by checking a flag in the TPM's
  // entry in /sys/class/misc
  bool IsOwnedCheckViaSysfs();

  bool SaveCryptohomeKey(const chromeos::SecureBlob& wrapped_key);

  bool LoadCryptohomeKey(TSS_HCONTEXT context_handle, TSS_HKEY* key_handle,
                         TSS_RESULT* result);

  bool CreateCryptohomeKey(TSS_HCONTEXT context_handle);

  bool LoadOrCreateCryptohomeKey(TSS_HCONTEXT context_handle,
                                 TSS_HKEY* key_handle,
                                 TSS_RESULT* result);

  // Returns true if the first byte of the file |file_name| is "1"
  bool CheckSysfsForOne(const char* file_name) const;

  // The background task for initializing the TPM, implemented as a
  // PlatformThread::Delegate
  scoped_ptr<TpmInitTask> tpm_init_task_;
  base::PlatformThreadHandle init_thread_;

  TpmInitCallback* notify_callback_;

  bool initialize_called_;
  bool initialize_took_ownership_;
  int64_t initialization_time_;
  Platform* platform_;
  trousers::ScopedTssContext cryptohome_context_;
  trousers::ScopedTssKey cryptohome_key_;

  DISALLOW_COPY_AND_ASSIGN(TpmInit);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_INIT_H_
