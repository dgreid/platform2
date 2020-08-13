// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines interface for TpmPersistentState class.

#ifndef CRYPTOHOME_TPM_PERSISTENT_STATE_H_
#define CRYPTOHOME_TPM_PERSISTENT_STATE_H_

#include <brillo/secure_blob.h>

#include <base/synchronization/lock.h>

#include "cryptohome/tpm_status.pb.h"

namespace cryptohome {

class Platform;

// Class for managing persistent tpm state stored in the filesystem.
// Lazily reads the current state into memory on the first access and
// caches it there for further accesses.
// Note: not thread-safe.
class TpmPersistentState {
 public:
  // Dependencies on the tpm owner password. Each of the listed entities
  // clears its dependency when it no longer needs the owner password for
  // further initialization. The password is cleared from persistent state
  // once all dependencies are cleared.
  enum class TpmOwnerDependency {
    kInstallAttributes,
    kAttestation,
  };

  explicit TpmPersistentState(Platform* platform);

  // Indicates in the state that the tpm is owned with the default
  // well-known password. Sets the dependencies to the initial set (all
  // entities that depend on the owner password still need it kept in the
  // persistent state).
  // Saves the updated state in the persistent storage before returning.
  // Returns true on success, false otherwise.
  bool SetDefaultPassword();

  // Indicates in the state that the tpm is owned with the provided
  // password. The password is sealed to the current boot state and the
  // resulting encrypted value is passed to this method. Sets the dependencies
  // to the initial set (all entities that depend on the owner password still
  // need it kept in the persistent state).
  // Saves the updated state in the persistent storage before returning.
  // Returns true on success, false otherwise.
  bool SetSealedPassword(const brillo::SecureBlob& sealed_password);

  // Gets the sealed password saved in the persistent state for the tpm
  // owner. An empty value indicates default well-known password. If the
  // value is not empty, the password must be unsealed after getting it
  // from this method before using it for authorization.
  // Returns false if the state indicates that it doesn't contain a default
  // or a sealed password, true otherwise.
  bool GetSealedPassword(brillo::SecureBlob* sealed_password);

  // Resets the status to empty default, as before owning the tpm: the
  // owner password is not stored, no dependencies are set.
  // Saves the updated state in the persistent storage before returning.
  // Returns true on success, false otherwise.
  bool ClearStatus();

  // Clears the specified dependency on the owner password in the state.
  // If there were any changes, saves the updated state in the persistent
  // storage before returning.
  // Returns true on success, false otherwise.
  bool ClearDependency(TpmOwnerDependency dependency);

  // Attempts to clear the owner password in the persistent state.
  // If there were any changes, saves the updated state in the persistent
  // storage before returning.
  // Returns false if there are still pending dependencies or it failed to
  // update the state, false otherwise.
  bool ClearStoredPasswordIfNotNeeded();

  // Returns the flag that indicates if the tpm is marked as 'ready', meaning
  // that tpm initialization has been completed for it. Caches the 'ready'
  // flag in memory on the first access. Subsequent checks return the cached
  // value.
  bool IsReady();

  // Sets the 'ready' flag for the tpm (in memory and in the persistent
  // storage).
  // If there were any changes, saves the updated flag in the persistent
  // storage before returning.
  // Returns true on success, false otherwise.
  bool SetReady(bool is_ready);

  // Returns the global flag indicating if cryptohomed shall attempt TPM
  // initialization. Reads the flag from persistent storage and caches it in
  // memory on the first access. Subsequent checks return the cached value.
  // About the flag: cryptohomed is normally requested to attempt TPM
  // initialization during OOBE. The flag is persistent over reboots: if the
  // TPM is still not initialized yet upon reboot, cryptohomed shall
  // attempt to continue the interrupted initialization. After successfully
  // owning the TPM, this flag is cleared. Powerwash also clears the flag.
  bool ShallInitialize() const;

  // Sets the global flag indicating if cryptohomed was requested to attempt
  // TPM initialization. See ShallInitialize() for the flag description.
  // If there were any changes, saves the updated flag in the persistent
  // storage before returning.
  // Returns true on success, false otherwise.
  bool SetShallInitialize(bool shall_initialize);

 private:
  // Loads TpmStatus that includes the owner password and the dependencies
  // from persistent storage, if not done yet. Caches TpmStatus in memory
  // after the first access. Subsequent Load's return success w/o re-reading
  // the data from persistent storage.
  bool LoadTpmStatus();

  // Saves the updated TpmStatus (in memory and in the persistent storage).
  // Returns true on success, false otherwise.
  bool StoreTpmStatus();

  // Checks whether the TPM is ready. Requires that |tpm_status_lock_| is held.
  bool IsReadyLocked();

  // Checks whether shall initialize is set. Requires that |tpm_status_lock_| is
  // held.
  bool ShallInitializeLocked() const;

  Platform* platform_;

  // Protects access to data members held by this class.
  mutable base::Lock tpm_status_lock_;

  // Cached TpmStatus (read_tpm_status_ tells if was already read and cached).
  // TODO(apronin): replace with std::optional / base::Optional when available.
  bool read_tpm_status_ = false;
  TpmStatus tpm_status_;

  // Cached "readiness" flag (read_tpm_ready_ tells if was already read and
  // cached).
  // TODO(apronin): replace with std::optional / base::Optional when available.
  bool read_tpm_ready_ = false;
  bool tpm_ready_ = false;

  // Cached "shall initialize" flag implementation:
  //  - checked_shall_initialize_ - indicates if a non-volatile
  //    flag has been read from the file system into the cached flag below.
  //  - shall_initialize_ - in-memory cached flag.
  // TODO(apronin): replace with std::optional / base::Optional when available.
  // TODO(apronin): refactor tpm_ready and shall_initialize to have
  // a shared generic implementation parametrized by the underlying filename.
  mutable bool read_shall_initialize_ = false;
  mutable bool shall_initialize_ = false;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_PERSISTENT_STATE_H_
