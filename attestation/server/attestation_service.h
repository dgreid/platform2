// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_SERVER_ATTESTATION_SERVICE_H_
#define ATTESTATION_SERVER_ATTESTATION_SERVICE_H_

#include "attestation/common/attestation_interface.h"

#include <stdint.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include <attestation/proto_bindings/attestation_ca.pb.h>
#include <attestation/proto_bindings/pca_agent.pb.h>
#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <base/threading/thread.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest_prod.h>
#include <policy/libpolicy.h>

#include "attestation/common/crypto_utility.h"
#include "attestation/common/crypto_utility_impl.h"
#include "attestation/common/tpm_utility_factory.h"
#include "attestation/pca_agent/dbus-proxies.h"
#include "attestation/server/attestation_flow.h"
#include "attestation/server/attestation_service_metrics.h"
#include "attestation/server/certificate_queue.h"
#include "attestation/server/database.h"
#include "attestation/server/database_impl.h"
#include "attestation/server/enrollment_queue.h"
#include "attestation/server/google_keys.h"
#include "attestation/server/key_store.h"
#include "attestation/server/pkcs11_key_store.h"
#include "tpm_manager/client/tpm_nvram_dbus_proxy.h"
#include "tpm_manager/client/tpm_ownership_dbus_proxy.h"
#include "tpm_manager/common/tpm_nvram_interface.h"
#include "tpm_manager/common/tpm_ownership_interface.h"

namespace attestation {

// An implementation of AttestationInterface for the core attestation service.
// Access to TPM, network and local file-system resources occurs asynchronously
// with the exception of Initialize(). All methods must be called on the same
// thread that originally called Initialize().
// Usage:
//   std::unique_ptr<AttestationInterface> attestation =
//       new AttestationService();
//   CHECK(attestation->Initialize(nullptr));
//   attestation->GetEndorsementInfo(...);
//
// THREADING NOTES:
// This class runs a worker thread and delegates all calls to it. This keeps the
// public methods non-blocking while allowing complex implementation details
// with dependencies on the TPM, network, and filesystem to be coded in a more
// readable way. It also serves to serialize method execution which reduces
// complexity with TPM state.
//
// Tasks that run on the worker thread are bound with base::Unretained which is
// safe because the thread is owned by this class (so it is guaranteed not to
// process a task after destruction). Weak pointers are used to post replies
// back to the main thread.
class AttestationService : public AttestationInterface {
 public:
  using IdentityCertificateMap = google::protobuf::
      Map<int, attestation::AttestationDatabase_IdentityCertificate>;
  using InitializeCompleteCallback = base::OnceCallback<void(bool)>;

  // The index of the first identity.
  static constexpr int kFirstIdentity = 0;

  // The request limit for enrollment queue.
  constexpr static size_t kEnrollmentRequestLimit = 50;

  // The alias limit for certification queue.
  const size_t kCertificateRequestAliasLimit = 5;

  // If abe_data is not an empty blob, its contents will be
  // used to enable attestation-based enterprise enrollment.
  explicit AttestationService(brillo::SecureBlob* abe_data);
  AttestationService(const AttestationService&) = delete;
  AttestationService& operator=(const AttestationService&) = delete;

  ~AttestationService() override = default;

  // AttestationInterface methods.
  bool Initialize() override;
  void GetEnrollmentPreparations(
      const GetEnrollmentPreparationsRequest& request,
      const GetEnrollmentPreparationsCallback& callback) override;
  void GetKeyInfo(const GetKeyInfoRequest& request,
                  const GetKeyInfoCallback& callback) override;
  void GetEndorsementInfo(const GetEndorsementInfoRequest& request,
                          const GetEndorsementInfoCallback& callback) override;
  void GetAttestationKeyInfo(
      const GetAttestationKeyInfoRequest& request,
      const GetAttestationKeyInfoCallback& callback) override;
  void ActivateAttestationKey(
      const ActivateAttestationKeyRequest& request,
      const ActivateAttestationKeyCallback& callback) override;
  void CreateCertifiableKey(
      const CreateCertifiableKeyRequest& request,
      const CreateCertifiableKeyCallback& callback) override;
  void Decrypt(const DecryptRequest& request,
               const DecryptCallback& callback) override;
  void Sign(const SignRequest& request, const SignCallback& callback) override;
  void RegisterKeyWithChapsToken(
      const RegisterKeyWithChapsTokenRequest& request,
      const RegisterKeyWithChapsTokenCallback& callback) override;
  void GetStatus(const GetStatusRequest& request,
                 const GetStatusCallback& callback) override;
  void Verify(const VerifyRequest& request,
              const VerifyCallback& callback) override;
  void CreateEnrollRequest(
      const CreateEnrollRequestRequest& request,
      const CreateEnrollRequestCallback& callback) override;
  void FinishEnroll(const FinishEnrollRequest& request,
                    const FinishEnrollCallback& callback) override;
  void Enroll(const EnrollRequest& request,
              const EnrollCallback& callback) override;
  void CreateCertificateRequest(
      const CreateCertificateRequestRequest& request,
      const CreateCertificateRequestCallback& callback) override;
  void FinishCertificateRequest(
      const FinishCertificateRequestRequest& request,
      const FinishCertificateRequestCallback& callback) override;
  void GetCertificate(const GetCertificateRequest& request,
                      const GetCertificateCallback& callback) override;
  void SignEnterpriseChallenge(
      const SignEnterpriseChallengeRequest& request,
      const SignEnterpriseChallengeCallback& callback) override;
  void SignSimpleChallenge(
      const SignSimpleChallengeRequest& request,
      const SignSimpleChallengeCallback& callback) override;
  void SetKeyPayload(const SetKeyPayloadRequest& request,
                     const SetKeyPayloadCallback& callback) override;
  void DeleteKeys(const DeleteKeysRequest& request,
                  const DeleteKeysCallback& callback) override;
  void ResetIdentity(const ResetIdentityRequest& request,
                     const ResetIdentityCallback& callback) override;
  void GetEnrollmentId(const GetEnrollmentIdRequest& request,
                       const GetEnrollmentIdCallback& callback) override;
  void GetCertifiedNvIndex(
      const GetCertifiedNvIndexRequest& request,
      const GetCertifiedNvIndexCallback& callback) override;

  // Same as initialize but calls callback when tasks finish.
  bool InitializeWithCallback(InitializeCompleteCallback callback);

  // Return the type of the endorsement key (EK).
  KeyType GetEndorsementKeyType() const;

  // Return the type of the attestation identity key (AIK).
  KeyType GetAttestationIdentityKeyType() const;

  // Mutators useful for testing.
  void set_crypto_utility(CryptoUtility* crypto_utility) {
    crypto_utility_ = crypto_utility;
  }

  void set_database(Database* database) { database_ = database; }

  void set_key_store(KeyStore* key_store) { key_store_ = key_store; }

  void set_tpm_utility(TpmUtility* tpm_utility) { tpm_utility_ = tpm_utility; }

  void set_hwid(const std::string& hwid) { hwid_ = hwid; }

  void set_abe_data(brillo::SecureBlob* abe_data) { abe_data_ = abe_data; }

  void set_pca_agent_proxy(org::chromium::PcaAgentProxyInterface* proxy) {
    pca_agent_proxy_ = proxy;
  }

  void set_google_keys(GoogleKeys google_keys) {
    google_keys_ = std::move(google_keys);
  }

  void set_policy_provider(policy::PolicyProvider* policy_provider) {
    policy_provider_.reset(policy_provider);
  }

 private:
  enum class EnrollmentStatus {
    kUnknown,
    kNotEnrolled,
    kInProgress,
    kEnrolled,
  };

  enum ACATypeInternal { kDefaultACA = 0, kTestACA = 1, kMaxACATypeInternal };

  static ACAType GetACAType(ACATypeInternal aca_type_internal);

  friend class AttestationServiceBaseTest;
  friend class AttestationServiceTest;

  typedef std::map<std::string, std::string> CertRequestMap;

  // Attestation service worker thread class that cleans up after stopping.
  class ServiceWorkerThread : public base::Thread {
   public:
    explicit ServiceWorkerThread(AttestationService* service)
        : base::Thread("Attestation Service Worker"), service_(service) {
      DCHECK(service_);
    }
    ServiceWorkerThread(const ServiceWorkerThread&) = delete;
    ServiceWorkerThread& operator=(const ServiceWorkerThread&) = delete;

    ~ServiceWorkerThread() override { Stop(); }

   private:
    void CleanUp() override { service_->ShutdownTask(); }

    AttestationService* const service_;

  };

  // A relay callback which allows the use of weak pointer semantics for a reply
  // to TaskRunner::PostTaskAndReply.
  template <typename ReplyProtobufType>
  void TaskRelayCallback(
      const base::Callback<void(const ReplyProtobufType&)> callback,
      const std::shared_ptr<ReplyProtobufType>& reply) {
    callback.Run(*reply);
  }

  // Initialization to be run on the worker thread.
  void InitializeTask(InitializeCompleteCallback callback);

  // Checks if |database_| needs to be migrated to the latest data model and
  // do so if needed. Returns true if migration was needed and successful.
  bool MigrateAttestationDatabase();

  // Migrates identity date in |database_| if needed. Returns true if the
  // migration was needed and successful.
  // Note that this function is not multithread safe.
  bool MigrateIdentityData();

  // Shutdown to be run on the worker thread.
  void ShutdownTask();

  // A blocking implementation of GetEnrollmentPreparations.
  void GetEnrollmentPreparationsTask(
      const GetEnrollmentPreparationsRequest& request,
      const std::shared_ptr<GetEnrollmentPreparationsReply>& result);

  // A blocking implementation of GetKeyInfo.
  void GetKeyInfoTask(const GetKeyInfoRequest& request,
                      const std::shared_ptr<GetKeyInfoReply>& result);

  // A blocking implementation of GetEndorsementInfo.
  void GetEndorsementInfoTask(
      const GetEndorsementInfoRequest& request,
      const std::shared_ptr<GetEndorsementInfoReply>& result);

  // A blocking implementation of GetAttestationKeyInfo.
  void GetAttestationKeyInfoTask(
      const GetAttestationKeyInfoRequest& request,
      const std::shared_ptr<GetAttestationKeyInfoReply>& result);

  // A blocking implementation of ActivateAttestationKey.
  void ActivateAttestationKeyTask(
      const ActivateAttestationKeyRequest& request,
      const std::shared_ptr<ActivateAttestationKeyReply>& result);

  // A blocking implementation of CreateCertifiableKey.
  void CreateCertifiableKeyTask(
      const CreateCertifiableKeyRequest& request,
      const std::shared_ptr<CreateCertifiableKeyReply>& result);

  // A blocking implementation of Decrypt.
  void DecryptTask(const DecryptRequest& request,
                   const std::shared_ptr<DecryptReply>& result);

  // A blocking implementation of Sign.
  void SignTask(const SignRequest& request,
                const std::shared_ptr<SignReply>& result);

  // A synchronous implementation of RegisterKeyWithChapsToken.
  void RegisterKeyWithChapsTokenTask(
      const RegisterKeyWithChapsTokenRequest& request,
      const std::shared_ptr<RegisterKeyWithChapsTokenReply>& result);

  // A synchronous implementation of GetStatus.
  void GetStatusTask(const GetStatusRequest& request,
                     const std::shared_ptr<GetStatusReply>& result);

  // A synchronous implementation of Verify.
  void VerifyTask(const VerifyRequest& request,
                  const std::shared_ptr<VerifyReply>& result);

  // A synchronous implementation of CreateEnrollRequest.
  template <typename RequestType>
  void CreateEnrollRequestTask(
      const RequestType& request,
      const std::shared_ptr<CreateEnrollRequestReply>& result);

  // A synchronous implementation of FinishEnroll.
  template <typename ReplyType>
  void FinishEnrollTask(const FinishEnrollRequest& request,
                        const std::shared_ptr<ReplyType>& result);

  // A synchronous implementation of CreateCertificateRequest.
  template <typename RequestType>
  void CreateCertificateRequestTask(
      const RequestType& request,
      const std::shared_ptr<CreateCertificateRequestReply>& result);

  // A synchronous implementation of FinishCertificateRequest.
  template <typename ReplyType>
  void FinishCertificateRequestTask(
      const FinishCertificateRequestRequest& request,
      const std::shared_ptr<ReplyType>& result);

  // A synchronous implementation of SignEnterpriseChallenge.
  void SignEnterpriseChallengeTask(
      const SignEnterpriseChallengeRequest& request,
      const std::shared_ptr<SignEnterpriseChallengeReply>& result);

  // A synchronous implementation of SignSimpleChallenge.
  void SignSimpleChallengeTask(
      const SignSimpleChallengeRequest& request,
      const std::shared_ptr<SignSimpleChallengeReply>& result);

  // A synchronous implementation of SetKeyPayload.
  void SetKeyPayloadTask(const SetKeyPayloadRequest& request,
                         const std::shared_ptr<SetKeyPayloadReply>& result);

  // A synchronous implementation of DeleteKeys.
  void DeleteKeysTask(const DeleteKeysRequest& request,
                      const std::shared_ptr<DeleteKeysReply>& result);

  // A synchronous implementation of ResetIdentity.
  void ResetIdentityTask(const ResetIdentityRequest& request,
                         const std::shared_ptr<ResetIdentityReply>& result);

  // A synchronous implementation for GetEnrollmentId.
  void GetEnrollmentIdTask(const GetEnrollmentIdRequest& request,
                           const std::shared_ptr<GetEnrollmentIdReply>& result);

  // A synchronous implementation for GetCertifiedNvIndex.
  void GetCertifiedNvIndexTask(
      const GetCertifiedNvIndexRequest& request,
      const std::shared_ptr<GetCertifiedNvIndexReply>& result);

  // Returns true if PrepareForEnrollment() initialization step has been
  // successfully done for any Google Attestation CA.
  // Note that while in normal circumstance, this returning true means that all
  // info required for enrollment is available, but that's not always the case,
  // see the implementation for detail.
  bool IsPreparedForEnrollment();

  // Returns true if PrepareForEnrollment() initialization step has been
  // successfully done for the given Google Attestation CA.
  bool IsPreparedForEnrollmentWithACA(ACAType aca_type);

  // Returns true iff enrollment with the default or test Google Attestation CA
  // has been completed.
  bool IsEnrolled();

  // Returns true iff enrollment with the given Google Attestation CA has been
  // completed.
  bool IsEnrolledWithACA(ACAType aca_type);

  // Creates an enrollment request compatible with the Google Attestation CA.
  // Returns true on success.
  bool CreateEnrollRequestInternal(ACAType aca_type,
                                   std::string* enroll_request);

  // Finishes enrollment given an |enroll_response| from the Google Attestation
  // CA. Returns true on success. On failure, returns false and sets
  // |server_error| to the error string from the CA.
  bool FinishEnrollInternal(ACAType aca_type,
                            const std::string& enroll_response,
                            std::string* server_error);

  // Creates a |certificate_request| compatible with the Google Attestation CA
  // for the given |key|, according to the given |profile|, |username| and
  // |origin|.
  bool CreateCertificateRequestInternal(ACAType aca_type,
                                        const std::string& username,
                                        const CertifiedKey& key,
                                        CertificateProfile profile,
                                        const std::string& origin,
                                        std::string* certificate_request,
                                        std::string* message_id);

  // Finishes a certificate request by decoding the |certificate_response| to
  // recover the |certificate_chain| and storing it in association with the
  // |key| identified by |username| and |key_label|. Returns true on success. On
  // failure, returns false and sets |server_error| to the error string from the
  // CA. Calls PopulateAndStoreCertifiedKey internally.
  bool FinishCertificateRequestInternal(const std::string& certificate_response,
                                        const std::string& username,
                                        const std::string& key_label,
                                        const std::string& message_id,
                                        CertifiedKey* key,
                                        std::string* certificate_chain,
                                        std::string* server_error);

  // Recover the |certificate_chain| from |response_pb| and store it in
  // association with the |key| identified by |username| and |key_label|.
  // Returns true on success.
  bool PopulateAndStoreCertifiedKey(
      const AttestationCertificateResponse& response_pb,
      const std::string& username,
      const std::string& key_label,
      CertifiedKey* key,
      std::string* certificate_chain);

  // Creates, certifies, and saves a new |key| for |username| with the given
  // |key_label|, |key_type|, and |key_usage|. Returns true on success.
  bool CreateKey(const std::string& username,
                 const std::string& key_label,
                 KeyType key_type,
                 KeyUsage key_usage,
                 CertifiedKey* key);

  // Finds the |key| associated with |username| and |key_label|. Returns false
  // if such a key does not exist.
  bool FindKeyByLabel(const std::string& username,
                      const std::string& key_label,
                      CertifiedKey* key);

  // Saves the |key| associated with |username| and |key_label|. Returns true on
  // success.
  bool SaveKey(const std::string& username,
               const std::string& key_label,
               const CertifiedKey& key);

  // Deletes the key associated with |username| and |key_label|.
  bool DeleteKey(const std::string& username, const std::string& key_label);

  // Deletes the key associated with |username| and having prefix |key_prefix|.
  bool DeleteKeysByPrefix(const std::string& username,
                          const std::string& key_prefix);

  // Adds named device-wide key to the attestation database.
  bool AddDeviceKey(const std::string& key_label, const CertifiedKey& key);

  // Removes a device-wide key from the attestation database.
  bool RemoveDeviceKey(const std::string& key_label);

  // Removes device-wide keys with a given prefix from the attestation
  // database.
  bool RemoveDeviceKeysByPrefix(const std::string& key_prefix);

  // Creates a PEM certificate chain from the credential fields of a |key|.
  std::string CreatePEMCertificateChain(const CertifiedKey& key);

  // Creates a certificate in PEM format from a DER encoded X.509 certificate.
  std::string CreatePEMCertificate(const std::string& certificate);

  // Chooses a temporal index which will be used by the ACA to create a
  // certificate.  This decision factors in the currently signed-in |user| and
  // the |origin| of the certificate request.  The strategy is to find an index
  // which has not already been used by another user for the same origin.
  int ChooseTemporalIndex(const std::string& user, const std::string& origin);

  // Creates a X.509/DER SubjectPublicKeyInfo for the given |key_type| and
  // |public_key|. On success returns true and provides |public_key_info|.
  // TODO(crbug/942487): After this migration, we won't need this utility
  // anymore. For now, we store ECC public key as SubjectPublicKeyInfo format,
  // which will be passed as |public_key|.
  bool GetSubjectPublicKeyInfo(KeyType key_type,
                               const std::string& public_key,
                               std::string* public_key_info) const;

  // Get endorsement public key. Get it from proto database if exists, otherwise
  // get it from tpm_utility.
  base::Optional<std::string> GetEndorsementPublicKey() const;

  // Get endorsement certificate. Get it from proto database if exists,
  // otherwise get it from tpm_utility.
  base::Optional<std::string> GetEndorsementCertificate() const;

  // Prepares the attestation system for enrollment with an ACA.
  void PrepareForEnrollment(InitializeCompleteCallback callback);

  // Gets the customerId from the policy data and populates in |key_info|.
  bool PopulateCustomerId(KeyInfo* key_info);

  // Returns an iterator pointing to the identity certificate for the given
  // |identity| and given Privacy CA.
  virtual IdentityCertificateMap::iterator FindIdentityCertificate(
      int identity, ACAType pca_type);

  // Returns whether there is an identity certificate for the given |identity|
  // and given Privacy CA.
  virtual bool HasIdentityCertificate(int identity, ACAType pca_type);

  // Finds an existing identity certificate for the given |identity| and Privacy
  // CA, and if none is found, creates one. Returns a pointer to the certificate
  // or nullptr if one could not be created. If |cert_index| is non null, set it
  // to the index of the certificate, or -1 if none could be found or created.
  AttestationDatabase_IdentityCertificate* FindOrCreateIdentityCertificate(
      int identity, ACAType aca_type, int* cert_index);

  // Creates a new identity and returns its index, or -1 if it could not be
  // created.
  int CreateIdentity(int identity_features);

  // Quote NVRAM data. Returns the quoted data in |quote| and |true| if
  // success, |false| otherwise.
  bool QuoteNvramData(NVRAMQuoteType quote_type,
                      const IdentityKey& identity_key,
                      Quote* quote);

  // Certify NVRAM data and insert it into the given |identity|. Returns false
  // if data cannot be inserted, or if |must_be_present| is true and the data
  // cannot be certified.
  bool InsertCertifiedNvramData(NVRAMQuoteType quote_type,
                                bool must_be_present,
                                AttestationDatabase::Identity* identity);

  // Returns the count of identities in the attestation database.
  virtual int GetIdentitiesCount() const;
  // Returns the identity features of |identity|.
  virtual int GetIdentityFeatures(int identity) const;

  // Returns a copy of the identity certificate map.
  virtual IdentityCertificateMap GetIdentityCertificateMap() const;

  // ENcrypts all the endorsement credentials that we don't have yet.
  bool EncryptAllEndorsementCredentials();

  // Encrypts data for the given |aca_type|.
  bool EncryptDataForAttestationCA(ACAType aca_type,
                                   const std::string& data,
                                   EncryptedData* encrypted_data);

  // Activates an attestation key given an |encrypted_certificate|. The EK with
  // |ek_key_type| will be used for activation. On success returns true and
  // provides the decrypted |certificate| if not null. If |save_certificate| is
  // set, also writes the |certificate| to the database and returns the
  // certificate number in |certificate_index|. The value of |certificate_index|
  // is undefined if the certificate is not saved. |ek_key_type| is only used in
  // TPM 2.0.
  bool ActivateAttestationKeyInternal(
      int identity,
      ACAType aca_type,
      KeyType ek_key_type,
      const EncryptedIdentityCredential& encrypted_certificate,
      bool save_certificate,
      std::string* certificate,
      int* certificate_index);

  // Checks if PCR0 indicates that the system booted in verified mode.
  // Always reads PCR0 contents from TPM, so works even when not prepared
  // for enrollment.
  bool IsVerifiedMode() const;

  // Validates incoming enterprise challenge data.
  bool ValidateEnterpriseChallenge(VAType va_type,
                                   const SignedData& signed_challenge);

  // Encrypts a KeyInfo protobuf as required for an enterprise challenge
  // response.
  bool EncryptEnterpriseKeyInfo(VAType va_type,
                                const KeyInfo& key_info,
                                EncryptedData* encrypted_data);

  // Signs data using the provided key. On success, returns true and fills
  // |response| with serialized SignedData.
  bool SignChallengeData(const CertifiedKey& key,
                         const std::string& data_to_sign,
                         std::string* response);

  // Verifies identity key binding data.
  bool VerifyIdentityBinding(const IdentityBinding& binding);

  // Computes and returns the PCR value for a known 3-byte |mode|:
  //  - byte 0: 1 if in developer mode, 0 otherwise,
  //  - byte 1: 1 if in recovery mode, 0 otherwise,
  //  - byte 2: 1 if verified firmware, 0 if developer firmware.
  std::string GetPCRValueForMode(const char* mode) const;

  // Verifies that PCR quote signature is correct.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  bool VerifyQuoteSignature(const std::string& aik_public_key_info,
                            const Quote& quote,
                            uint32_t pcr_index);

  // Verifies PCR0 quote.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  bool VerifyPCR0Quote(const std::string& aik_public_key_info,
                       const Quote& pcr0_quote);

  // Verifies PCR1 quote.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  bool VerifyPCR1Quote(const std::string& aik_public_key_info,
                       const Quote& pcr1_quote);

  // Calculates the digest for a certified key. The digest is TPM version (1.2
  // vs 2.0) specific.
  // public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  // public_key_tpm_format must be provided in TPM format.
  bool GetCertifiedKeyDigest(const std::string& public_key_info,
                             const std::string& public_key_tpm_format,
                             std::string* key_digest);

  // Verifies a certified key.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  // public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  // key_info is a TPM_CERTIFY_INFO structure.
  bool VerifyCertifiedKey(const std::string& aik_public_key_info,
                          const std::string& public_key_info,
                          const std::string& public_key_tpm_format,
                          const std::string& key_info,
                          const std::string& proof);

  // Creates a certified key and verifies it.
  // aik_public_key_info must be provided in X.509 SubjectPublicKeyInfo format.
  bool VerifyCertifiedKeyGeneration(const std::string& aik_key_blob,
                                    const std::string& aik_public_key_info);

  // Performs AIK activation with a fake credential. It use RSA EK for the fake
  // credential sharing.
  bool VerifyActivateIdentity(const std::string& aik_public_key_tpm_format);

  // Calls the routine corresponding to the |AttestationFlowAction| stored in
  // |data| to proceed the next step of the enrollment flow. Most of the actions
  // taken are quite self-explanatory except a caveat -- Since enrollment might
  // be followed by certificate flow, upon Receiving
  // |AttestationFlowAction::kNoop|, the flow might continue by posting
  // |StartCertificateTask|.
  void OnEnrollAction(const std::shared_ptr<AttestationFlowData>& data);

  // Sends the enroll request with the content stored in |data| via
  // |pca_agentd|. See |HandlePcaAgentEnrollRequestError| and
  // |HandlePcaAgentEnrollReply| for more details.
  void SendEnrollRequest(const std::shared_ptr<AttestationFlowData>& data);

  // Calls the D-bus method callback stored in |data| with a bad
  // |AttestationStatus|. Passed as the error callback of
  // |pca_agent::EnrollAsync|.
  void HandlePcaAgentEnrollRequestError(
      const std::shared_ptr<AttestationFlowData>& data, brillo::Error* err);

  // Calls the D-bus method callback stored in |data| with a proper
  // |AttestationStatus| depending on the response received from PCA server.
  // Passed as the success callback of |pca_agent::EnrollAsync|.
  void HandlePcaAgentEnrollReply(
      const std::shared_ptr<AttestationFlowData>& data,
      const pca_agent::EnrollReply& pca_reply);

  // Calls the routine corresponding to the |AttestationFlowAction| stored in
  // |data| to proceed the next step of the certificate flow.
  void OnGetCertificateAction(const std::shared_ptr<AttestationFlowData>& data);

  // Sends the certificate request with the content stored in |data| via
  // |pca_agentd|.See |HandlePcaAgentGetCertificateRequestError| and
  // |HandlePcaAgentGetCertificateReply| for more details.
  void SendGetCertificateRequest(
      const std::shared_ptr<AttestationFlowData>& data);

  // Calls the D-bus method callback stored in |data| with a bad
  // |AttestationStatus|. Passed as the error callback of
  // |pca_agent::GetCertificateAsync|.
  void HandlePcaAgentGetCertificateRequestError(
      const std::shared_ptr<AttestationFlowData>& data, brillo::Error* err);

  // Calls the D-bus method callback stored in |data| with a proper
  // |AttestationStatus| depending on the response received from PCA server via
  // |pca_agentd|. Passed as the success callback of
  // |pca_agent::GetCertificateAsync|.
  void HandlePcaAgentGetCertificateReply(
      const std::shared_ptr<AttestationFlowData>& data,
      const pca_agent::GetCertificateReply& pca_reply);

  // Creates the enroll request and stores the return status code and the result
  // request (if any) into |data|; also, specifies the next
  // |AttestationFlowAction| into |data| accordingly.
  void StartEnrollTask(const std::shared_ptr<AttestationFlowData>& data);

  // Posts |StartEnrollTask|, along with its reply task |OnEnrollAction|.
  void PostStartEnrollTask(const std::shared_ptr<AttestationFlowData>& data);

  // Finishes the enroll request  with the response stored in |data|; also,
  // specifies the next |AttestationFlowAction| into |data| accordingly. Named
  // with suffix "V2" because the legacy |FinishEnrollTask| exists; function
  // overloading causes some unnecessary code change because we have to
  // specify the function signature when we posts |FinishEnrollTask|; thus,
  // chooses just rename rather than overloading.
  void FinishEnrollTaskV2(const std::shared_ptr<AttestationFlowData>& data);

  // Creates the certificate request and stores the return status code and the
  // result request (if any) into |data|; also, specifies the next
  // |AttestationFlowAction| into |data| accordingly.
  void StartCertificateTask(const std::shared_ptr<AttestationFlowData>& data);

  // Posts |StartCertificateTask| if necessary, i.e., |data| indicates it is
  // certification flow instead of just enrollment.
  void PostStartCertificateTaskOrReturn(
      const std::shared_ptr<AttestationFlowData>& data);

  // Finishes the certificate request  with the response stored in |data|; also,
  // specifies the next |AttestationFlowAction| into |data| accordingly.
  void FinishCertificateTask(const std::shared_ptr<AttestationFlowData>& data);

  // If the status of |data| is success, it will call |data|s
  // |ReturnCertificate|, otherwise it will call |ReturnStatus|. Removes all
  // aliases of |data| from |certificate_queue_| and invokes the same method on
  // them (|ReturnCertificate| or |ReturnStatus|).
  void ReturnForAllCertificateRequestAliases(
      const std::shared_ptr<AttestationFlowData>& data);

  // Compute the enterprise DEN for attestation-based enrollment.
  std::string ComputeEnterpriseEnrollmentNonce();

  // Compute the enterprise EID for attestation-based enrollment.
  std::string ComputeEnterpriseEnrollmentId();

  base::WeakPtr<AttestationService> GetWeakPtr();

  FRIEND_TEST(AttestationServiceBaseTest, MigrateAttestationDatabase);
  FRIEND_TEST(AttestationServiceBaseTest,
              MigrateAttestationDatabaseWithCorruptedFields);
  FRIEND_TEST(AttestationServiceBaseTest,
              MigrateAttestationDatabaseAllEndorsementCredentials);
  FRIEND_TEST_ALL_PREFIXES(AttestationServiceEnterpriseTest,
                           SignEnterpriseChallengeSuccess);
  FRIEND_TEST_ALL_PREFIXES(AttestationServiceEnterpriseTest,
                           SignEnterpriseChallengeUseKeyForSPKAC);

  AttestationServiceMetrics metrics_;

  // Other than initialization and destruction, these are used only by the
  // worker thread.
  CryptoUtility* crypto_utility_{nullptr};
  Database* database_{nullptr};
  KeyStore* key_store_{nullptr};
  // |tpm_utility_| typically points to |default_tpm_utility_| created/destroyed
  // on the |worker_thread_|. As such, should not be accessed after that thread
  // is stopped/destroyed.
  TpmUtility* tpm_utility_{nullptr};
  std::string hwid_;
  CertRequestMap pending_cert_requests_;
  std::string system_salt_;
  brillo::SecureBlob* abe_data_;
  GoogleKeys google_keys_;
  // Default identity features for newly created identities.
  int default_identity_features_ =
      attestation::IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID;
  // Maps NVRAMQuoteType indices to indices into the static NVRAM data we
  // use for NVRAM quotes.
  std::map<NVRAMQuoteType, int> nvram_quote_type_to_index_data_;

  // Default implementations for the above interfaces. These will be setup
  // during Initialize() if the corresponding interface has not been set with a
  // mutator.

  // As |default_database_| has a reference of |default_crypto_utility_| and
  // |default_crypto_utility_| has a reference of |default_tpm_utility|,
  // the availabilities of these 2 variable follow the rule applied to
  // |default_tpm_utility_|. See the comment for |default_tpm_utility_| below.
  std::unique_ptr<CryptoUtilityImpl> default_crypto_utility_;
  std::unique_ptr<DatabaseImpl> default_database_;
  std::unique_ptr<Pkcs11KeyStore> default_key_store_;
  std::unique_ptr<chaps::TokenManagerClient> pkcs11_token_manager_;
  // |default_tpm_utility_| is created and destroyed on the |worker_thread_|,
  // and is not available after the thread is stopped/destroyed.
  std::unique_ptr<TpmUtility> default_tpm_utility_;

  std::unique_ptr<org::chromium::PcaAgentProxyInterface>
      default_pca_agent_proxy_;
  org::chromium::PcaAgentProxyInterface* pca_agent_proxy_{nullptr};

  // Enrollment statuses for respective ACA type is maintained in this array. By
  // default it is zero-initialized, i.e., EnrollmentStatus::kUnknown. Since
  // both dbus calling thread and worker thread both mutate the values, we use
  // atomic variables to prevent data race  and make sure the side effect is
  // propagated to other threads immediately.
  std::atomic<EnrollmentStatus> enrollment_statuses_[ACAType_ARRAYSIZE]{};

  // Used to store the requests during enrollment.
  EnrollmentQueue enrollment_queue_{kEnrollmentRequestLimit};

  // The certificate queue that is used to store the |AttestationFlowData|
  // aliases during certification.
  SynchronizedCertificateQueue certificate_queue_{
      kCertificateRequestAliasLimit};

  // All work is done in the background. This serves to serialize requests and
  // allow synchronous implementation of complex methods. This is intentionally
  // declared after the thread-owned members.
  std::unique_ptr<ServiceWorkerThread> worker_thread_;

  // The device policy provider, used to get device policy data.
  std::unique_ptr<policy::PolicyProvider> policy_provider_;

  // Declared last so any weak pointers are destroyed first.
  base::WeakPtrFactory<AttestationService> weak_factory_;
};

}  // namespace attestation

#endif  // ATTESTATION_SERVER_ATTESTATION_SERVICE_H_
