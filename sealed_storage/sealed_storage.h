// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header file defines the interface for the "sealed storage": an
// engine that allows "sealing" sensitive data, resulting in a blob that can
// only be restored to the original data on a local machine in a given state,
// i.e. when specified PCRs have specified values.
//
// For the general description of the process see README.md.
// For the examples of using the interface see sealed_storage_tool.cc.

#ifndef SEALED_STORAGE_SEALED_STORAGE_H_
#define SEALED_STORAGE_SEALED_STORAGE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/optional.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <trunks/trunks_factory.h>

namespace sealed_storage {

using ScopedTrunksFactory = std::unique_ptr<trunks::TrunksFactory>;
using ScopedTpmOwnership =
    std::unique_ptr<org::chromium::TpmManagerProxyInterface>;
using BootMode = char[3];

constexpr BootMode kVerifiedBootMode = {0, 0, 1};
constexpr BootMode kDevMode = {1, 0, 1};

// Structure for defining the PCR-binding policy for the sealed storage.
// The policy defines the state, in which the data can be unsealed.
struct Policy {
  using PcrMap = std::map<uint32_t, std::string>;

  // Returns an entry for the PCR-binding map corresponding to the specified
  // boot mode, i.e. (PCR0 == value_corresponding_to_bootmode).
  static PcrMap::value_type BootModePCR(const BootMode& mode);

  // Returns an entry for the PCR-binding map corresponding to the specified
  // PCR being in the initial state, i.e. (PCRx == initial_value).
  static PcrMap::value_type UnchangedPCR(uint32_t pcr_num);

  PcrMap pcr_map;
};

// Data type for non-sensitive data (e.g. serialized encrypted blob).
using Data = brillo::Blob;

// Data type for sensitive data (e.g. plaintext stored in the sealed storage).
using SecretData = brillo::SecureBlob;

// Public seeds: data that will go into the resulting blob to allow later
// decryption. Contains pub point, IV, plaintext size and policy digest.
// See README.
struct PubSeeds {
  trunks::TPM2B_ECC_POINT pub_point;
  trunks::TPM2B_DIGEST iv;
  uint16_t plain_size;
  base::Optional<std::string> policy_digest;
};

// Private seeds: the ephemeral private part that is "sealed" using the sealing
// key. See README.
struct PrivSeeds {
  trunks::TPM2B_ECC_POINT z_point;
};

class SealedStorage {
 public:
  // The constructor that allows specifying TrunksFactory and
  // TpmOwnershipInterface, useful for mocking when testing.
  SealedStorage(const Policy& policy,
                trunks::TrunksFactory* trunks_factory,
                org::chromium::TpmManagerProxyInterface* tpm_ownership);
  // The constructor that uses the default TrunksFactory and
  // TpmOwnershipInterface, which connect to the corresponding daemons over
  // dbus.
  explicit SealedStorage(const Policy& policy);
  SealedStorage(const SealedStorage&) = delete;
  SealedStorage& operator=(const SealedStorage&) = delete;

  // Returns the default TrunksFactory that connects to trunksd over dbus.
  static ScopedTrunksFactory CreateTrunksFactory();
  // Returns the default TpmOwnershipInterface that connects to tpm_managerd
  // over dbus.
  static ScopedTpmOwnership CreateTpmOwnershipInterface();

  trunks::TrunksFactory* trunks_factory() const { return trunks_factory_; }

  org::chromium::TpmManagerProxyInterface* tpm_ownership() const {
    return tpm_ownership_;
  }

  void set_plain_size_for_v1(uint16_t size) { plain_size_for_v1_ = size; }

  void reset_policy(const Policy& policy) { policy_ = policy; }
  const Policy& policy() const { return policy_; }

  // Seals the provided plain data according to the policy specified in the
  // constructor. Returns the encrypted blob, or nullopt in case of error.
  base::Optional<Data> Seal(const SecretData& plain_data) const;

  // Unseals the encrypted blob according to the policy specified in the
  // constructor. Returns the original plain data, or nullopt in case of error.
  base::Optional<SecretData> Unseal(const Data& sealed_data) const;

  // Extends the PCR with the specified number, making sure it's no longer
  // in the initial state. Useful for locking the encrypted blob from further
  // unsealing until reboot: after extending a PCR specified in a policy to a
  // value that doesn't match the policy, unseal will fail until the PCRs are
  // reset after reboot. Returns true on success, false otherwise.
  bool ExtendPCR(uint32_t pcr_num) const;

  // Checks if the current state of the PCRs matches the policy specified in
  // the constructor. Returns the flag, which indicates if the current state
  // matches the policy (true if yes, false if not), or nullopt in case of
  // error.
  base::Optional<bool> CheckState() const;

 private:
  // Creates the TPM-bound ECC sealing key with the attached policy specified
  // in the constructor. If |expected_digest| is specified checks that the
  // resulting policy digest matches it, and returns false in case of mismatch.
  // On success, returns true and fills |key_handle|, |key_name| and
  // |resulting_digest| with the handle, name and policy digest of the created
  // key object. On failure, returns false.
  bool PrepareSealingKeyObject(
      const base::Optional<std::string>& expected_digest,
      trunks::TPM_HANDLE* key_handle,
      std::string* key_name,
      std::string* resulting_digest) const;

  // Retrieves the endorsement password.
  // On success, returns true and fills the |password|.
  bool GetEndorsementPassword(std::string* password) const;

  // Creates the primary key object with the specified parent, sensitive and
  // public areas. |object_descr| is the textual description of the created
  // key for logging purposes. |parent_handle| and |parent_name| define the
  // parent, |auth_delegate| is the authorization session for CreatePrimary
  // with that parent.
  // On success, returns true and fills |object_handle| and |object_name| with
  // the handle and name of the created key object. On failure, returns false.
  bool CreatePrimaryKeyObject(const std::string& object_descr,
                              trunks::TPM_HANDLE parent_handle,
                              const std::string& parent_name,
                              const trunks::TPMS_SENSITIVE_CREATE& sensitive,
                              const trunks::TPMT_PUBLIC& public_area,
                              trunks::AuthorizationDelegate* auth_delegate,
                              trunks::TPM_HANDLE* object_handle,
                              std::string* object_name) const;

  // Checks if the underlying trunks factory is properly initialized.
  bool CheckInitialized() const;

  // Creates public and private seeds for sealing: creates the sealing key,
  // goes through ECDH_KeyGen to get the public and private parts of the
  // ephemeral key, and generates a random IV.
  // On success, returns true and fills |priv_seeds| and |pub_seeds|. Otherwise,
  // returns false.
  bool CreateEncryptionSeeds(PrivSeeds* priv_seeds, PubSeeds* pub_seeds) const;

  // Restores the private seeds given the public seed during unsealing: creates
  // the sealing key, goes through ECDH_ZGen to get the private part given the
  // public part of the ephemeral key.
  // On success, returns true and fills |priv_seeds|. Otherwise, returns false.
  bool RestoreEncryptionSeeds(const PubSeeds& pub_seeds,
                              PrivSeeds* priv_seeds) const;

  // Serializes public seeds and encrypted data into an encrypted blob.
  // Returns the resulting blob on success, or nullopt otherwise.
  base::Optional<Data> SerializeSealedBlob(const PubSeeds& pub_seeds,
                                           const Data& encrypted_data) const;

  // Deserializes the encrypted blob into public seeds and encrypted data.
  // On success, returns true and fills |pub_seeds| and |encrypted_data|.
  // Otherwise, returns false.
  bool DeserializeSealedBlob(const Data& sealed_data,
                             PubSeeds* pub_seeds,
                             Data* encrypted_data) const;

  Policy policy_;

  // What is the expected plaintext size for data sealed with version 1 of
  // SealedStorage. In practice, v1 was only supposed to be used for 16 byte
  // buffers, but can be manually reset, if some application used it for other
  // data.
  uint16_t plain_size_for_v1_ = 16;

  // dft_* memmbers must go before TrunksFactory* and pmOwnershipInterface*
  // to be deleted last during destruction.
  ScopedTrunksFactory dft_trunks_factory_;
  ScopedTpmOwnership dft_tpm_ownership_;
  trunks::TrunksFactory* trunks_factory_;
  org::chromium::TpmManagerProxyInterface* tpm_ownership_;
};

}  // namespace sealed_storage

#endif  // SEALED_STORAGE_SEALED_STORAGE_H_
