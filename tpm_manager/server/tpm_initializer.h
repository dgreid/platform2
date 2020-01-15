// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_TPM_INITIALIZER_H_
#define TPM_MANAGER_SERVER_TPM_INITIALIZER_H_

#include "tpm_manager/server/dictionary_attack_reset_status.h"

namespace tpm_manager {

// TpmInitializer performs initialization tasks on some kind of TPM device.
class TpmInitializer {
 public:
  TpmInitializer() = default;
  virtual ~TpmInitializer() = default;

  // Initializes a TPM and returns true on success. If the TPM is already
  // initialized, this method has no effect and succeeds. If the TPM is
  // partially initialized, e.g. the process was previously interrupted, then
  // the process picks up where it left off.
  virtual bool InitializeTpm() = 0;

  // Performs actions that can be done on uninitialized TPM before
  // receiving a signal that taking ownership can be attempted.
  // This is an optional optimization: InitializeTpm() doesn't rely on
  // it to be called first and runs pre-initialization steps, if necessary,
  // itself.
  // If the TPM is already initialized, does nothing.
  // Returns an error if pre-initialization is attempted but failed.
  virtual bool PreInitializeTpm() = 0;

  // Ensures the owner delegate is stored in the persistent storage, if
  // applicable. Returns |true| iff the owner delegate can be found after this
  // function call. In case the delegate is non-applicable for the underlying
  // implementation, performs no-ops and returns |true|.
  virtual bool EnsurePersistentOwnerDelegate() = 0;

  // This will be called when the service is initializing. It is an early
  // opportunity to perform tasks related to verified boot.
  virtual void VerifiedBootHelper() = 0;

  // Reset the state of TPM dictionary attack protection. Returns true on
  // success.
  virtual DictionaryAttackResetStatus ResetDictionaryAttackLock() = 0;

  // Removes stale auths and owner dependencies from the on-disk local data, if
  // any. If the local data is already in use, or if we cannot determine that,
  // the data won't be touched.
  //
  // Note that this function doesn't guarantee to remove all stale data if there
  // is a TPM and/or disk IO error. It does the work in its best effort.
  virtual void PruneStoredPasswords() = 0;
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_TPM_INITIALIZER_H_
