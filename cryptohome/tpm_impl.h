// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_IMPL_H_
#define CRYPTOHOME_TPM_IMPL_H_

#include "cryptohome/tpm.h"

#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha)

namespace cryptohome {

class TpmImpl : public Tpm {
 public:
  TpmImpl();
  virtual ~TpmImpl();
  // Tpm methods
  TpmRetryAction EncryptBlob(TpmKeyHandle key_handle,
                             const chromeos::SecureBlob& plaintext,
                             const chromeos::SecureBlob& key,
                             chromeos::SecureBlob* ciphertext) override;
  TpmRetryAction DecryptBlob(TpmKeyHandle key_handle,
                             const chromeos::SecureBlob& ciphertext,
                             const chromeos::SecureBlob& key,
                             chromeos::SecureBlob* plaintext) override;
  TpmRetryAction GetPublicKeyHash(TpmKeyHandle key_handle,
                                  chromeos::SecureBlob* hash) override;
  bool GetOwnerPassword(chromeos::Blob* owner_password) override;
  bool IsEnabled() const override { return !is_disabled_; }
  void SetIsEnabled(bool enabled) override { is_disabled_ = !enabled; }
  bool IsOwned() const override { return is_owned_; }
  void SetIsOwned(bool owned) override { is_owned_ = owned; }
  bool PerformEnabledOwnedCheck(bool* enabled, bool* owned) override;
  bool IsInitialized() const override { return initialized_; }
  void SetIsInitialized(bool done) override { initialized_ = done; }
  bool IsBeingOwned() const override { return is_being_owned_; }
  void SetIsBeingOwned(bool value) override { is_being_owned_ = value; }
  bool GetRandomData(size_t length, chromeos::Blob* data) override;
  bool DefineLockOnceNvram(uint32_t index, size_t length) override;
  bool DestroyNvram(uint32_t index) override;
  bool WriteNvram(uint32_t index, const chromeos::SecureBlob& blob) override;
  bool ReadNvram(uint32_t index, chromeos::SecureBlob* blob) override;
  bool IsNvramDefined(uint32_t index) override;
  bool IsNvramLocked(uint32_t index) override;
  unsigned int GetNvramSize(uint32_t index) override;
  bool GetEndorsementPublicKey(chromeos::SecureBlob* ek_public_key) override;
  bool GetEndorsementCredential(chromeos::SecureBlob* credential) override;
  bool MakeIdentity(chromeos::SecureBlob* identity_public_key_der,
                    chromeos::SecureBlob* identity_public_key,
                    chromeos::SecureBlob* identity_key_blob,
                    chromeos::SecureBlob* identity_binding,
                    chromeos::SecureBlob* identity_label,
                    chromeos::SecureBlob* pca_public_key,
                    chromeos::SecureBlob* endorsement_credential,
                    chromeos::SecureBlob* platform_credential,
                    chromeos::SecureBlob* conformance_credential) override;
  bool QuotePCR(int pcr_index,
                const chromeos::SecureBlob& identity_key_blob,
                const chromeos::SecureBlob& external_data,
                chromeos::SecureBlob* pcr_value,
                chromeos::SecureBlob* quoted_data,
                chromeos::SecureBlob* quote) override;
  bool SealToPCR0(const chromeos::Blob& value,
                  chromeos::Blob* sealed_value) override;
  bool Unseal(const chromeos::Blob& sealed_value,
              chromeos::Blob* value) override;
  bool CreateCertifiedKey(
      const chromeos::SecureBlob& identity_key_blob,
      const chromeos::SecureBlob& external_data,
      chromeos::SecureBlob* certified_public_key,
      chromeos::SecureBlob* certified_public_key_der,
      chromeos::SecureBlob* certified_key_blob,
      chromeos::SecureBlob* certified_key_info,
      chromeos::SecureBlob* certified_key_proof) override;
  bool CreateDelegate(const chromeos::SecureBlob& identity_key_blob,
                      chromeos::SecureBlob* delegate_blob,
                      chromeos::SecureBlob* delegate_secret) override;
  bool ActivateIdentity(const chromeos::SecureBlob& delegate_blob,
                        const chromeos::SecureBlob& delegate_secret,
                        const chromeos::SecureBlob& identity_key_blob,
                        const chromeos::SecureBlob& encrypted_asym_ca,
                        const chromeos::SecureBlob& encrypted_sym_ca,
                        chromeos::SecureBlob* identity_credential) override;
  bool Sign(const chromeos::SecureBlob& key_blob,
            const chromeos::SecureBlob& der_encoded_input,
            chromeos::SecureBlob* signature) override;
  bool CreatePCRBoundKey(int pcr_index,
                         const chromeos::SecureBlob& pcr_value,
                         chromeos::SecureBlob* key_blob,
                         chromeos::SecureBlob* public_key_der) override;
  bool VerifyPCRBoundKey(int pcr_index,
                         const chromeos::SecureBlob& pcr_value,
                         const chromeos::SecureBlob& key_blob) override;
  bool ExtendPCR(int pcr_index, const chromeos::SecureBlob& extension) override;
  bool ReadPCR(int pcr_index, chromeos::SecureBlob* pcr_value) override;
  bool IsEndorsementKeyAvailable() override;
  bool CreateEndorsementKey() override;
  bool TakeOwnership(int max_timeout_tries,
                     const chromeos::SecureBlob& owner_password) override;
  bool InitializeSrk(const chromeos::SecureBlob& owner_password) override;
  bool ChangeOwnerPassword(const chromeos::SecureBlob& previous_owner_password,
                           const chromeos::SecureBlob& owner_password) override;
  bool TestTpmAuth(const chromeos::SecureBlob& owner_password) override;
  void SetOwnerPassword(const chromeos::SecureBlob& owner_password) override;
  bool IsTransient(TpmRetryAction retry_action) override;
  bool CreateWrappedRsaKey(chromeos::SecureBlob* wrapped_key) override;
  TpmRetryAction LoadWrappedKey(const chromeos::SecureBlob& wrapped_key,
                                ScopedKeyHandle* key_handle) override;
  bool LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                               chromeos::SecureBlob* key_blob) override;
  void CloseHandle(TpmKeyHandle key_handle) override;
  void GetStatus(TpmKeyHandle key, TpmStatusInfo* status) override;
  bool GetDictionaryAttackInfo(int* counter,
                               int* threshold,
                               bool* lockout,
                               int* seconds_remaining) override;
  bool ResetDictionaryAttackMitigation(
      const chromeos::SecureBlob& delegate_blob,
      const chromeos::SecureBlob& delegate_secret) override;

 private:
  // Connects to the TPM and return its context at |context_handle|.
  bool OpenAndConnectTpm(TSS_HCONTEXT* context_handle,
                         TSS_RESULT* result);

  // Gets the Public Key blob associated with |key_handle|.
  bool GetPublicKeyBlob(TSS_HCONTEXT context_handle,
                        TSS_HKEY key_handle,
                        chromeos::SecureBlob* data_out,
                        TSS_RESULT* result) const;

  // Gets the key blob associated with |key_handle|.
  bool GetKeyBlob(TSS_HCONTEXT context_handle,
                  TSS_HKEY key_handle,
                  chromeos::SecureBlob* data_out,
                  TSS_RESULT* result) const;

  // Gets a handle to the SRK.
  bool LoadSrk(TSS_HCONTEXT context_handle, TSS_HKEY* srk_handle,
               TSS_RESULT* result) const;

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

  // Tries to connect to the TPM
  TSS_HCONTEXT ConnectContext();

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

  // Tpm Context information
  trousers::ScopedTssContext tpm_context_;

  DISALLOW_COPY_AND_ASSIGN(TpmImpl);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_IMPL_H_
