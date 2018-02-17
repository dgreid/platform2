// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LE_CREDENTIAL_MANAGER_H_
#define CRYPTOHOME_LE_CREDENTIAL_MANAGER_H_

#include <map>
#include <memory>
#include <vector>

#include "cryptohome/le_credential_backend.h"
#include "cryptohome/sign_in_hash_tree.h"

namespace cryptohome {

// List of all the errors returned by the LECredentialManager class.
enum LECredError {
  // Operation succeeded.
  LE_CRED_SUCCESS,
  // Check failed due to incorrect Low Entropy(LE) secret.
  LE_CRED_ERROR_INVALID_LE_SECRET,
  // Check failed due to too many attempts as per delay schedule.
  LE_CRED_ERROR_TOO_MANY_ATTEMPTS,
  // Error in hash tree synchronization.
  LE_CRED_ERROR_HASH_TREE,
  // Label provided isn't present in hash tree.
  LE_CRED_ERROR_INVALID_LABEL,
  // No free labels available.
  LE_CRED_ERROR_NO_FREE_LABEL,
};

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
class LECredentialManager {
 public:
  typedef std::map<uint32_t, uint32_t> DelaySchedule;

  explicit LECredentialManager(LECredentialBackend* le_backend);

  // Inserts an LE credential into the system.
  //
  // The Low entropy credential is represented by |le_secret|, and the high
  // entropy and reset secrets by |he_secret| and |reset_secret| respectively.
  // The delay schedule which governs the rate at which CheckCredential()
  // attempts are allowed is provided in |delay_sched|. On success, returns
  // LE_CRED_SUCCESS and stores the newly provisioned label in |ret_label|. On
  // failure, returns:
  // - LE_CRED_ERROR_NO_FREE_LABEL if there is no free label.
  // - LE_CRED_ERROR_HASH_TREE if there was an error in the hash tree.
  //
  // The returned label should be placed into the metadata associated with the
  // Encrypted Vault Key (EVK). so that it can be used to look up the credential
  // later.
  LECredError InsertCredential(const brillo::SecureBlob& le_secret,
                               const brillo::SecureBlob& he_secret,
                               const brillo::SecureBlob& reset_secret,
                               const DelaySchedule& delay_sched,
                               uint64_t* ret_label);

  // Attempts authentication for a LE Credential.
  //
  // Checks whether the LE credential |le_secret| for a |label| is correct.
  // Returns LE_TPM_SUCCESS on success. Additionally, the released
  // high entropy credential is placed in |he_secret|.
  //
  // On failure, returns:
  // LE_CRED_ERROR_INVALID_LE_SECRET for incorrect authentication attempt.
  // LE_CRED_ERROR_TOO_MANY_ATTEMPTS for locked out credential (too many
  // incorrect attempts). LE_CRED_ERROR_HASH_TREE for error in hash tree.
  // LE_CRED_ERROR_INVALID_LABEL for invalid label.
  LECredError CheckCredential(const uint64_t& label,
                              const brillo::SecureBlob& le_secret,
                              brillo::SecureBlob* he_secret);

  // Remove a credential at node with label |label|.
  //
  // Returns LE_TPM_SUCCESS on success.
  // On failure, returns:
  // - LE_CRED_ERROR_INVALID_LABEL for invalid label.
  // - LE_CRED_ERROR_HASH_TREE for hash tree error.
  LECredError RemoveCredential(const uint64_t& label);

 private:
  // Helper function to retrieve the credential metadata, MAC, and auxiliary
  // hashes associated with a label |label| (stored in |cred_metadata|, |mac|
  // and |h_aux| respectively).
  //
  // Returns LE_CRED_SUCCESS on success.
  // On failure, returns:
  // - LE_CRED_ERROR_INVALID_LABEL if the label provided doesn't exist.
  // - LE_CRED_ERROR_HASH_TREE if there was hash tree error (possibly out of
  // sync).
  LECredError RetrieveLabelInfo(const SignInHashTree::Label& label,
                                std::vector<uint8_t>* cred_metadata,
                                std::vector<uint8_t>* mac,
                                std::vector<std::vector<uint8_t>>* h_aux);

  // Given a label, gets the list of auxiliary hashes for that label.
  // On failure, returns an empty vector.
  std::vector<std::vector<uint8_t>> GetAuxHashes(
      const SignInHashTree::Label& label);

  // Converts the error returned from LECredentialBackend to the equivalent
  // LECredError.
  LECredError ConvertTpmError(LECredBackendError err);

  // Last resort flag which prevents any further Low Entropy operations from
  // occuring, till the next time the class is instantiated.
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
  std::unique_ptr<SignInHashTree> hash_tree_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_LE_CREDENTIAL_MANAGER_H_
