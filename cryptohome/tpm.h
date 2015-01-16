// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tpm - class for performing encryption/decryption in the TPM.  For cryptohome,
// the TPM may be used as a way to strengthen the security of the wrapped vault
// keys stored on disk.  When the TPM is enabled, there is a system-wide
// cryptohome RSA key that is used during the encryption/decryption of these
// keys.
// TODO(wad) make more functions virtual for use in mock_tpm.h.

#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <base/synchronization/lock.h>
#include <chromeos/secure_blob.h>
#include <openssl/rsa.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha) - needs tss.h

#include "tpm_status.pb.h"  // NOLINT(build/include)

#ifndef CRYPTOHOME_TPM_H_
#define CRYPTOHOME_TPM_H_

namespace cryptohome {

class Tpm {
 public:
  enum TpmRetryAction {
    RetryNone,
    RetryCommFailure,
    RetryDefendLock,
    Fatal,
    RetryReboot
  };

  struct TpmStatusInfo {
    TSS_RESULT LastTpmError;
    bool CanConnect;
    bool CanLoadSrk;
    bool CanLoadSrkPublicKey;
    bool HasCryptohomeKey;
    bool CanEncrypt;
    bool CanDecrypt;
    bool ThisInstanceHasContext;
    bool ThisInstanceHasKeyHandle;
  };

  static Tpm* GetSingleton();

  virtual ~Tpm();

  // Encrypts a data blob using the provided RSA key
  //
  // Parameters
  //   context_handle - The TPM context
  //   key_handle - The loaded TPM key handle
  //   plaintext - One RSA message to encrypt
  //   key - AES key to encrypt with
  //   ciphertext (OUT) - Encrypted blob
  //   result (OUT) - TPM error code
  virtual bool EncryptBlob(TSS_HCONTEXT context_handle,
                           TSS_HKEY key_handle,
                           const chromeos::SecureBlob& plaintext,
                           const chromeos::SecureBlob& key,
                           chromeos::SecureBlob* ciphertext,
                           TSS_RESULT* result);

  // Decrypts a data blob using the provided RSA key
  //
  // Parameters
  //   context_handle - The TPM context
  //   key_handle - The loaded TPM key handle
  //   ciphertext - One RSA message to encrypt
  //   key - AES key to encrypt with
  //   plaintext (OUT) - Decrypted blob
  //   result (OUT) - TPM error code
  virtual bool DecryptBlob(TSS_HCONTEXT context_handle,
                           TSS_HKEY key_handle,
                           const chromeos::SecureBlob& ciphertext,
                           const chromeos::SecureBlob& key,
                           chromeos::SecureBlob* plaintext,
                           TSS_RESULT* result);

  // Retrieves the sha1sum of the public key component of the RSA key
  virtual TpmRetryAction GetPublicKeyHash(TSS_HCONTEXT context_handle,
                                          TSS_HKEY key_handle,
                                          chromeos::SecureBlob* hash);

  // Returns the owner password if this instance was used to take ownership.
  // This will only occur when the TPM is unowned, which will be on OOBE
  //
  // Parameters
  //   owner_password (OUT) - The random owner password used
  virtual bool GetOwnerPassword(chromeos::Blob* owner_password);


  // Returns whether or not the TPM is enabled.  This method call returns a
  // cached result because querying the TPM directly will block if ownership is
  // currently being taken (such as on a separate thread).
  virtual bool IsEnabled() const { return !is_disabled_; }
  virtual void SetIsEnabled(bool enabled) { is_disabled_ = !enabled; }

  // Returns whether or not the TPM is owned.  This method call returns a cached
  // result because querying the TPM directly will block if ownership is
  // currently being taken (such as on a separate thread).
  virtual bool IsOwned() const { return is_owned_; }
  virtual void SetIsOwned(bool owned) { is_owned_ = owned; }

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
  bool PerformEnabledOwnedCheck(bool* enabled, bool* owned);

  // Returns whether or not this instance has been setup'd by an external
  // entity (such as cryptohome::TpmInit).
  virtual bool IsInitialized() const { return initialized_; }
  virtual void SetIsInitialized(bool done) { initialized_ = done; }

  // Returns whether or not the TPM is being owned
  virtual bool IsBeingOwned() const { return is_being_owned_; }
  virtual void SetIsBeingOwned(bool value) { is_being_owned_ = value; }

  // Runs the TPM initialization sequence.  This may take a long time due to the
  // call to Tspi_TPM_TakeOwnership.
  bool InitializeTpm(bool* OUT_took_ownership);

  // Gets random bytes from the TPM
  //
  // Parameters
  //   length - The number of bytes to get
  //   data (OUT) - The random data from the TPM
  virtual bool GetRandomData(size_t length, chromeos::Blob* data);

  // Creates a lockable NVRAM space in the TPM
  //
  // Parameters
  //   index - The index of the space
  //   length - The number of bytes to allocate
  // TODO(wad) Add PCR compositing via flags.
  // TODO(wad) Should this just be a factory for a TpmNvram class?
  // Returns false if the index or length invalid or the required
  // authorization is not possible.
  virtual bool DefineLockOnceNvram(uint32_t index,
                                   size_t length);

  // Destroys a defined NVRAM space
  //
  // Parameters
  //  index - The index of the space to destroy
  // Returns false if the index is invalid or the required authorization
  // is not possible.
  virtual bool DestroyNvram(uint32_t index);

  // Writes the given blob to NVRAM
  //
  // Parameters
  //  index - The index of the space to write
  //  blob - the data to write (size==0 may be used for locking)
  // Returns false if the index is invalid or the request lacks the required
  // authorization.
  virtual bool WriteNvram(uint32_t index, const chromeos::SecureBlob& blob);

  // Reads from the NVRAM index to the given blob
  //
  // Parameters
  //  index - The index of the space to write
  //  blob - the data to read
  // Returns false if the index is invalid or the request lacks the required
  // authorization.
  virtual bool ReadNvram(uint32_t index, chromeos::SecureBlob* blob);

  // Determines if the given index is defined in the TPM
  //
  // Parameters
  //  index - The index of the space
  // Returns true if it exists and false if it doesn't or there is a failure to
  // communicate with the TPM.
  virtual bool IsNvramDefined(uint32_t index);

  // Determines if the NVRAM space at the given index is bWriteDefine locked
  //
  // Parameters
  //   index - The index of the space
  // Returns true if locked and false if it is unlocked, the space does not
  // exist, or there is a TPM-related error.
  virtual bool IsNvramLocked(uint32_t index);

  // Returns the reported size of the NVRAM space indicated by its index
  //
  // Parameters
  //   index - The index of the space
  // Returns the size of the space. If undefined or an error occurs, 0 is
  // returned.
  virtual unsigned int GetNvramSize(uint32_t index);

  void set_srk_auth(const chromeos::SecureBlob& value) {
    srk_auth_.resize(value.size());
    memcpy(srk_auth_.data(), value.const_data(), srk_auth_.size());
  }

  // Get the endorsement public key. This method requires TPM owner privilege.
  //
  // Parameters
  //   ek_public_key - The EK public key in DER encoded form.
  //
  // Returns true on success.
  virtual bool GetEndorsementPublicKey(chromeos::SecureBlob* ek_public_key);

  // Get the endorsement credential. This method requires TPM owner privilege.
  //
  // Parameters
  //   credential - The EK credential as it is stored in NVRAM.
  //
  // Returns true on success.
  virtual bool GetEndorsementCredential(chromeos::SecureBlob* credential);

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
  virtual bool MakeIdentity(chromeos::SecureBlob* identity_public_key_der,
                            chromeos::SecureBlob* identity_public_key,
                            chromeos::SecureBlob* identity_key_blob,
                            chromeos::SecureBlob* identity_binding,
                            chromeos::SecureBlob* identity_label,
                            chromeos::SecureBlob* pca_public_key,
                            chromeos::SecureBlob* endorsement_credential,
                            chromeos::SecureBlob* platform_credential,
                            chromeos::SecureBlob* conformance_credential);

  // Generates a quote of a given PCR with the given identity key.
  // - PCR0 is used to differentiate normal mode from developer mode.
  // - PCR1 is used on some systems to measure the HWID.
  //
  // Parameters
  //   pcr_index - The index of the PCR to be quoted.
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
  // Returns true on success.
  virtual bool QuotePCR(int pcr_index,
                        const chromeos::SecureBlob& identity_key_blob,
                        const chromeos::SecureBlob& external_data,
                        chromeos::SecureBlob* pcr_value,
                        chromeos::SecureBlob* quoted_data,
                        chromeos::SecureBlob* quote);

  // Seals a secret to PCR0 with the SRK.
  //
  // Parameters
  //   value - The value to be sealed.
  //   sealed_value - The sealed value.
  //
  // Returns true on success.
  virtual bool SealToPCR0(const chromeos::Blob& value,
                          chromeos::Blob* sealed_value);

  // Unseals a secret previously sealed with the SRK.
  //
  // Parameters
  //   sealed_value - The sealed value.
  //   value - The original value.
  //
  // Returns true on success.
  virtual bool Unseal(const chromeos::Blob& sealed_value,
                      chromeos::Blob* value);

  // Creates a certified non-migratable signing key.
  //
  // Parameters
  //   identity_key_blob - The AIK key blob, as provided by MakeIdentity.
  //   certified_public_key - The certified public key in TPM_PUBKEY form.
  //   certified_public_key_der - The certified public key in DER encoded form.
  //   certified_key_blob - The certified key in blob form.
  //   certified_key_info - The key info that was signed (TPM_CERTIFY_INFO).
  //   certified_key_proof - The signature of the certified key info by the AIK.
  virtual bool CreateCertifiedKey(
      const chromeos::SecureBlob& identity_key_blob,
      const chromeos::SecureBlob& external_data,
      chromeos::SecureBlob* certified_public_key,
      chromeos::SecureBlob* certified_public_key_der,
      chromeos::SecureBlob* certified_key_blob,
      chromeos::SecureBlob* certified_key_info,
      chromeos::SecureBlob* certified_key_proof);

  // Creates a TPM owner delegate for future use.
  //
  // Parameters
  //   identity_key_blob - The AIK key blob, as provided by MakeIdentity.
  //   delegate_blob - The blob for the owner delegation.
  //   delegate_secret - The delegate secret that will be required to perform
  //                     privileged operations in the future.
  virtual bool CreateDelegate(const chromeos::SecureBlob& identity_key_blob,
                              chromeos::SecureBlob* delegate_blob,
                              chromeos::SecureBlob* delegate_secret);

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
  virtual bool ActivateIdentity(const chromeos::SecureBlob& delegate_blob,
                                const chromeos::SecureBlob& delegate_secret,
                                const chromeos::SecureBlob& identity_key_blob,
                                const chromeos::SecureBlob& encrypted_asym_ca,
                                const chromeos::SecureBlob& encrypted_sym_ca,
                                chromeos::SecureBlob* identity_credential);

  // Encrypts data in a TSS compatible way using AES-256-CBC.
  //
  // Parameters
  //   key - The AES key.
  //   input - The data to be encrypted.
  //   output - The encrypted data.
  virtual bool TssCompatibleEncrypt(const chromeos::SecureBlob& key,
                                    const chromeos::SecureBlob& input,
                                    chromeos::SecureBlob* output);

  // Encrypts data using the TPM_ES_RSAESOAEP_SHA1_MGF1 scheme.
  //
  // Parameters
  //   key - The RSA public key.
  //   input - The data to be encrypted.
  //   output - The encrypted data.
  virtual bool TpmCompatibleOAEPEncrypt(RSA* key,
                                        const chromeos::SecureBlob& input,
                                        chromeos::SecureBlob* output);

  // Signs data using the TPM_SS_RSASSAPKCS1v15_DER scheme.  This method will
  // work with any signing key that has been assigned this scheme.  This
  // includes all keys created using CreateCertifiedKey.
  //
  // Parameters
  //   key_blob - An SRK-wrapped private key blob.
  //   der_encoded_input - The value to be signed, encoded as required by the
  //                       TPM_SS_RSASSAPKCS1v15_DER scheme.
  //   signature - On success, will be populated with the signature.
  virtual bool Sign(const chromeos::SecureBlob& key_blob,
                    const chromeos::SecureBlob& der_encoded_input,
                    chromeos::SecureBlob* signature);

  // Creates an SRK-wrapped signing key that has both create attributes and
  // usage policy bound to the given |pcr_index| and |pcr_value|.  On success
  // returns true and populates |key_blob| with the TPM private key blob and
  // |public_key_der| with the DER-encoded public key.
  virtual bool CreatePCRBoundKey(int pcr_index,
                                 const chromeos::SecureBlob& pcr_value,
                                 chromeos::SecureBlob* key_blob,
                                 chromeos::SecureBlob* public_key_der);

  // Returns true iff the given |key_blob| represents a SRK-wrapped key which
  // has both create attributes and usage policy bound to |pcr_value| for
  // |pcr_index|.
  virtual bool VerifyPCRBoundKey(int pcr_index,
                                 const chromeos::SecureBlob& pcr_value,
                                 const chromeos::SecureBlob& key_blob);

  // Extends the PCR given by |pcr_index| with |extension|. The |extension| must
  // be exactly 20 bytes in length.
  virtual bool ExtendPCR(int pcr_index, const chromeos::SecureBlob& extension);

  // Reads the current |pcr_value| of the PCR given by |pcr_index|.
  virtual bool ReadPCR(int pcr_index, chromeos::SecureBlob* pcr_value);

  bool OpenAndConnectTpm(TSS_HCONTEXT* context_handle, TSS_RESULT* result);

  // Tries to connect to the TPM
  virtual TSS_HCONTEXT ConnectContext();

  // Frees up the context. This is different from Disconnect() method. This
  // version does not touch on resources owned by this TPM instance.
  virtual void CloseContext(TSS_HCONTEXT context_handle) const;

  // Checks to see if the endorsement key is available by attempting to get its
  // public key
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  bool IsEndorsementKeyAvailable(TSS_HCONTEXT context_handle);

  // Attempts to create the endorsement key in the TPM
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  bool CreateEndorsementKey(TSS_HCONTEXT context_handle);

  // Attempts to take ownership of the TPM
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   max_timeout_tries - The maximum number of attempts to make if the call
  //                       times out, which it may occasionally do
  bool TakeOwnership(TSS_HCONTEXT context_handle, int max_timeout_tries,
                     const chromeos::SecureBlob& owner_password);

  // Zeros the SRK password (sets it to an empty string)
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   owner_password - The owner password for the TPM
  bool ZeroSrkPassword(TSS_HCONTEXT context_handle,
                       const chromeos::SecureBlob& owner_password);

  // Removes usage restrictions on the SRK
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   owner_password - The owner password for the TPM
  bool UnrestrictSrk(TSS_HCONTEXT context_handle,
                     const chromeos::SecureBlob& owner_password);

  // Changes the owner password
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   previous_owner_password - The previous owner password for the TPM
  //   owner_password - The owner password for the TPM
  bool ChangeOwnerPassword(TSS_HCONTEXT context_handle,
                           const chromeos::SecureBlob& previous_owner_password,
                           const chromeos::SecureBlob& owner_password);

  // Gets a handle to the TPM from the specified context with the given owner
  // password
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   owner_password - The owner password to use when getting the handle
  //   tpm_handle (OUT) - The handle for the TPM on success
  bool GetTpmWithAuth(TSS_HCONTEXT context_handle,
                      const chromeos::SecureBlob& owner_password,
                      TSS_HTPM* tpm_handle);

  // Test the TPM auth by calling Tspi_TPM_GetStatus
  //
  // Parameters
  //   tpm_handle = The TPM handle
  bool TestTpmAuth(TSS_HTPM tpm_handle);

  // Sets the TPM owner password to be used in subsequent commands
  //
  // Parameters
  //   owner_password - The owner password for the TPM
  void SetOwnerPassword(const chromeos::SecureBlob& owner_password);

  // Gets a handle to the SRK
  bool LoadSrk(TSS_HCONTEXT context_handle, TSS_HKEY* srk_handle,
               TSS_RESULT* result) const;

  bool IsTransient(TSS_RESULT result);

  bool GetPublicKeyBlob(TSS_HCONTEXT context_handle,
                        TSS_HKEY key_handle,
                        chromeos::SecureBlob* data_out,
                        TSS_RESULT* result) const;

  bool GetKeyBlob(TSS_HCONTEXT context_handle, TSS_HKEY key_handle,
                  chromeos::SecureBlob* data_out, TSS_RESULT* result) const;

  // Creates an RSA key wrapped by the TPM's Storage Root Key. The key is
  // created by OpenSSL, and not by the TPM.
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   wrapped_key (OUT) - A blob representing the wrapped key
  bool CreateWrappedRsaKey(TSS_HCONTEXT context_handle,
                           chromeos::SecureBlob* wrapped_key);

  // Loads an SRK-wrapped key into the TPM.
  //
  // Parameters
  //   context_handle - The context handle for the TPM session.
  //   wrapped_key - The blob (as produced by CreateWrappedRsaKey).
  //   key_handle (OUT) - A handle to the key loaded into the TPM.
  bool LoadWrappedKey(TSS_HCONTEXT context_handle,
                      const chromeos::SecureBlob& wrapped_key,
                      TSS_HKEY* key_handle,
                      TSS_RESULT* result) const;

  // Loads a key by well-known UUID.
  //
  // Parameters
  //   context_handle - The context handle for the TPM session.
  //   key_uuid - The well-known UUID identifying the TPM key.
  //   key_handle (OUT) - A handle to the key loaded into the TPM.
  //   key_blob (OUT) - If non-null, the blob representing this loaded key.
  bool LoadKeyByUuid(TSS_HCONTEXT context_handle,
                     TSS_UUID key_uuid,
                     TSS_HKEY* key_handle,
                     chromeos::SecureBlob* key_blob,
                     TSS_RESULT* result) const;

  TpmRetryAction HandleError(TSS_RESULT result);

  // Gets the TPM status information. If there |context| and |key| are supplied,
  // they will be used in encryption/decryption test. They can be 0 to bypass
  // the test.
  //
  // Parameters
  //   context - The TPM context to check for encryption/decryption
  //   key - The key to check for encryption/decryption
  //   status (OUT) - The TpmStatusInfo structure containing the results
  void GetStatus(TSS_HCONTEXT context,
                 TSS_HKEY key,
                 Tpm::TpmStatusInfo* status);

  // Gets the current state of the dictionary attack logic. Returns false on
  // failure.
  bool GetDictionaryAttackInfo(int* counter,
                               int* threshold,
                               bool* lockout,
                               int* seconds_remaining);

  // Requires owner permissions so a |delegate_blob| and |delegate_secret| for
  // an owner delegate must be provided.
  bool ResetDictionaryAttackMitigation(
      const chromeos::SecureBlob& delegate_blob,
      const chromeos::SecureBlob& delegate_secret);

 protected:
  // Default constructor
  Tpm();

 private:
  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the owner password is available and
  // authorization is successfully acquired.
  bool ConnectContextAsOwner(TSS_HCONTEXT* context_handle,
                             TSS_HTPM* tpm_handle);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object authorized by the given delegation.
  bool ConnectContextAsDelegate(const chromeos::SecureBlob& delegate_blob,
                                const chromeos::SecureBlob& delegate_secret,
                                TSS_HCONTEXT* context, TSS_HTPM* tpm);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the context can be created and a TPM object
  // exists in the TSS.
  bool ConnectContextAsUser(TSS_HCONTEXT* context_handle,
                            TSS_HTPM* tpm_handle);

  // Returns the size of the specified NVRAM space.
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   index - NVRAM Space index
  // Returns -1 if the index, handle, or space is invalid.
  unsigned int GetNvramSizeForContext(TSS_HCONTEXT context_handle,
                                      TSS_HTPM tpm_handle,
                                      uint32_t index);

  // Returns if an Nvram space exists using the given context.
  bool IsNvramDefinedForContext(TSS_HCONTEXT context_handle,
                                TSS_HTPM tpm_handle,
                                uint32_t index);

  // Returns if bWriteDefine is true for a given NVRAM space using the given
  // context.
  bool IsNvramLockedForContext(TSS_HCONTEXT context_handle,
                               TSS_HTPM tpm_handle,
                               uint32_t index);

  // Reads an NVRAM space using the given context.
  bool ReadNvramForContext(TSS_HCONTEXT context_handle,
                           TSS_HTPM tpm_handle,
                           TSS_HPOLICY policy_handle,
                           uint32_t index,
                           chromeos::SecureBlob* blob);

  // Gets a handle to the TPM from the specified context
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   tpm_handle (OUT) - The handle for the TPM on success
  bool GetTpm(TSS_HCONTEXT context_handle, TSS_HTPM* tpm_handle);

  // Gets a handle to the TPM from the specified context with the given
  // delegation.
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   delegate_blob - The delegate blob to use when getting the handle
  //   delegate_secret - The delegate secret to use when getting the handle
  //   tpm_handle (OUT) - The handle for the TPM on success
  bool GetTpmWithDelegation(TSS_HCONTEXT context_handle,
                            const chromeos::SecureBlob& delegate_blob,
                            const chromeos::SecureBlob& delegate_secret,
                            TSS_HTPM* tpm_handle);

  // Decrypts and parses an identity request.
  //
  // Parameters
  //   pca_key - The private key of the Privacy CA.
  //   request - The identity request data.
  //   identityBinding - The EK-AIK binding (i.e. public key signature).
  //   endorsementCredential - The endorsement credential.
  //   platformCredential - The platform credential.
  //   conformanceCredential - The conformance credential.
  bool DecryptIdentityRequest(RSA* pca_key, const chromeos::SecureBlob& request,
                              chromeos::SecureBlob* identity_binding,
                              chromeos::SecureBlob* endorsement_credential,
                              chromeos::SecureBlob* platform_credential,
                              chromeos::SecureBlob* conformance_credential);

  // Creates a DER encoded RSA public key given a serialized TPM_PUBKEY.
  //
  // Parameters
  //   public_key - A serialized TPM_PUBKEY as returned by Tspi_Key_GetPubKey.
  //   public_key_der - The same public key in DER encoded form.
  bool ConvertPublicKeyToDER(const chromeos::SecureBlob& public_key,
                             chromeos::SecureBlob* public_key_der);

  // Obscure an RSA message by encrypting part of it.
  // The TPM could _in theory_ produce an RSA message (as a response from Bind)
  // that contains a header of a known format. If it did, and we encrypted the
  // whole message with a passphrase-derived AES key, then one could test
  // passphrase correctness by trial-decrypting the header. Instead, encrypt
  // only part of the message, and hope the part we encrypt is part of the RSA
  // message.
  //
  // In practice, this never makes any difference, because no TPM does that; the
  // result is always a bare PKCS1.5-padded RSA-encrypted message, which is
  // (as far as the author knows, although no proof is known) indistinguishable
  // from random data, and hence the attack this would protect against is
  // infeasible.
  //
  // Have a look at tpm.cc for the gory details.
  bool ObscureRSAMessage(const chromeos::SecureBlob& plaintext,
                         const chromeos::SecureBlob& key,
                         chromeos::SecureBlob* ciphertext);
  bool UnobscureRSAMessage(const chromeos::SecureBlob& ciphertext,
                           const chromeos::SecureBlob& key,
                           chromeos::SecureBlob* plaintext);

  // Wrapper for Tspi_GetAttribData.
  bool GetDataAttribute(TSS_HCONTEXT context,
                        TSS_HOBJECT object,
                        TSS_FLAG flag,
                        TSS_FLAG sub_flag,
                        chromeos::SecureBlob* data) const;

  // Wrapper for Tspi_TPM_GetCapability. If |data| is not NULL, the raw
  // capability data will be assigned. If |value| is not NULL, the capability
  // data must be exactly 4 bytes and it will be decoded into |value|.
  bool GetCapability(TSS_HCONTEXT context_handle,
                     TSS_HTPM tpm_handle,
                     UINT32 capability,
                     UINT32 sub_capability,
                     chromeos::Blob* data,
                     UINT32* value) const;

  // Member variables
  bool initialized_;
  chromeos::SecureBlob srk_auth_;

  // If TPM ownership is taken, owner_password_ contains the password used
  chromeos::SecureBlob owner_password_;

  // Used to provide thread-safe access to owner_password_, as it is set in the
  // initialization background thread.
  base::Lock password_sync_lock_;

  // Indicates if the TPM is disabled
  bool is_disabled_;

  // Indicates if the TPM is owned
  bool is_owned_;

  // Indicates if the TPM is being owned
  bool is_being_owned_;

  static Tpm* singleton_;
  static base::Lock singleton_lock_;

  DISALLOW_COPY_AND_ASSIGN(Tpm);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_H_
