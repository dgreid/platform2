// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tpm - class for performing encryption/decryption in the TPM.  For cryptohome,
// the TPM may be used as a way to strengthen the security of the wrapped vault
// keys stored on disk.  When the TPM is enabled, there is a system-wide
// cryptohome RSA key that is used during the encryption/decryption of these
// keys.
// TODO(wad) make more functions virtual for use in mock_tpm.h.

#ifndef CRYPTOHOME_TPM_H_
#define CRYPTOHOME_TPM_H_

#include <stdint.h>

#include <map>
#include <set>
#include <string>

#include <base/optional.h>
#include <base/synchronization/lock.h>
#include <brillo/secure_blob.h>
#include <openssl/rsa.h>

#include "cryptohome/tpm_persistent_state.h"

#if USE_TPM2
#include "cryptohome/tpm2.h"
#else
#include "cryptohome/tpm1.h"
#endif

namespace cryptohome {

using TpmKeyHandle = uint32_t;

class LECredentialBackend;
class SignatureSealingBackend;
class Tpm;

constexpr TpmKeyHandle kInvalidKeyHandle = 0;
constexpr uint32_t kNotBoundToPCR = UINT32_MAX;
constexpr uint32_t kTpmBootPCR = 0;
// The PCR index used to restrict the device to access to a single user data.
constexpr uint32_t kTpmSingleUserPCR = 4;
const char kDefaultPcrValue[32] = {0};

// This class provides a wrapper around TpmKeyHandle, and manages freeing of
// TPM resources associated with TPM keys. It does not take ownership of the
// Tpm pointer provided.
class ScopedKeyHandle {
 public:
  ScopedKeyHandle();
  ~ScopedKeyHandle();
  TpmKeyHandle value();
  TpmKeyHandle release();
  void reset(Tpm* tpm, TpmKeyHandle handle);

 private:
  Tpm* tpm_;
  TpmKeyHandle handle_;
  DISALLOW_COPY_AND_ASSIGN(ScopedKeyHandle);
};

class Tpm {
 public:
  enum TpmVersion {
    TPM_UNKNOWN = 0,
    TPM_1_2 = 1,
    TPM_2_0 = 2,
  };

  enum TpmRetryAction {
    // Action succeeded - no retry needed.
    kTpmRetryNone,
    // Action failed - retrying won't change the outcome, so don't retry.
    kTpmRetryFailNoRetry,
    // Action failed - TPM communication failure.
    kTpmRetryCommFailure,
    // Action failed - TPM is in dictionary attack defense mode.
    kTpmRetryDefendLock,
    // Action failed - fatal error.
    kTpmRetryFatal,
    // Action failed - target key/object handle is invalid.
    kTpmRetryInvalidHandle,
    // Action failed - can't load a key or other object.
    kTpmRetryLoadFail,
    // Action failed - TPM is in the state that requires reboot.
    kTpmRetryReboot,
    // Action failed - TPM requested retrying the action later.
    kTpmRetryLater,
  };

  enum TpmNvramFlags {
    // NVRAM space is write-once; lock by writing 0 bytes
    kTpmNvramWriteDefine = (1 << 0),

    // NVRAM space is only accessible if PCR0 has the same value it did
    // when the space was created
    kTpmNvramBindToPCR0 = (1 << 1),

    // NVRAM space is readable by firmware (PPREAD is set)
    kTpmNvramFirmwareReadable = (1 << 2),
  };

  // Specifies the type of the currently signed in user.
  enum class UserType {
    // Type not known: is set initially, before the first SetUserType, or
    // in case of errors only.
    Unknown,
    // Non-owner or no user signed in.
    NonOwner,
    // Owner signed in.
    Owner,
  };

  // Results of PCR quoting.
  enum class QuotePcrResult {
    kSuccess,
    kFailure,
    kInvalidPcrValue,
  };

  struct TpmStatusInfo {
    uint32_t last_tpm_error = 0;
    bool can_connect = false;
    bool can_load_srk = false;
    bool can_load_srk_public_key = false;
    bool srk_vulnerable_roca = false;
    bool has_cryptohome_key = false;
    bool can_encrypt = false;
    bool can_decrypt = false;
    bool this_instance_has_context = false;
    bool this_instance_has_key_handle = false;
  };

  // Holds TPM version info.
  struct TpmVersionInfo {
    uint32_t family;
    uint64_t spec_level;
    uint32_t manufacturer;
    uint32_t tpm_model;
    uint64_t firmware_version;
    std::string vendor_specific;

    // Computes a fingerprint of the version parameters in the struct fields by
    // running them through a hash function and truncating the output. The idea
    // is to produce a fingerprint that's unique in practice for each set of
    // real-life version parameters.
    int GetFingerprint() const;
  };

  // Holds status information pertaining to TPM firmware updates for Infineon
  // TPMs.
  struct IFXFieldUpgradeInfo {
    // Describes status of a firmware package.
    struct FirmwarePackage {
      uint32_t package_id;
      uint32_t version;
      uint32_t stale_version;
    };

    // Chunk size for transmitting the firmware update.
    uint16_t max_data_size;
    // Version numbers of the bootloader in ROM.
    FirmwarePackage bootloader;
    // Version numbers for the two writable firmware slots.
    FirmwarePackage firmware[2];
    // Status of the TPM - 0x5a3c indicates bootloader mode, i.e. no running
    // TPM firmware.
    uint16_t status;
    // Version numbers of the firmware for which installation has started, but
    // not completed.
    FirmwarePackage process_fw;
    // Total number of updates the TPM has installed in its entire lifetime.
    uint16_t field_upgrade_counter;
  };

  // Number of alerts supported by UMA
  static constexpr size_t kAlertsNumber = 45;
  struct AlertsData {
    // alert counters with UMA enum index
    uint16_t counters[kAlertsNumber];
  };

  // Constants for default labels for use with the CreateDelegate() method.
  static constexpr uint8_t kDefaultDelegateFamilyLabel = 1;
  static constexpr uint8_t kDefaultDelegateLabel = 2;

  static Tpm* GetSingleton();

  static const uint32_t kLockboxIndex;

  virtual ~Tpm() {}

  // Returns TPM version
  virtual TpmVersion GetVersion() = 0;

  // Encrypts a data blob using the provided RSA key. Returns a TpmRetryAction
  // struct
  //
  // Parameters
  //   key_handle - The loaded TPM key handle
  //   plaintext - One RSA message to encrypt
  //   key - AES key to encrypt with
  //   ciphertext (OUT) - Encrypted blob
  virtual TpmRetryAction EncryptBlob(TpmKeyHandle key_handle,
                                     const brillo::SecureBlob& plaintext,
                                     const brillo::SecureBlob& key,
                                     brillo::SecureBlob* ciphertext) = 0;

  // Decrypts a data blob using the provided RSA key. Returns a TpmRetryAction
  // struct
  //
  // Parameters
  //   key_handle - The loaded TPM key handle
  //   ciphertext - One RSA message to encrypt
  //   key - AES key to encrypt with
  //   pcr_map - The map of PCR index -> value bound to the key. This is used
  //             only for TPM 2.0. If the key is not bound to PCR, an empty map
  //             should be provided. For TPM 1.2 this parameter is ignored, even
  //             if the key is bound to PCR, so an empty map can be used.
  //   plaintext (OUT) - Decrypted blob
  virtual TpmRetryAction DecryptBlob(
      TpmKeyHandle key_handle,
      const brillo::SecureBlob& ciphertext,
      const brillo::SecureBlob& key,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* plaintext) = 0;

  // Seals a data blob to provided PCR data, while assigning a authorization
  // value derived from provided |auth_blob|. For TPM2.0, |key_handle| is used
  // to decrypt the |auth_blob|, obtaining the authorization value. For TPM1.2
  // the |key_handle| is unused. Returns a TpmRetryAction struct.
  //
  // Parameters
  //   key_handle - The loaded TPM key handle.
  //   plaintext - The data blob to be sealed.
  //   auth_blob - The blob to be used to derive the authorization value.
  //   pcr_map - The map of PCR index -> expected value when Unseal will be
  //             used.
  //   sealed_data (OUT) - Sealed blob.
  virtual TpmRetryAction SealToPcrWithAuthorization(
      TpmKeyHandle key_handle,
      const brillo::SecureBlob& plaintext,
      const brillo::SecureBlob& auth_blob,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* sealed_data) = 0;

  // Unseal a data blob using the provided |auth_blob| to derive the
  // authorization value. For TPM2.0, |key_handle| is used to decrypt the
  // |auth_blob|, obtaining the authorization value. Also for TPM2.0, the
  // session used to unseal is not salted, meaning there's a security risk to
  // leak the |auth_blob|. That's because it uses one expensive operation in
  // decrypt to obtain the auth_value, and we can't afford to add a second
  // operation. This might change in the future if we implement ECC encryption
  // for salted sessions.
  // For TPM1.2 the |key_handle| is unused. Returns a TpmRetryAction struct.
  //
  // Parameters
  //   key_handle - The loaded TPM key handle.
  //   sealed_data - The sealed data.
  //   auth_blob - The blob used to derive the authorization value.
  //   pcr_map - The map of PCR index -> value bound to the key. This is used
  //             only for TPM 2.0. For TPM 1.2 this parameter is ignored, even
  //             so an empty map can be used.
  //   plaintext (OUT) - Unsealed blob.
  virtual TpmRetryAction UnsealWithAuthorization(
      TpmKeyHandle key_handle,
      const brillo::SecureBlob& sealed_data,
      const brillo::SecureBlob& auth_blob,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* plaintext) = 0;

  // Retrieves the sha1sum of the public key component of the RSA key
  virtual TpmRetryAction GetPublicKeyHash(TpmKeyHandle key_handle,
                                          brillo::SecureBlob* hash) = 0;

  // Returns the owner password if this instance was used to take ownership.
  // This will only occur when the TPM is unowned, which will be on OOBE
  //
  // Parameters
  //   owner_password (OUT) - The random owner password used
  virtual bool GetOwnerPassword(brillo::SecureBlob* owner_password) = 0;

  // Returns whether or not the TPM is enabled.  This method call returns a
  // cached result because querying the TPM directly will block if ownership is
  // currently being taken (such as on a separate thread).
  virtual bool IsEnabled() = 0;
  virtual void SetIsEnabled(bool enabled) = 0;

  // Returns whether or not the TPM is owned.  This method call returns a cached
  // result because querying the TPM directly will block if ownership is
  // currently being taken (such as on a separate thread).
  virtual bool IsOwned() = 0;
  virtual void SetIsOwned(bool owned) = 0;

  // Returns whether or not the TPM has the permission to reset lock.
  virtual bool HasResetLockPermissions() = 0;

  // Returns whether or not the TPM is enabled and owned using a call to
  // Tspi_TPM_GetCapability.
  //
  // Unlike former functions, this function performs the check (which could take
  // some time) every time it is invoked. It does not use cached value.
  //
  // Parameters
  //   enabled (OUT) - Whether the TPM is enabled
  //   owned (OUT) - Whether the TPM is owned
  //
  // Returns true if the check was successfully carried out.
  virtual bool PerformEnabledOwnedCheck(bool* enabled, bool* owned) = 0;

  // Returns whether or not this instance has been setup'd by an external
  // entity (such as cryptohome::TpmInit).
  virtual bool IsInitialized() = 0;
  virtual void SetIsInitialized(bool done) = 0;

  // Returns whether or not the TPM is being owned
  virtual bool IsBeingOwned() = 0;
  virtual void SetIsBeingOwned(bool value) = 0;

  // Gets random bytes from the TPM.
  //
  // Parameters
  //   length - The number of bytes to get
  //   data (OUT) - The random data from the TPM
  virtual bool GetRandomDataBlob(size_t length, brillo::Blob* data) = 0;

  // Gets random bytes from the TPM, returns them in a SecureBlob.
  // brillo::SecureBlob intentionally does not inherit from brillo::Blob.
  //
  // Parameters
  //   length - The number of bytes to get
  //   data (OUT) - The random data from the TPM
  virtual bool GetRandomDataSecureBlob(size_t length,
                                       brillo::SecureBlob* data) = 0;

  // Gets alerts data the TPM
  //
  // Parameters
  //   alerts (OUT) - Struct that contains TPM alerts information
  // Returns true is hardware supports Alerts reporting, false otherwise
  virtual bool GetAlertsData(Tpm::AlertsData* alerts) = 0;

  // Creates a NVRAM space in the TPM
  //
  // Parameters
  //   index - The index of the space
  //   length - The number of bytes to allocate
  //   flags - Flags for NVRAM space attributes; zero or more TpmNvramFlags
  // Returns false if the index or length invalid or the required
  // authorization is not possible.
  virtual bool DefineNvram(uint32_t index, size_t length, uint32_t flags) = 0;

  // Destroys a defined NVRAM space
  //
  // Parameters
  //  index - The index of the space to destroy
  // Returns false if the index is invalid or the required authorization
  // is not possible.
  virtual bool DestroyNvram(uint32_t index) = 0;

  // Writes the given blob to NVRAM
  //
  // Parameters
  //  index - The index of the space to write
  //  blob - the data to write (size==0 may be used for locking)
  // Returns false if the index is invalid or the request lacks the required
  // authorization.
  virtual bool WriteNvram(uint32_t index, const brillo::SecureBlob& blob) = 0;

  // Reads from the NVRAM index to the given blob
  //
  // Parameters
  //  index - The index of the space to write
  //  blob - the data to read
  // Returns false if the index is invalid or the request lacks the required
  // authorization.
  virtual bool ReadNvram(uint32_t index, brillo::SecureBlob* blob) = 0;

  // Determines if the given index is defined in the TPM
  //
  // Parameters
  //  index - The index of the space
  // Returns true if it exists and false if it doesn't or there is a failure to
  // communicate with the TPM.
  virtual bool IsNvramDefined(uint32_t index) = 0;

  // Determines if the NVRAM space at the given index is bWriteDefine locked
  //
  // Parameters
  //   index - The index of the space
  // Returns true if locked and false if it is unlocked, the space does not
  // exist, or there is a TPM-related error.
  virtual bool IsNvramLocked(uint32_t index) = 0;

  // Locks NVRAM space for writing
  //
  // Parameters
  //  index - The index of the space
  // Returns true if the index has been successfully write-locked, and false
  // otherwise.
  virtual bool WriteLockNvram(uint32_t index) = 0;

  // Returns the reported size of the NVRAM space indicated by its index
  //
  // Parameters
  //   index - The index of the space
  // Returns the size of the space. If undefined or an error occurs, 0 is
  // returned.
  virtual unsigned int GetNvramSize(uint32_t index) = 0;

  // Get the endorsement public key. This method requires TPM owner privilege.
  //
  // Parameters
  //   ek_public_key - The EK public key in DER encoded form.
  //
  // Returns true on success.
  virtual TpmRetryAction GetEndorsementPublicKey(
      brillo::SecureBlob* ek_public_key) = 0;

  // Get the endorsement public key through the TPM delegate. This method
  // assumes the TPM is locked.
  //
  // Parameters
  //   ek_public_key - The EK public key in DER encoded form.
  virtual TpmRetryAction GetEndorsementPublicKeyWithDelegate(
      brillo::SecureBlob* ek_public_key,
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret) = 0;

  // Get the endorsement credential. This method requires TPM owner privilege.
  //
  // Parameters
  //   credential - The EK credential as it is stored in NVRAM.
  //
  // Returns true on success.
  virtual bool GetEndorsementCredential(brillo::SecureBlob* credential) = 0;

  // Creates an Attestation Identity Key (AIK). This method requires TPM owner
  // privilege.
  //
  // Parameters
  //   identity_public_key_der - The AIK public key in DER encoded form.
  //   identity_public_key - The AIK public key in serialized TPM_PUBKEY form.
  //   identity_key_blob - The AIK key in blob form.
  //   identity_binding - The EK-AIK binding (i.e. public key signature).
  //   identity_label - The label used to create the identity binding.
  //   pca_public_key - The public key of the temporary PCA used to create the
  //                    identity binding in serialized TPM_PUBKEY form.
  //   endorsement_credential - The endorsement credential.
  //   platform_credential - The platform credential.
  //   conformance_credential - The conformance credential.
  //
  // Returns true on success.
  virtual bool MakeIdentity(brillo::SecureBlob* identity_public_key_der,
                            brillo::SecureBlob* identity_public_key,
                            brillo::SecureBlob* identity_key_blob,
                            brillo::SecureBlob* identity_binding,
                            brillo::SecureBlob* identity_label,
                            brillo::SecureBlob* pca_public_key,
                            brillo::SecureBlob* endorsement_credential,
                            brillo::SecureBlob* platform_credential,
                            brillo::SecureBlob* conformance_credential) = 0;

  // Generates a quote of a given PCR with the given identity key.
  // - PCR0 is used to differentiate normal mode from developer mode.
  // - PCR1 is used on some systems to measure the HWID.
  //
  // Parameters
  //   pcr_index - The index of the PCR to be quoted.
  //   check_pcr_value - If set, checks if current PCR value is valid. If it's
  //                     invalid, returns false, in which case the caller
  //                     shouldn't use the output values. This flag is effective
  //                     only if pcr_index is 0 and if the TPM version is 1.2.
  //   identity_key_blob - The AIK blob, as provided by MakeIdentity.
  //   external_data - Data to be added to the quote, this must be at least 160
  //                   bits in length and only the first 160 bits will be used.
  //   pcr_value - The value of PCR0 at the time of quote. This is more reliable
  //               than separately reading the PCR value because it is not
  //               susceptible to race conditions.
  //   quoted_data - The exact data that was quoted (i.e. the TPM_QUOTE_INFO
  //                 structure), this can make verifying the quote easier.
  //   quote - The generated quote.
  //
  // Returns kSuccess on success, kInvalidPcrValue on a bad PCR value if
  // |check_pcr_value| is set, and kFailure for other errors.
  virtual QuotePcrResult QuotePCR(uint32_t pcr_index,
                                  bool check_pcr_value,
                                  const brillo::SecureBlob& identity_key_blob,
                                  const brillo::SecureBlob& external_data,
                                  brillo::Blob* pcr_value,
                                  brillo::SecureBlob* quoted_data,
                                  brillo::SecureBlob* quote) = 0;

  // Seals a secret to PCR0 with the SRK.
  //
  // Parameters
  //   value - The value to be sealed.
  //   sealed_value - The sealed value.
  //
  // Returns true on success.
  virtual bool SealToPCR0(const brillo::SecureBlob& value,
                          brillo::SecureBlob* sealed_value) = 0;

  // Unseals a secret previously sealed with the SRK.
  //
  // Parameters
  //   sealed_value - The sealed value.
  //   value - The original value.
  //
  // Returns true on success.
  virtual bool Unseal(const brillo::SecureBlob& sealed_value,
                      brillo::SecureBlob* value) = 0;

  // Creates a certified non-migratable signing key.
  //
  // Parameters
  //   identity_key_blob - The AIK key blob, as provided by MakeIdentity.
  //   certified_public_key - The certified public key in TPM_PUBKEY form.
  //   certified_public_key_der - The certified public key in DER encoded form.
  //   certified_key_blob - The certified key in blob form.
  //   certified_key_info - The key info that was signed (TPM_CERTIFY_INFO).
  //   certified_key_proof - The signature of the certified key info by the AIK.
  virtual bool CreateCertifiedKey(const brillo::SecureBlob& identity_key_blob,
                                  const brillo::SecureBlob& external_data,
                                  brillo::SecureBlob* certified_public_key,
                                  brillo::SecureBlob* certified_public_key_der,
                                  brillo::SecureBlob* certified_key_blob,
                                  brillo::SecureBlob* certified_key_info,
                                  brillo::SecureBlob* certified_key_proof) = 0;

  // Creates a TPM owner delegate for future use.
  //
  // Parameters
  //   bound_pcrs - Specifies the PCRs to which the delegate is bound.
  //   delegate_family_label - Specifies the label of the created delegate
  //                           family. Should be equal to
  //                           |kDefaultDelegateFamilyLabel| in most cases. Non-
  //                           default values are primarily intended for testing
  //                           purposes.
  //   delegate_label - Specifies the label of the created delegate. Should be
  //                    equal to |kDefaultDelegateLabel| in most cases. Non-
  //                    default values are primarily intended for testing
  //                    purposes.
  //   delegate_blob - The blob for the owner delegation.
  //   delegate_secret - The delegate secret that will be required to perform
  //                     privileged operations in the future.
  virtual bool CreateDelegate(const std::set<uint32_t>& bound_pcrs,
                              uint8_t delegate_family_label,
                              uint8_t delegate_label,
                              brillo::Blob* delegate_blob,
                              brillo::Blob* delegate_secret) = 0;

  // Activates an AIK by using the EK to decrypt the AIK credential.
  //
  // Parameters
  //
  //   delegate_blob - The delegate blob, as provided by CreateDelegate.
  //   delegate_secret - The secret to be used for delegate authorization.
  //   identity_key_blob - The AIK key blob, as provided by MakeIdentity.
  //   encrypted_asym_ca - Encrypted TPM_ASYM_CA_CONTENTS from the CA.
  //   encrypted_sym_ca - Encrypted TPM_SYM_CA_CONTENTS from the CA.
  //   identity_credential - The AIK credential created by the CA.
  virtual bool ActivateIdentity(const brillo::Blob& delegate_blob,
                                const brillo::Blob& delegate_secret,
                                const brillo::SecureBlob& identity_key_blob,
                                const brillo::SecureBlob& encrypted_asym_ca,
                                const brillo::SecureBlob& encrypted_sym_ca,
                                brillo::SecureBlob* identity_credential) = 0;

  // Signs data using the TPM_SS_RSASSAPKCS1v15_DER scheme.  This method will
  // work with any signing key that has been assigned this scheme.  This
  // includes all keys created using CreateCertifiedKey.
  //
  // Parameters
  //   key_blob - An SRK-wrapped private key blob.
  //   input - The value to be signed.
  //   bound_pcr_index - If the signing key used is a PCR bound key, this arg
  //                     is the pcr to which it was bound. Else it is
  //                     kNotBoundToPCR.
  //   signature - On success, will be populated with the signature.
  virtual bool Sign(const brillo::SecureBlob& key_blob,
                    const brillo::SecureBlob& input,
                    uint32_t bound_pcr_index,
                    brillo::SecureBlob* signature) = 0;

  // Creates an SRK-wrapped key that has both create attributes and usage policy
  // bound to the given |pcr_map| of pcr_index -> pcr_value. The usage is
  // restricted by |key_type|. On success returns true and populates |key_blob|
  // with the TPM private key blob and |public_key_der| with the DER-encoded
  // public key. |creation_blob| is an opaque blob that must be passed back as
  // an input into VerifyPCRBoundKey.
  virtual bool CreatePCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                                 AsymmetricKeyUsage key_type,
                                 brillo::SecureBlob* key_blob,
                                 brillo::SecureBlob* public_key_der,
                                 brillo::SecureBlob* creation_blob) = 0;

  // Returns true if the given |key_blob| represents a SRK-wrapped key which
  // has both create attributes and usage policy bound to |pcr_map| of
  // pcr_index -> pcr_value. |creation_blob| is the blob containing creation
  // data, that was generated by CreatePCRBoundKey.
  virtual bool VerifyPCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                                 const brillo::SecureBlob& key_blob,
                                 const brillo::SecureBlob& creation_blob) = 0;

  // Extends the PCR given by |pcr_index| with |extension|. The |extension| must
  // be exactly 20 bytes in length.
  virtual bool ExtendPCR(uint32_t pcr_index, const brillo::Blob& extension) = 0;

  // Reads the current |pcr_value| of the PCR given by |pcr_index|.
  virtual bool ReadPCR(uint32_t pcr_index, brillo::Blob* pcr_value) = 0;

  // Checks to see if the endorsement key is available by attempting to get its
  // public key
  virtual bool IsEndorsementKeyAvailable() = 0;

  // Attempts to create the endorsement key in the TPM
  virtual bool CreateEndorsementKey() = 0;

  // Attempts to take ownership of the TPM
  //
  // Parameters
  //   max_timeout_tries - The maximum number of attempts to make if the call
  //                       times out, which it may occasionally do
  virtual bool TakeOwnership(int max_timeout_tries,
                             const brillo::SecureBlob& owner_password) = 0;

  // Initializes the SRK by Zero-ing its password and unrestricting it.
  //
  // Parameters
  //   owner_password - The owner password for the TPM
  virtual bool InitializeSrk(const brillo::SecureBlob& owner_password) = 0;

  // Changes the owner password
  //
  // Parameters
  //   previous_owner_password - The previous owner password for the TPM
  //   owner_password - The owner password for the TPM
  virtual bool ChangeOwnerPassword(
      const brillo::SecureBlob& previous_owner_password,
      const brillo::SecureBlob& owner_password) = 0;

  // Test the TPM auth by calling Tspi_TPM_GetStatus
  //
  // Parameters
  //   owner_password - The owner password to use when getting the handle
  virtual bool TestTpmAuth(const brillo::SecureBlob& owner_password) = 0;

  // Sets the TPM owner password to be used in subsequent commands
  //
  // Parameters
  //   owner_password - The owner password for the TPM
  virtual void SetOwnerPassword(const brillo::SecureBlob& owner_password) = 0;

  // Returns true if |retry_action| represents a transient error.
  //
  // Parameters
  //   retry_action - The result of a performed action.
  static bool IsTransient(TpmRetryAction retry_action) {
    return !(retry_action == kTpmRetryNone ||
             retry_action == kTpmRetryFailNoRetry);
  }

  // Wrapps a provided RSA key with the TPM's Storage Root Key.
  //
  // Parameters
  //   public_modulus - the public modulus of the provided Rsa key
  //   prime_factor - one of the prime factors of the Rsa key to wrap
  //   wrapped_key (OUT) - A blob representing the wrapped key
  virtual bool WrapRsaKey(const brillo::SecureBlob& public_modulus,
                          const brillo::SecureBlob& prime_factor,
                          brillo::SecureBlob* wrapped_key) = 0;

  // Loads an SRK-wrapped key into the TPM.
  //
  // Parameters
  //   wrapped_key - The blob (as produced by WrapRsaKey).
  //   key_handle (OUT) - A handle to the key loaded into the TPM.
  virtual TpmRetryAction LoadWrappedKey(const brillo::SecureBlob& wrapped_key,
                                        ScopedKeyHandle* key_handle) = 0;

  // Loads the Cryptohome Key using a pre-defined UUID. This method does
  // nothing when using TPM2.0
  //
  // Parameters
  //   key_handle (OUT) - A handle to the key loaded into the TPM.
  //   key_blob (OUT) - If non-null, the blob representing this loaded key.
  virtual bool LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                                       brillo::SecureBlob* key_blob) = 0;

  // Closes the TPM state associated with the given |key_handle|.
  virtual void CloseHandle(TpmKeyHandle key_handle) = 0;

  // Gets the TPM status information. If there |context| and |key| are supplied,
  // they will be used in encryption/decryption test. They can be 0 to bypass
  // the test.
  //
  // Parameters
  //   key - The key to check for encryption/decryption
  //   status (OUT) - The TpmStatusInfo structure containing the results
  virtual void GetStatus(TpmKeyHandle key, TpmStatusInfo* status) = 0;

  // Returns whether the TPM SRK is vulnerable to the ROCA vulnerability.
  // An empty optional is returned when the result is unknown due to an error.
  // This method always returns |false| when using TPM 2.0.
  virtual base::Optional<bool> IsSrkRocaVulnerable() = 0;

  // Gets the current state of the dictionary attack logic. Returns false on
  // failure.
  virtual bool GetDictionaryAttackInfo(int* counter,
                                       int* threshold,
                                       bool* lockout,
                                       int* seconds_remaining) = 0;

  // Resets DA lock. This call requires owner permissions. For TPM 1.2,
  // |delegate_blob| and |delegate_secret| for an owner delegate must be
  // provided. For TPM 2.0, everything is handled in tpm_managerd and those 2
  // args are unused.
  virtual bool ResetDictionaryAttackMitigation(
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret) = 0;

  // For TPMs with updateable firmware: Declate the current firmware
  // version stable and invalidate previous versions, if any.
  // For TPMs with fixed firmware: NOP.
  virtual void DeclareTpmFirmwareStable() = 0;

  // Performs TPM-specific actions to remove the specified |dependency| on
  // retaining the TPM owner password. When all dependencies have been removed
  // the owner password can be cleared.
  // Returns true if the dependency has been successfully removed or was
  // already removed by the time this function is called.
  virtual bool RemoveOwnerDependency(
      TpmPersistentState::TpmOwnerDependency dependency) = 0;

  // Clears the stored owner password.
  // Returns true if the password is cleared by this method, or was already
  // clear when we called it.
  virtual bool ClearStoredPassword() = 0;

  // Obtains version information from the TPM.
  virtual bool GetVersionInfo(TpmVersionInfo* version_info) = 0;

  // Obtains field upgrade status for IFX TPMs.
  virtual bool GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) = 0;

  // For TPMs that require it, performs TPM-specific actions when the type of
  // the signed-in user changes (owner vs non-owner/no-user).
  // Returns true if successful or no action is needed (see below).
  // In Cr50 case: Enables/disables changing CCD password.
  // For other TPMs: Does nothing, always returns TPM_RC_SUCCESS. These TPMs
  // have no notion of restricting the CCD password and don't need a signal to
  // lock things down at login.
  virtual bool SetUserType(Tpm::UserType type) = 0;

  // Obtains RSU device id from TPM for the Remote Server Unlock process.
  // Due to the privacy reasons, this id must not be used for any purpose other
  // than verifying RMA unlock eligibility.
  virtual bool GetRsuDeviceId(std::string* device_id) = 0;

  // Get a pointer to the LECredentialBackend object, which is used to call the
  // relevant TPM commands necessary to implement Low Entropy (LE) credential
  // protection.
  //
  // If the Tpm implementation does not support LE credential handling,
  // this function will return a nullptr.
  virtual LECredentialBackend* GetLECredentialBackend() = 0;

  // Get a pointer to the SignatureSealingBackend object, which is used for
  // performing signature-sealing operations. Returns nullptr if the
  // implementation does not support signature-sealing operations.
  virtual SignatureSealingBackend* GetSignatureSealingBackend() = 0;

  // Callback function that is called after ownership is taken, regardless of
  // how we know that ownership is taken. (i.e. signal from tpm_managerd,
  // ownership taken in cryptohomed.)
  virtual void HandleOwnershipTakenEvent() = 0;

  // Gets owner auth delegate. Returns |true| iff the operation succeeds. Once
  // returning |true|, |blob| and |secret| are set to the blob and secret of
  // owner auth delegate, respectively. |has_reset_lock_permissions| indicates
  // whether the delegate has permissions to call |TPM_ResetLockValue|.
  //
  // For TPM2.0, the implementaion could be an exception, which is returning
  // |true| without the output data. The definition of this interface would
  // therefore be a little bit awkward because this interface should be private
  // to TPM1.2. Yet, due to the current design of |ServiceDistributed| we need
  // to expose this interface for now. Need to remove the all the delegate
  // interace at once after proper refactoring.
  virtual bool GetDelegate(brillo::Blob* blob,
                           brillo::Blob* secret,
                           bool* has_reset_lock_permissions) = 0;

  // Returns |true| if any function is implemented utiltizing the
  // |tpm_manager::LocalData| coming from |tpm_managerd|, and optionally nvram
  // related functions could also use |tpm_managerd|.
  virtual bool DoesUseTpmManager() = 0;

  // Returns if current PCR0 value is valid, i.e., hasn't been extended multiple
  // times.
  //
  // This function does the check for TPM 1.2 only. For TPM 2.0, it always
  // returns true;
  virtual bool IsCurrentPCR0ValueValid() = 0;

  // Returns whether the owner auth delegate set by is bound to PCR.
  virtual base::Optional<bool> IsDelegateBoundToPcr() = 0;

  // Returns whether the owner auth delegate set by has reset lock permissions.
  virtual bool DelegateCanResetDACounter() = 0;

  // Returns the map with expected PCR values for the user.
  // TODO(crbug.com/1065907): The username and use_extended_pcr params do not
  // belong in the tpm class layer, and long term should be refactored out. See
  // bug.
  virtual std::map<uint32_t, std::string> GetPcrMap(
      const std::string& obfuscated_username, bool use_extended_pcr) const = 0;

 private:
  static Tpm* singleton_;
  static base::Lock singleton_lock_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_H_
