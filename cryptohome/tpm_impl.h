// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_IMPL_H_
#define CRYPTOHOME_TPM_IMPL_H_

#include <base/macros.h>

#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha)

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>

#include "cryptohome/signature_sealing_backend_tpm1_impl.h"
#include "cryptohome/tpm.h"

namespace cryptohome {

// See README.lockbox for information on how this was selected.
const uint32_t kLockboxIndex = 0x20000004;

class TpmImpl : public Tpm {
 public:
  TpmImpl();
  virtual ~TpmImpl();
  // Tpm methods
  TpmVersion GetVersion() override { return TpmVersion::TPM_1_2; }
  TpmRetryAction EncryptBlob(TpmKeyHandle key_handle,
                             const brillo::SecureBlob& plaintext,
                             const brillo::SecureBlob& key,
                             brillo::SecureBlob* ciphertext) override;
  TpmRetryAction DecryptBlob(TpmKeyHandle key_handle,
                             const brillo::SecureBlob& ciphertext,
                             const brillo::SecureBlob& key,
                             const std::map<uint32_t, std::string>& pcr_map,
                             brillo::SecureBlob* plaintext) override;
  TpmRetryAction SealToPcrWithAuthorization(
      TpmKeyHandle key_handle,
      const brillo::SecureBlob& plaintext,
      const brillo::SecureBlob& auth_blob,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* sealed_data) override;
  TpmRetryAction UnsealWithAuthorization(
      TpmKeyHandle key_handle,
      const brillo::SecureBlob& sealed_data,
      const brillo::SecureBlob& auth_blob,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* plaintext) override;
  TpmRetryAction GetPublicKeyHash(TpmKeyHandle key_handle,
                                  brillo::SecureBlob* hash) override;
  bool GetOwnerPassword(brillo::SecureBlob* owner_password) override;
  bool IsEnabled() override { return !is_disabled_; }
  void SetIsEnabled(bool enabled) override { is_disabled_ = !enabled; }
  bool IsOwned() override {
    // Note that this is one hack to align the behaviour of TPM 1.2 monolithic
    // mode and distributed mode (tpm_manager). In distributed mode, IsOwned()
    // will return false when the device is pre-owned, as in owned with the
    // default password. However, TPM 1.2 monolithic mode will return true if we
    // just return is_owned_. The addition of && !is_being_owned_ here is to
    // ensure that it return false when it is in preowned state, in alignment
    // with distributed mode.
    return is_owned_ && !is_being_owned_;
  }
  void SetIsOwned(bool owned) override { is_owned_ = owned; }
  bool PerformEnabledOwnedCheck(bool* enabled, bool* owned) override;
  bool IsInitialized() override { return initialized_; }
  void SetIsInitialized(bool done) override { initialized_ = done; }
  bool IsBeingOwned() override { return is_being_owned_; }
  void SetIsBeingOwned(bool value) override { is_being_owned_ = value; }
  bool GetRandomDataBlob(size_t length, brillo::Blob* data) override;
  bool GetRandomDataSecureBlob(size_t length,
                               brillo::SecureBlob* data) override;
  bool GetAlertsData(Tpm::AlertsData* alerts) override;
  bool DefineNvram(uint32_t index, size_t length, uint32_t flags) override;
  bool DestroyNvram(uint32_t index) override;
  bool WriteNvram(uint32_t index, const brillo::SecureBlob& blob) override;
  bool ReadNvram(uint32_t index, brillo::SecureBlob* blob) override;
  bool IsNvramDefined(uint32_t index) override;
  bool IsNvramLocked(uint32_t index) override;
  bool WriteLockNvram(uint32_t index) override;
  unsigned int GetNvramSize(uint32_t index) override;
  TpmRetryAction GetEndorsementPublicKey(
      brillo::SecureBlob* ek_public_key) override;
  TpmRetryAction GetEndorsementPublicKeyWithDelegate(
      brillo::SecureBlob* ek_public_key,
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret) override;
  bool GetEndorsementCredential(brillo::SecureBlob* credential) override;
  bool MakeIdentity(brillo::SecureBlob* identity_public_key_der,
                    brillo::SecureBlob* identity_public_key,
                    brillo::SecureBlob* identity_key_blob,
                    brillo::SecureBlob* identity_binding,
                    brillo::SecureBlob* identity_label,
                    brillo::SecureBlob* pca_public_key,
                    brillo::SecureBlob* endorsement_credential,
                    brillo::SecureBlob* platform_credential,
                    brillo::SecureBlob* conformance_credential) override;
  QuotePcrResult QuotePCR(uint32_t pcr_index,
                          bool check_pcr_value,
                          const brillo::SecureBlob& identity_key_blob,
                          const brillo::SecureBlob& external_data,
                          brillo::Blob* pcr_value,
                          brillo::SecureBlob* quoted_data,
                          brillo::SecureBlob* quote) override;
  bool SealToPCR0(const brillo::SecureBlob& value,
                  brillo::SecureBlob* sealed_value) override;
  bool Unseal(const brillo::SecureBlob& sealed_value,
              brillo::SecureBlob* value) override;
  bool CreateCertifiedKey(const brillo::SecureBlob& identity_key_blob,
                          const brillo::SecureBlob& external_data,
                          brillo::SecureBlob* certified_public_key,
                          brillo::SecureBlob* certified_public_key_der,
                          brillo::SecureBlob* certified_key_blob,
                          brillo::SecureBlob* certified_key_info,
                          brillo::SecureBlob* certified_key_proof) override;
  bool CreateDelegate(const std::set<uint32_t>& bound_pcrs,
                      uint8_t delegate_family_label,
                      uint8_t delegate_label,
                      brillo::Blob* delegate_blob,
                      brillo::Blob* delegate_secret) override;
  bool ActivateIdentity(const brillo::Blob& delegate_blob,
                        const brillo::Blob& delegate_secret,
                        const brillo::SecureBlob& identity_key_blob,
                        const brillo::SecureBlob& encrypted_asym_ca,
                        const brillo::SecureBlob& encrypted_sym_ca,
                        brillo::SecureBlob* identity_credential) override;
  bool Sign(const brillo::SecureBlob& key_blob,
            const brillo::SecureBlob& input,
            uint32_t bound_pcr_index,
            brillo::SecureBlob* signature) override;
  bool CreatePCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                         AsymmetricKeyUsage key_type,
                         brillo::SecureBlob* key_blob,
                         brillo::SecureBlob* public_key_der,
                         brillo::SecureBlob* creation_blob) override;
  bool VerifyPCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                         const brillo::SecureBlob& key_blob,
                         const brillo::SecureBlob& creation_blob) override;
  bool ExtendPCR(uint32_t pcr_index, const brillo::Blob& extension) override;
  bool ReadPCR(uint32_t pcr_index, brillo::Blob* pcr_value) override;
  bool IsEndorsementKeyAvailable() override;
  bool CreateEndorsementKey() override;
  bool TakeOwnership(int max_timeout_tries,
                     const brillo::SecureBlob& owner_password) override;
  bool InitializeSrk(const brillo::SecureBlob& owner_password) override;
  bool ChangeOwnerPassword(const brillo::SecureBlob& previous_owner_password,
                           const brillo::SecureBlob& owner_password) override;
  bool TestTpmAuth(const brillo::SecureBlob& owner_password) override;
  void SetOwnerPassword(const brillo::SecureBlob& owner_password) override;
  bool WrapRsaKey(const brillo::SecureBlob& public_modulus,
                  const brillo::SecureBlob& prime_factor,
                  brillo::SecureBlob* wrapped_key) override;
  TpmRetryAction LoadWrappedKey(const brillo::SecureBlob& wrapped_key,
                                ScopedKeyHandle* key_handle) override;
  bool LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                               brillo::SecureBlob* key_blob) override;
  void CloseHandle(TpmKeyHandle key_handle) override;
  void GetStatus(TpmKeyHandle key, TpmStatusInfo* status) override;
  base::Optional<bool> IsSrkRocaVulnerable() override;
  bool GetDictionaryAttackInfo(int* counter,
                               int* threshold,
                               bool* lockout,
                               int* seconds_remaining) override;
  bool ResetDictionaryAttackMitigation(
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret) override;
  void DeclareTpmFirmwareStable() override {}
  bool RemoveOwnerDependency(
      TpmPersistentState::TpmOwnerDependency dependency) override;
  bool ClearStoredPassword() override;
  bool GetVersionInfo(TpmVersionInfo* version_info) override;
  bool GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) override;
  bool SetUserType(Tpm::UserType type) override;
  bool GetRsuDeviceId(std::string* device_id) override;
  LECredentialBackend* GetLECredentialBackend() override;
  SignatureSealingBackend* GetSignatureSealingBackend() override;
  bool GetDelegate(brillo::Blob* blob,
                   brillo::Blob* secret,
                   bool* has_reset_lock_permissions) override;
  void HandleOwnershipTakenEvent() override;
  bool DoesUseTpmManager() override;
  bool IsCurrentPCR0ValueValid() override;
  void SetDelegateData(const std::string& delegate_blob,
                       bool has_reset_lock_permissions) override;
  base::Optional<bool> IsDelegateBoundToPcr() override;
  bool DelegateCanResetDACounter() override;
  // Returns the map with expected PCR values for the user.
  std::map<uint32_t, std::string> GetPcrMap(
      const std::string& obfuscated_username,
      bool use_extended_pcr) const override;

  bool CreatePolicyWithRandomPassword(TSS_HCONTEXT context_handle,
                                      TSS_FLAG policy_type,
                                      TSS_HPOLICY* policy_handle);

  // Gets a handle to the SRK.
  bool LoadSrk(TSS_HCONTEXT context_handle,
               TSS_HKEY* srk_handle,
               TSS_RESULT* result) const;

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the owner password is available and
  // authorization is successfully acquired.
  bool ConnectContextAsOwner(TSS_HCONTEXT* context_handle,
                             TSS_HTPM* tpm_handle);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object authorized by the given delegation.
  bool ConnectContextAsDelegate(const brillo::Blob& delegate_blob,
                                const brillo::Blob& delegate_secret,
                                TSS_HCONTEXT* context,
                                TSS_HTPM* tpm_handle);

  // Wrapper for Tspi_GetAttribData.
  TpmRetryAction GetDataAttribute(TSS_HCONTEXT context,
                                  TSS_HOBJECT object,
                                  TSS_FLAG flag,
                                  TSS_FLAG sub_flag,
                                  brillo::SecureBlob* data) const;

  // Creates Trousers key object for the RSA public key, given its public
  // modulus in |key_modulus|, creation flags in |key_flags|, signature scheme
  // or |TSS_SS_NONE| in |signature_scheme|, encryption scheme or |TSS_ES_NONE|
  // in |encryption_scheme|. The key's public exponent is assumed to be 65537.
  // Populates |key_handle| with the loaded key handle.
  bool CreateRsaPublicKeyObject(TSS_HCONTEXT tpm_context,
                                const brillo::Blob& key_modulus,
                                TSS_FLAG key_flags,
                                UINT32 signature_scheme,
                                UINT32 encryption_scheme,
                                TSS_HKEY* key_handle);

 private:
  // Connects to the TPM and return its context at |context_handle|.
  bool OpenAndConnectTpm(TSS_HCONTEXT* context_handle, TSS_RESULT* result);

  // Gets the Public Key blob associated with |key_handle|.
  bool GetPublicKeyBlob(TSS_HCONTEXT context_handle,
                        TSS_HKEY key_handle,
                        brillo::SecureBlob* data_out,
                        TSS_RESULT* result) const;

  // Gets the key blob associated with |key_handle|.
  bool GetKeyBlob(TSS_HCONTEXT context_handle,
                  TSS_HKEY key_handle,
                  brillo::SecureBlob* data_out,
                  TSS_RESULT* result) const;

  // Zeros the SRK password (sets it to an empty string)
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   owner_password - The owner password for the TPM
  bool ZeroSrkPassword(TSS_HCONTEXT context_handle,
                       const brillo::SecureBlob& owner_password);

  // Removes usage restrictions on the SRK
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   owner_password - The owner password for the TPM
  bool UnrestrictSrk(TSS_HCONTEXT context_handle,
                     const brillo::SecureBlob& owner_password);

  // Tries to connect to the TPM
  TSS_HCONTEXT ConnectContext();

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the context can be created and a TPM object
  // exists in the TSS.
  bool ConnectContextAsUser(TSS_HCONTEXT* context_handle, TSS_HTPM* tpm_handle);

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
                           brillo::SecureBlob* blob);

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
                      const brillo::SecureBlob& owner_password,
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
                            const brillo::Blob& delegate_blob,
                            const brillo::Blob& delegate_secret,
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
  bool DecryptIdentityRequest(RSA* pca_key,
                              const brillo::SecureBlob& request,
                              brillo::SecureBlob* identity_binding,
                              brillo::SecureBlob* endorsement_credential,
                              brillo::SecureBlob* platform_credential,
                              brillo::SecureBlob* conformance_credential);

  // Wrapper for Tspi_TPM_GetCapability. If |data| is not NULL, the raw
  // capability data will be assigned. If |value| is not NULL, the capability
  // data must be exactly 4 bytes and it will be decoded into |value|.
  bool GetCapability(TSS_HCONTEXT context_handle,
                     TSS_HTPM tpm_handle,
                     UINT32 capability,
                     UINT32 sub_capability,
                     brillo::Blob* data,
                     UINT32* value) const;

  // Get the endorsement public key based on context and tpm handle previously
  // obtained. Returns true on success.
  TpmRetryAction GetEndorsementPublicKeyInternal(
      brillo::SecureBlob* ek_public_key,
      TSS_HCONTEXT* context_handle,
      TSS_HTPM* tpm_handle);

  // Assigns the authorization value to object.
  bool SetAuthValue(TSS_HCONTEXT context_handle,
                    trousers::ScopedTssKey* enc_handle,
                    TSS_HTPM tpm_handle,
                    const brillo::SecureBlob& auth_blob);

  // Member variables
  bool initialized_;
  brillo::SecureBlob srk_auth_;

  // If TPM ownership is taken, owner_password_ contains the password used
  brillo::SecureBlob owner_password_;

  // Used to provide thread-safe access to owner_password_, as it is set in the
  // initialization background thread.
  base::Lock password_sync_lock_;

  // Indicates if the TPM is disabled
  bool is_disabled_;

  // Indicates if the TPM is owned
  bool is_owned_;

  // Indicates if the TPM is being owned
  bool is_being_owned_;

  // Indicates if the delegate is bound to PCR.
  base::Optional<bool> is_delegate_bound_to_pcr_;

  // Indicates if the delegate is allowed to reset dictional attack counter.
  bool has_reset_lock_permissions_ = false;

  // Tpm Context information
  trousers::ScopedTssContext tpm_context_;

  // A single instance of the backend for signature-sealing operations that is
  // returned from GetSignatureSealingBackend().
  SignatureSealingBackendTpm1Impl signature_sealing_backend_{this};

  DISALLOW_COPY_AND_ASSIGN(TpmImpl);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_IMPL_H_
