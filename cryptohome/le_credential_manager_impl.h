// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LE_CREDENTIAL_MANAGER_IMPL_H_
#define CRYPTOHOME_LE_CREDENTIAL_MANAGER_IMPL_H_

#include "cryptohome/le_credential_manager.h"

#include <map>
#include <memory>
#include <vector>

#include "cryptohome/le_credential_backend.h"
#include "cryptohome/sign_in_hash_tree.h"

namespace cryptohome {

// Class containing all logic pertaining to management of Low Entropy(LE)
// credentials. The stated aim of this class should be the following:
// - Provide an interface to Set and Remove credentials in the underlying
// storage.
// - Provide an interface to verify a credential.
//
// This class contains a SignInHashTree object, which is used to store and
// maintain the credentials on disk.
//
// It also contains a pointer to a TPM object which will be able to invoke the
// necessary commands on the TPM side, for verification.
class LECredentialManagerImpl : public LECredentialManager {
 public:
  explicit LECredentialManagerImpl(LECredentialBackend* le_backend,
                                   const base::FilePath& le_basedir);

  virtual ~LECredentialManagerImpl() {}

  LECredError InsertCredential(const brillo::SecureBlob& le_secret,
                               const brillo::SecureBlob& he_secret,
                               const brillo::SecureBlob& reset_secret,
                               const DelaySchedule& delay_sched,
                               const ValidPcrCriteria& valid_pcr_criteria,
                               uint64_t* ret_label) override;

  LECredError CheckCredential(const uint64_t& label,
                              const brillo::SecureBlob& le_secret,
                              brillo::SecureBlob* he_secret,
                              brillo::SecureBlob* reset_secret) override;

  LECredError ResetCredential(const uint64_t& label,
                              const brillo::SecureBlob& reset_secret) override;

  LECredError RemoveCredential(const uint64_t& label) override;

  bool NeedsPcrBinding(const uint64_t& label) override;

  // Returns the number of wrong authentication attempts done since the label
  // was reset or created. Returns -1 if |label| is not present in the tree or
  // the tree is corrupted.
  int GetWrongAuthAttempts(const uint64_t& label) override;

 private:
  // Since the CheckCredential() and ResetCredential() functions are very
  // similar, this function combines the common parts of both the calls
  // into a generic "check credential" function. The label to be checked
  // is stored in |label|, the secret to be verified is in |secret|, the
  // high entropy credential and reset secret which gets released on successful
  // verification are stored in |he_secret| and |reset_secret|. A flag
  // |is_le_secret| is used to signal whether the secret being checked is the LE
  // secret (true) or the reset secret (false).
  //
  // Returns LE_CRED_SUCCESS on success.
  //
  // On failure, returns:
  // - LE_CRED_ERROR_INVALID_LE_SECRET for incorrect LE authentication attempt.
  // - LE_CRED_ERROR_INVALID_RESET_SECRET for incorrect reset secret.
  // incorrect attempts).
  // - LE_CRED_ERROR_HASH_TREE for error in hash tree.
  // - LE_CRED_ERROR_INVALID_LABEL for invalid label.
  // - LE_CRED_ERROR_INVALID_METADATA for invalid credential metadata.
  // - LE_CRED_ERROR_PCR_NOT_MATCH if the PCR registers from TPM have unexpected
  // values, in which case only reboot will allow this user to authenticate.
  LECredError CheckSecret(const uint64_t& label,
                          const brillo::SecureBlob& secret,
                          brillo::SecureBlob* he_secret,
                          brillo::SecureBlob* reset_secret,
                          bool is_le_secret);

  // Helper function to retrieve the credential metadata, MAC, and auxiliary
  // hashes associated with a label |label| (stored in |cred_metadata|, |mac|
  // and |h_aux| respectively). |metadata_lost| will denote whether the label
  // contains valid metadata (false) or not (true).
  //
  // Returns LE_CRED_SUCCESS on success.
  // On failure, returns:
  // - LE_CRED_ERROR_INVALID_LABEL if the label provided doesn't exist.
  // - LE_CRED_ERROR_HASH_TREE if there was hash tree error (possibly out of
  // sync).
  LECredError RetrieveLabelInfo(const SignInHashTree::Label& label,
                                std::vector<uint8_t>* cred_metadata,
                                std::vector<uint8_t>* mac,
                                std::vector<std::vector<uint8_t>>* h_aux,
                                bool* metadata_lost);

  // Given a label, gets the list of auxiliary hashes for that label.
  // On failure, returns an empty vector.
  std::vector<std::vector<uint8_t>> GetAuxHashes(
      const SignInHashTree::Label& label);

  // Converts the error returned from LECredentialBackend to the equivalent
  // LECredError.
  LECredError ConvertTpmError(LECredBackendError err);

  // Performs checks to ensure the SignInHashTree is in sync with the tree
  // state in the LECredentialBackend. If there is an out-of-sync situation,
  // this function also attempts to get the HashTree back in sync.
  //
  // Returns true on successful synchronization, and false on failure. On
  // failure, |is_locked_| will be set to true, to prevent further
  // operations during the class lifecycle.
  bool Sync();

  // Replays the InsertCredential operation using the information provided
  // from the log entry from the LE credential backend.
  // |label| denotes which label to perform the operation on,
  // |log_root| is what the root hash should be after this operation is
  // completed. It should directly be used from the log entry.
  // |mac| is the MAC of the credential which has to be inserted.
  //
  // Returns true on success, false on failure.
  //
  // NOTE: A replayed insert is unusable and should be deleted after the replay
  // is complete.
  bool ReplayInsert(uint64_t label,
                    const std::vector<uint8_t>& log_root,
                    const std::vector<uint8_t>& mac);

  // Replays the CheckCredential / ResetCredential operation using the
  // information provided from the log entry from the LE credential
  // backend.
  // |label| denotes which credential label to perform the operation on.
  // |log_root| is what the root hash should be after this operation is
  // completed. It should directly be used from the log entry.
  //
  // Returns true on success, false on failure.
  bool ReplayCheck(uint64_t label, const std::vector<uint8_t>& log_root);

  // Resets the HashTree.
  bool ReplayResetTree();

  // Replays the RemoveCredential for |label| which is provided from
  // the LE Backend Replay logs.
  //
  // Returns true on success, false otherwise.
  bool ReplayRemove(uint64_t label);

  // Replays all the log operations provided in |log|, and makes the
  // corresponding updates to the HashTree.
  bool ReplayLogEntries(const std::vector<LELogEntry>& log,
                        const std::vector<uint8_t>& disk_root_hash);

  // Last resort flag which prevents any further Low Entropy operations from
  // occurring, till the next time the class is instantiated.
  // This is used in a situation where an operation succeeds on the TPM,
  // but its on-disk counterpart fails. In this case, the mitigation strategy
  // is as follows:
  // - Prevent any further LE operations, to prevent disk and TPM from
  // going further out of state, till next reboot.
  // - Hope that on reboot, the problems causing disk failure don't recur,
  // and the TPM replay log will enable the disk state to get in sync with
  // the TPM again.
  //
  // We will collect UMA stats from the field and refine this strategy
  // as required.
  bool is_locked_;
  // Pointer to an implementation of the LE Credential operations in TPM.
  LECredentialBackend* le_tpm_backend_;
  // In-memory copy of LEBackend's root hash value.
  std::vector<uint8_t> root_hash_;
  // Directory where all LE Credential related data is stored.
  base::FilePath basedir_;
  std::unique_ptr<SignInHashTree> hash_tree_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_LE_CREDENTIAL_MANAGER_IMPL_H_
