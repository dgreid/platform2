// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ATTESTATION_H_
#define CRYPTOHOME_ATTESTATION_H_

#include <stdint.h>
#include <sys/types.h>

#include <map>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/scoped_observer.h>
#include <base/synchronization/lock.h>
#include <base/threading/platform_thread.h>
#include <brillo/http/http_transport.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <google/protobuf/map.h>
#include <openssl/evp.h>

#include "cryptohome/crypto.h"
#include "cryptohome/install_attributes.h"

#include "attestation.pb.h"  // NOLINT(build/include)

namespace cryptohome {

class KeyStore;
class Pkcs11KeyStore;
class Platform;
class Tpm;
class TpmInit;

// This class performs tasks which enable attestation enrollment.  These tasks
// include creating an AIK and recording all information about the AIK and EK
// that an attestation server will need to issue credentials for this system.
// If a platform does not have a TPM, this class does nothing.  This class is
// thread-safe.
class Attestation : public base::PlatformThread::Delegate,
                    public InstallAttributes::Observer {
 public:
  using BooleanMap = google::protobuf::Map<int, bool>;
  using IdentityCertificateMap = google::protobuf::
      Map<int, cryptohome::AttestationDatabase_IdentityCertificate>;

  static constexpr int kFirstIdentity = 0;

  enum PCAType {
    kDefaultPCA,  // The Google-operated Privacy CA.
    kTestPCA,     // The test version of the Google-operated Privacy CA.
    kMaxPCAType
  };

  enum PCARequestType {
    kEnroll,          // Enrolls a device, certifying an identity key.
    kGetCertificate,  // Issues a certificate for a TPM-backed key.
  };

  enum VAType {
    kDefaultVA,     // Verified Access servers.
    kTestVA,        // Test version of the Verified Access servers.
    kMaxVAType
  };

  Attestation();
  virtual ~Attestation();

  // Must be called before any other method. If |retain_endorsement_data| is set
  // then this instance will not perform any finalization of endorsement data.
  // That is, it will continue to be available in an unencrypted state.
  // The caller retains ownership of all pointers. If |abe_data| is not an
  // empty blob, its contents will be used to enable attestation-based
  // enterprise enrollment.
  virtual void Initialize(Tpm* tpm,
                          TpmInit* tpm_init,
                          Platform* platform,
                          Crypto* crypto,
                          InstallAttributes* install_attributes,
                          const brillo::SecureBlob& abe_data,
                          bool retain_endorsement_data);

  // Returns true if the attestation enrollment blobs already exist.
  virtual bool IsPreparedForEnrollment();

  // Returns true if the attestation enrollment blobs already exist for
  // a given PCA.
  virtual bool IsPreparedForEnrollmentWith(PCAType pca_type);

  // Returns true if an AIK certificate exist for one of the Privacy CAs.
  virtual bool IsEnrolled();

  // Returns an iterator pointing to the identity certificate for the given
  // |identity| and given Privacy CA.
  virtual IdentityCertificateMap::iterator FindIdentityCertificate(
      int identity, PCAType pca_type);

  // Returns whether there is an identity certificate for the given |identity|
  // and given Privacy CA.
  virtual bool HasIdentityCertificate(int identity, PCAType pca_type);

  // Creates a new identity and returns its index, or -1 if it could not be
  // created.
  virtual int CreateIdentity(int identity_features);

  // Returns the count of identities in the attestation database.
  virtual int GetIdentitiesCount() const;
  // Returns the identity features of |identity|.
  virtual int GetIdentityFeatures(int identity) const;

  // Returns a copy of the identity certificate map.
  virtual IdentityCertificateMap GetIdentityCertificateMap() const;

  // Creates attestation enrollment blobs if they do not already exist. This
  // includes extracting the EK information from the TPM and generating an AIK
  // to be used later during enrollment.
  virtual void PrepareForEnrollment();

  // Like PrepareForEnrollment(), but asynchronous.
  virtual void PrepareForEnrollmentAsync();

  // Verifies all attestation data as an attestation server would. Returns true
  // if all data is valid. If |is_cros_core| is true, checks that the EK is
  // endorsed for that configuration.
  virtual bool Verify(bool is_cros_core);

  // Verifies the EK certificate. If |is_cros_core| is true, checks that the EK
  // is endorsed for that configuration.
  virtual bool VerifyEK(bool is_cros_core);

  // Retrieves the EID for the device. The EID is computed and cached as
  // needed.
  // Returns false if a transient error prevents the obtention of the EID,
  // and true if the EID is successfully obtained or definitely cannot be
  // obtained.
  virtual bool GetEnterpriseEnrollmentId(
      brillo::SecureBlob* enterprise_enrollment_id);

  // Computes the enrollment id and populates the result in
  // |enterprise_enrollment_id|. If |abe_data_| or endorsement public key is
  // empty, the |enterprise_enrollment_id| is cleared.
  // Returns false if a transient error prevents the computation of the EID,
  // and true if the EID is successfully computed or definitely cannot be
  // computed.
  virtual bool ComputeEnterpriseEnrollmentId(
      brillo::SecureBlob* enterprise_enrollment_id);

  // Creates an enrollment request to be sent to the Privacy CA. This request
  // is a serialized AttestationEnrollmentRequest protobuf. Attestation
  // enrollment is a process by which the Privacy CA verifies the EK certificate
  // of a device and issues a certificate for an AIK. The enrollment process can
  // be finished by calling Enroll() with the response from the Privacy CA. This
  // method can only succeed if IsPreparedForEnrollment() returns true.
  //
  // Parameters
  //   pca_type - Specifies to which Privacy CA the request will be sent.
  //   pca_request - The request to be sent to the Privacy CA.
  //
  // Returns true on success.
  virtual bool CreateEnrollRequest(PCAType pca_type,
                                   brillo::SecureBlob* pca_request);

  // Finishes the enrollment process. On success, IsEnrolled() will return true
  // and GetIdentityCertificate(kFirstIdentity, |pca_type|) will return a
  // non null value.
  //
  // The response from the Privacy CA is a serialized
  // AttestationEnrollmentResponse protobuf. This method recovers the AIK
  // certificate by calling TPM_ActivateIdentity and stores this certificate to
  // be used later during a certificate request.
  //
  // Parameters
  //   pca_type - Specifies which Privacy CA created the response.
  //   pca_response - The Privacy CA's response to an enrollment request as
  //                  returned by CreateEnrollRequest().
  //
  // Returns true on success.
  virtual bool Enroll(PCAType pca_type,
                      const brillo::SecureBlob& pca_response);

  // Creates an attestation certificate request to be sent to the Privacy CA.
  // The request is a serialized AttestationCertificateRequest protobuf. The
  // certificate request process generates and certifies a key in the TPM and
  // sends the AIK certificate along with information about the certified key to
  // the Privacy CA. The PCA verifies the information and issues a certificate
  // for the certified key. The certificate request process can be finished by
  // calling FinishCertRequest() with the response from the Privacy CA. This
  // method can succeed iff GetIdentityCertificate(kFirstIdentity, |pca_type|)
  // returns a non-null value.
  //
  // Parameters
  //   pca_type - Specifies to which Privacy CA the request will be sent.
  //   profile - Specifies the type of certificate to be requested.
  //   username - The user requesting the certificate.
  //   origin - Some certificate requests require information about the origin
  //            of the request.  If no origin is needed, this can be empty.
  //   pca_request - The request to be sent to the Privacy CA.
  //
  // Returns true on success.
  virtual bool CreateCertRequest(PCAType pca_type,
                                 CertificateProfile profile,
                                 const std::string& username,
                                 const std::string& origin,
                                 brillo::SecureBlob* pca_request);

  // Finishes the certificate request process. The |pca_response| from the
  // Privacy CA is a serialized AttestationCertificateResponse protobuf. This
  // final step verifies that the Privacy CA operation succeeded and extracts
  // the certificate for the certified key. The certificate is stored with the
  // key in association with the |key_name|. If the key is a user-specific key
  // it is stored in association with the currently signed-in user.
  //
  // Parameters
  //   pca_response - The Privacy CA's response to a certificate request as
  //                  returned by CreateCertRequest().
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The canonical username of the active user profile.  Ignored if
  //              |is_user_specific| is false.
  //   key_name - A name for the key.  If a key already exists with this name it
  //              will be destroyed and replaced with this one.  This name may
  //              subsequently be used to query the certificate for the key
  //              using GetCertificateChain.
  //   certificate_chain - The PCA issued certificate chain in PEM format.  By
  //                       convention the certified key certificate is listed
  //                       first followed by intermediate CA certificate(s).
  //                       The PCA root certificate is not included.
  virtual bool FinishCertRequest(const brillo::SecureBlob& pca_response,
                                 bool is_user_specific,
                                 const std::string& username,
                                 const std::string& key_name,
                                 brillo::SecureBlob* certificate_chain);

  // Gets the PCA issued certificate chain for an existing certified key.
  // Returns true on success.
  //
  // Parameters
  //
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The canonical username of the active user profile.  Ignored if
  //              |is_user_specific| is false.
  //   key_name - The key name; this is the same name previously passed to
  //              FinishCertRequest.
  //   certificate_chain - The PCA issued certificate chain in the same format
  //                       used by FinishCertRequest.
  virtual bool GetCertificateChain(bool is_user_specific,
                                   const std::string& username,
                                   const std::string& key_name,
                                   brillo::SecureBlob* certificate_chain);

  // Gets the public key for an existing certified key.  Returns true on
  // success.  This method is provided for convenience, the same public key can
  // also be extracted from the certificate chain.
  //
  // Parameters
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The canonical username of the active user profile.  Ignored if
  //              |is_user_specific| is false.
  //   key_name - The key name; this is the same name previously passed to
  //              FinishCertRequest.
  //   public_key - The public key, in DER-encoded X.509 SubjectPublicKeyInfo
  //                format.
  virtual bool GetPublicKey(bool is_user_specific,
                            const std::string& username,
                            const std::string& key_name,
                            brillo::SecureBlob* public_key);

  // Returns true iff a given key exists.
  virtual bool DoesKeyExist(bool is_user_specific,
                            const std::string& username,
                            const std::string& key_name);

  // Signs a challenge for an enterprise device / user.  This challenge is
  // typically generated by and the response verified by DMServer.  Returns true
  // on success.
  //
  // This method uses the production Verified Access servers.
  //
  // Parameters
  //
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The canonical username of the active user profile.  Ignored if
  //              |is_user_specific| is false.
  //   key_name - The key name; this is the same name previously passed to
  //              FinishCertRequest.
  //   domain - A domain value to be included in the challenge response.  The
  //            value is opaque to this class.
  //   device_id - A device identifier to be included in the challenge response.
  //               This value is opaque to this class.
  //   include_signed_public_key - Whether the challenge response should include
  //                               a SignedPublicKeyAndChallenge.
  //   challenge - The challenge to be signed.
  //   response - On success is populated with the challenge response.
  virtual bool SignEnterpriseChallenge(bool is_user_specific,
                                       const std::string& username,
                                       const std::string& key_name,
                                       const std::string& domain,
                                       const brillo::SecureBlob& device_id,
                                       bool include_signed_public_key,
                                       const brillo::SecureBlob& challenge,
                                       brillo::SecureBlob* response);

  // Signs a challenge for an enterprise device / user.  This challenge is
  // typically generated by and the response verified by DMServer.  Returns true
  // on success.
  //
  // Parameters
  //
  //   va_type - The kind of Verified Access servers to use.
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The canonical username of the active user profile.  Ignored if
  //              |is_user_specific| is false.
  //   key_name - The key name; this is the same name previously passed to
  //              FinishCertRequest.
  //   domain - A domain value to be included in the challenge response.  The
  //            value is opaque to this class.
  //   device_id - A device identifier to be included in the challenge response.
  //               This value is opaque to this class.
  //   include_signed_public_key - Whether the challenge response should include
  //                               a SignedPublicKeyAndChallenge.
  //   key_name_for_spkac - The name of the key used for
  //                        SignedPublicKeyAndChallenge. If empty, the same key
  //                        used to sign the challenge response will be used
  //                        for SignedPublicKeyAndChallenge.
  //   challenge - The challenge to be signed.
  //   response - On success is populated with the challenge response.
  virtual bool SignEnterpriseVaChallenge(
      VAType va_type,
      bool is_user_specific,
      const std::string& username,
      const std::string& key_name,
      const std::string& domain,
      const brillo::SecureBlob& device_id,
      bool include_signed_public_key,
      const brillo::SecureBlob& challenge,
      const std::string& key_name_for_spkac,
      brillo::SecureBlob* response);

  // Signs a challenge outside of an enterprise context.  This challenge is
  // typically generated by some module running within Chrome.  Returns true
  // on success.
  //
  // Parameters
  //
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The canonical username of the active user profile.  Ignored if
  //              |is_user_specific| is false.
  //   key_name - The key name; this is the same name previously passed to
  //              FinishCertRequest.
  //   challenge - The challenge to be signed.
  //   response - On success is populated with the challenge response.
  virtual bool SignSimpleChallenge(bool is_user_specific,
                                   const std::string& username,
                                   const std::string& key_name,
                                   const brillo::SecureBlob& challenge,
                                   brillo::SecureBlob* response);

  // Registers a key with the user's PKCS #11 token.  On success, the key is
  // removed from its old location and will no longer be available via this
  // interface.  Returns true on success.
  //
  // Parameters
  //
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The canonical username of the active user profile.  Ignored if
  //              |is_user_specific| is false.
  //   key_name - The key name; this is the same name previously passed to
  //              FinishCertRequest.
  //   include_certificates - Whether to also register the certificate chain
  //                          associated with the key.
  virtual bool RegisterKey(bool is_user_specific,
                           const std::string& username,
                           const std::string& key_name,
                           bool include_certificates);

  // Gets a payload previously set for a key.  If the key exists but no payload
  // has been set, the payload will be empty.  Returns true on success.
  //
  // Parameters
  //
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The canonical username of the active user profile.  Ignored if
  //              |is_user_specific| is false.
  //   key_name - The key name; this is the same name previously passed to
  //              FinishCertRequest.
  //   payload - Receives the payload data.
  virtual bool GetKeyPayload(bool is_user_specific,
                             const std::string& key_name,
                             const std::string& username,
                             brillo::SecureBlob* payload);

  // Sets a payload for a key; any previous payload will be overwritten.
  // Returns true on success.
  //
  // Parameters
  //
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The canonical username of the active user profile.  Ignored if
  //              |is_user_specific| is false.
  //   key_name - The key name; this is the same name previously passed to
  //              FinishCertRequest.
  //   payload - The payload data.
  virtual bool SetKeyPayload(bool is_user_specific,
                             const std::string& key_name,
                             const std::string& username,
                             const brillo::SecureBlob& payload);

  // Deletes all keys where the key name has the given |key_prefix|.
  // Returns true on success.
  //
  // Parameters
  //
  //   is_user_specific - Whether the key is associated with the current user.
  //   username - The current user canonical email address.
  //   key_prefix - The key name prefix.
  virtual bool DeleteKeysByPrefix(bool is_user_specific,
                                  const std::string& username,
                                  const std::string& key_prefix);

  // Gets the TPM Endorsement Key (EK) certificate and returns it in PEM format
  // in addition to a hex SHA256 hash of the raw DER encoded certificate.  The
  // result is intended to be human readable and is always valid ASCII.  Returns
  // true on success.  This method requires the TPM owner password to be
  // available.
  virtual bool GetEKInfo(std::string* ek_info);

  // Creates a request to be sent to the PCA which will reset the identity for
  // this device on future AIK enrollments.  The |reset_token| is put in the
  // request protobuf verbatim.  On success returns true and copies the
  // serialized request into |reset_request|.
  virtual bool GetIdentityResetRequest(const std::string& reset_token,
                                       brillo::SecureBlob* reset_request);

  // Helper method to determine PCR0 status. Returns true iff the current PCR0
  // value shows a verified boot measurement.
  virtual bool IsPCR0VerifiedMode();

  // Creates or migrates the default identity and other data. Returns whether
  // data were migrated or not.
  virtual bool MigrateAttestationDatabase();

  // Ensures all endorsement data which uniquely identifies the device no longer
  // exists in the attestation database unencrypted.  Encrypted endorsement data
  // cannot be decrypted locally.
  virtual void FinalizeEndorsementData();

  // Provides the owner delegate credentials normally used for AIK activation.
  // Returns true on success.
  virtual bool GetDelegateCredentials(brillo::Blob* blob,
                                      brillo::Blob* secret,
                                      bool* has_reset_lock_permissions);

  // Provides cached |ek_public_key| and |ek_certificate| if they exist. If only
  // the public key is found then it will be provided and the certificate will
  // be empty. Returns false if the public key is not found (i.e. not cached).
  virtual bool GetCachedEndorsementData(brillo::SecureBlob* ek_public_key,
                                        brillo::SecureBlob* ek_certificate);

  // Caches the endorsement public key if the TPM is not owned. If the TPM is
  // owned or the public key is already known this will have no effect.
  virtual void CacheEndorsementData();

  // Sends a |request| to a Privacy CA and waits for the |reply|. This is a
  // blocking call. Returns true on success.
  virtual bool SendPCARequestAndBlock(PCAType pca_type,
                                      PCARequestType request_type,
                                      const brillo::SecureBlob& request,
                                      brillo::SecureBlob* reply);

  // Returns a pointer to the enterprise signing key for the |va_type| VA
  // servers, creating one as needed.
  virtual RSA* GetEnterpriseSigningKey(VAType va_type);

  // Returns a pointer to the enterprise encryption key for the |va_type| VA
  // servers, creating one as needed.
  virtual RSA* GetEnterpriseEncryptionKey(VAType va_type);

  // Returns the public key ID for |va_type| VA servers. Note that this
  // key may have embedded zero characters.
  virtual std::string GetEnterpriseEncryptionPublicKeyID(VAType type) const;

  // Sets an alternative attestation database location. Useful in testing.
  virtual void set_database_path(const char* path) {
    database_path_ = base::FilePath(path);
  }

  // Sets an alternate key store.
  virtual void set_key_store(KeyStore* key_store) {
    key_store_ = key_store;
  }

  // Sets enterprise keys for testing. The receiver takes ownership of
  // the keys.
  virtual void set_enterprise_test_keys(VAType va_type,
                                        RSA* signing_key,
                                        RSA* encryption_key);

  virtual void set_http_transport(
      std::shared_ptr<brillo::http::Transport> transport) {
    http_transport_ = transport;
  }

  // PlatformThread::Delegate interface.
  virtual void ThreadMain() { PrepareForEnrollment(); }

  // InstallAttributes::Observer interface.
  virtual void OnFinalized() { PrepareForEnrollmentAsync(); }

  // Used by test.
  void set_default_identity_features_for_test(int default_identity_features);

 private:
  typedef std::map<std::string, brillo::SecureBlob> CertRequestMap;
  enum FirmwareType {
    kUnknown,
    kVerified,
    kDeveloper
  };
  // So we can use std::unique_ptr with openssl types.
  struct X509Deleter {
    inline void operator()(void* ptr) const;
  };
  struct NETSCAPE_SPKIDeleter {
    inline void operator()(void* ptr) const;
  };
  // Just enough CA information to do a basic verification.
  struct CertificateAuthority {
    const char* issuer;
    const char* modulus;  // In hex format.
  };
  // A map of enterprise keys by Verified Access server type.
  using KeysMap = std::map<VAType, crypto::ScopedRSA>;
  static const size_t kQuoteExternalDataSize;
  static const size_t kCipherKeySize;
  static const size_t kNonceSize;
  static const size_t kDigestSize;
  static const size_t kChallengeSignatureNonceSize;
  static const mode_t kDatabasePermissions;
  static const char kDatabaseOwner[];
  static const char kDefaultDatabasePath[];
  static const char kDefaultPCAPublicKey[];
  static const char kDefaultPCAPublicKeyID[];
  static const char kDefaultPCAWebOrigin[];
  static const char kTestPCAPublicKey[];
  static const char kTestPCAPublicKeyID[];
  static const char kTestPCAWebOrigin[];
  static const char kDefaultEnterpriseSigningPublicKey[];
  static const char kDefaultEnterpriseEncryptionPublicKey[];
  static const char kDefaultEnterpriseEncryptionPublicKeyID[];
  static const char kTestEnterpriseSigningPublicKey[];
  static const char kTestEnterpriseEncryptionPublicKey[];
  static const char kTestEnterpriseEncryptionPublicKeyID[];
  static const CertificateAuthority kKnownEndorsementCA[];
  static const CertificateAuthority kKnownCrosCoreEndorsementCA[];
  static const struct PCRValue {
    bool developer_mode_enabled;
    bool recovery_mode_enabled;
    FirmwareType firmware_type;
  } kKnownPCRValues[];
  // ASN.1 DigestInfo header for SHA-256 (see PKCS #1 v2.1 section 9.2).
  static const unsigned char kSha256DigestInfo[];
  static const int kNumTemporalValues;
  // Install attribute names for alternate PCA attributes.
  static const char kAlternatePCAKeyAttributeName[];
  static const char kAlternatePCAKeyIDAttributeName[];
  static const char kAlternatePCAUrlAttributeName[];
  // Context name to derive the device stable secret for attestation-based
  // enterprise enrollment.
  static const char kAttestationBasedEnterpriseEnrollmentContextName[];

  Tpm* tpm_;
  TpmInit* tpm_init_;
  Platform* platform_;
  Crypto* crypto_;
  brillo::SecureBlob abe_data_;
  // A lock to protect |database_pb_| because PrepareForEnrollment may happen on
  // a worker thread.
  mutable base::Lock lock_;
  brillo::SecureBlob database_key_;
  brillo::SecureBlob sealed_database_key_;
  brillo::SecureBlob enterprise_enrollment_id_;
  base::FilePath database_path_;
  AttestationDatabase database_pb_;
  base::PlatformThreadHandle thread_;
  CertRequestMap pending_cert_requests_;
  std::unique_ptr<Pkcs11KeyStore> pkcs11_key_store_;
  KeyStore* key_store_;
  KeysMap enterprise_signing_keys_;
  KeysMap enterprise_encryption_keys_;
  InstallAttributes* install_attributes_;
  ScopedObserver<InstallAttributes, InstallAttributes::Observer>
      install_attributes_observer_;
  // Don't use directly, use IsTPMReady() instead.
  bool is_tpm_ready_;
  bool is_prepare_in_progress_;
  bool retain_endorsement_data_;
  // Can be used to override the default transport (e.g. during testing).
  std::shared_ptr<brillo::http::Transport> http_transport_;
  // User and group for ownership of the database file.
  uid_t attestation_user_;
  gid_t attestation_group_;
  // Default identity features for newly created identities.
  int default_identity_features_ =
      cryptohome::IDENTITY_FEATURE_ENTERPRISE_ENROLLMENT_ID;

  // Serializes and encrypts an attestation database.
  bool
  EncryptDatabase(const AttestationDatabase& db,
                  std::string* serial_encrypted_db);

  // Decrypts and parses an attestation database.
  bool DecryptDatabase(const std::string& serial_encrypted_db,
                       AttestationDatabase* db);

  // Writes an encrypted database to a persistent storage location.
  bool StoreDatabase(const std::string& serial_encrypted_db);

  // Reads a database from a persistent storage location.
  bool LoadDatabase(std::string* serial_encrypted_db);

  // Persists any changes made to database_pb_.
  bool PersistDatabaseChanges();

  // Persists a specific database. Useful for unit tests to set state.
  bool PersistDatabase(const AttestationDatabase& db);

  // Ensures permissions of the database file are correct.
  void CheckDatabasePermissions();

  // Retrieves the EID for the device.  The EID is cached as needed.
  // Returns true on success.
  Tpm::TpmRetryAction GetEnterpriseEnrollmentIdInternal(
      brillo::SecureBlob* enterprise_enrollment_id);

  // Computes the enrollment id and populates the result in
  // |enterprise_enrollment_id|. If |abe_data_| or endorsement public key is
  // empty, the |enterprise_enrollment_id| is cleared.
  Tpm::TpmRetryAction ComputeEnterpriseEnrollmentIdInternal(
      brillo::SecureBlob* enterprise_enrollment_id);

  // Creates a new identity and returns its index, or -1 if it could not be
  // created.
  int CreateIdentity(const int identity_features,
                     const brillo::SecureBlob& ek_public_key);

  // Verifies an endorsement credential against known Chrome OS issuers. If
  // |is_cros_core| is true, checks that the EK is endorsed for that
  // configuration.
  bool VerifyEndorsementCredential(const brillo::SecureBlob& credential,
                                   const brillo::SecureBlob& public_key,
                                   bool is_cros_core);

  // Verifies identity key binding data.
  bool VerifyIdentityBinding(const IdentityBinding& binding);

  // Verifies a quote of PCR0.
  bool VerifyPCR0Quote(const brillo::SecureBlob& aik_public_key,
                       const Quote& quote);

  // Verifies a quote of PCR1.
  bool VerifyPCR1Quote(const brillo::SecureBlob& aik_public_key,
                       const Quote& quote);

  // Verifies that a quote signature is valid and matches the quoted data.
  bool VerifyQuoteSignature(const brillo::SecureBlob& aik_public_key,
                            const Quote& quote,
                            uint32_t pcr_index);

  // Verifies a certified key.
  bool VerifyCertifiedKey(const brillo::SecureBlob& aik_public_key,
                          const brillo::SecureBlob& certified_public_key,
                          const brillo::SecureBlob& certified_key_info,
                          const brillo::SecureBlob& proof);

  // Creates a public key based on a known credential issuer.
  crypto::ScopedEVP_PKEY GetAuthorityPublicKey(
      const char* issuer_name,
      bool is_cros_core);

  // Verifies an RSA-PKCS1-SHA1 digital signature.
  bool VerifySignature(const brillo::SecureBlob& public_key,
                       const brillo::SecureBlob& signed_data,
                       const brillo::SecureBlob& signature);

  // Finds the index of the first identity certificate for a PCA. Returns
  // the number of identity certificates if not found.
  int GetIndexOfFirstIdentityCertificate(PCAType pca_type);

  // Copies the deprecated identity-related data into the first identity.
  // Returns whether data were migrated or not.
  bool MigrateIdentityData();

  // Clears the memory of the database protobuf.
  void ClearDatabase();

  // Clears the memory of a Quote protobuf.
  void ClearQuote(Quote* quote);

  // Clears the memory of all identity-related data.
  void ClearIdentity(AttestationDatabase::Identity* identity);

  // Clears the memory of an identity certificate.
  void ClearIdentityCertificate(
      AttestationDatabase::IdentityCertificate* identity_certificate);

  // Clears the memory of a std::string.
  void ClearString(std::string* s);

  // Performs AIK activation with a fake credential.
  bool VerifyActivateIdentity(const brillo::Blob& delegate_blob,
                              const brillo::Blob& delegate_secret,
                              const brillo::SecureBlob& identity_key_blob,
                              const brillo::SecureBlob& identity_public_key,
                              const brillo::SecureBlob& ek_public_key);

  // Encrypts the endorsement credential with the Privacy CA public key.
  bool EncryptEndorsementCredential(PCAType pca_type,
                                    const brillo::SecureBlob& credential,
                                    EncryptedData* encrypted_credential);

  // Adds named device-wide key to the attestation database.
  bool AddDeviceKey(const std::string& key_name, const CertifiedKey& key);

  // Removes a device-wide key from the attestation database.
  void RemoveDeviceKey(const std::string& key_name);

  // Finds a key by name.
  bool FindKeyByName(bool is_user_specific,
                     const std::string& username,
                     const std::string& key_name,
                     CertifiedKey* key);

  // Saves a key either in the device or user key store.
  bool SaveKey(bool is_user_specific,
               const std::string& username,
               const std::string& key_name,
               const CertifiedKey& key);

  // Deletes a key either from the device or user key store.
  void DeleteKey(bool is_user_specific,
                 const std::string& username,
                 const std::string& key_name);

  // Assembles a certificate chain in PEM format for a certified key. By
  // convention, the leaf certificate will be first.
  bool CreatePEMCertificateChain(const CertifiedKey& key,
                                 brillo::SecureBlob* certificate_chain);

  // Creates a certificate in PEM format from a DER encoded X.509 certificate.
  std::string CreatePEMCertificate(const std::string& certificate);

  // Signs data with a certified key using the scheme specified for challenges
  // and outputs a serialized SignedData protobuf.
  bool SignChallengeData(const CertifiedKey& key,
                         const brillo::SecureBlob& challenge,
                         brillo::SecureBlob* response);

  // Validates incoming enterprise challenge data.
  bool ValidateEnterpriseChallenge(VAType va_type,
                                   const SignedData& signed_challenge);

  // Encrypts a KeyInfo protobuf as required for an enterprise challenge
  // response.
  bool EncryptEnterpriseKeyInfo(VAType va_type, const KeyInfo& key_info,
                                EncryptedData* encrypted_data);

  // Encrypts data into an EncryptedData protobuf, wrapping the encryption key
  // with |wrapping_key|.
  bool EncryptData(const brillo::SecureBlob& input,
                   RSA* wrapping_key,
                   const std::string& wrapping_key_id,
                   EncryptedData* output);

  // Creates an RSA* given a modulus in hex format.  The exponent is always set
  // to 65537.  If an error occurs, NULL is returned.
  crypto::ScopedRSA CreateRSAFromHexModulus(const std::string& hex_modulus);

  // Creates a SignedPublicKeyAndChallenge with a random challenge.
  bool CreateSignedPublicKey(const CertifiedKey& key,
                             brillo::SecureBlob* signed_public_key);

  // Standard AES-256-CBC padded encryption.
  bool AesEncrypt(const brillo::SecureBlob& plaintext,
                  const brillo::SecureBlob& key,
                  const brillo::SecureBlob& iv,
                  brillo::SecureBlob* ciphertext);

  // Standard AES-256-CBC padded decryption.
  bool AesDecrypt(const brillo::SecureBlob& ciphertext,
                  const brillo::SecureBlob& key,
                  const brillo::SecureBlob& iv,
                  brillo::SecureBlob* plaintext);

  // Encrypts data in a TSS compatible way using AES-256-CBC.
  //
  // Parameters
  //   key - The AES key.
  //   input - The data to be encrypted.
  //   output - The encrypted data.
  bool TssCompatibleEncrypt(const brillo::SecureBlob& key,
                            const brillo::SecureBlob& input,
                            brillo::SecureBlob* output);

  // Chooses a temporal index which will be used by the PCA to create a
  // certificate.  This decision factors in the currently signed-in |user| and
  // the |origin| of the certificate request.  The strategy is to find an index
  // which has not already been used by another user for the same origin.
  int ChooseTemporalIndex(const std::string& user, const std::string& origin);

  // Returns true if the TPM is ready.
  bool IsTPMReady();

  // If PCR1 is clear (i.e. all 0 bytes), extends the PCR with the HWID. This is
  // a fallback if the device firmware does not already do this.
  void ExtendPCR1IfClear();

  // Quote the given PCR and fill the result in the Quote object
  bool CreatePCRQuote(uint32_t pcr_index,
                      const brillo::SecureBlob& identity_key_blob,
                      Quote* output);

  // Creates a PCA URL for the given |pca_type| and |request_type|.
  std::string GetPCAURL(PCAType pca_type, PCARequestType request_type) const;

  // Retrieves the endorsement public key from cache or by asking the TPM if
  // possible.
  Tpm::TpmRetryAction GetTpmEndorsementPublicKey(
      brillo::SecureBlob* ek_public_key);

  // Computes the enterprise DEN for attestation-based enrollment and
  // stores it in |enterprise_enrollment_nonce|.
  void ComputeEnterpriseEnrollmentNonce(
      brillo::SecureBlob* enterprise_enrollment_nonce);

  // Injects a TpmInit object to be used for RemoveTpmOwnerDependency
  void set_tpm_init(TpmInit* value) { tpm_init_ = value; }

  friend class AttestationBaseTest;
  FRIEND_TEST(AttestationBaseTest, MigrateAttestationDatabase);
  FRIEND_TEST(AttestationBaseTest,
              MigrateAttestationDatabaseWithCorruptedFields);
  FRIEND_TEST(AttestationBaseTest,
              MigrateAttestationDatabaseAllEndorsementCredentials);
  FRIEND_TEST(AttestationTest, IsAttestationPreparedForOnePca);
  FRIEND_TEST(AttestationEnrollmentIdTest,
              ComputeEnterpriseEnrollmentIdHasDelegate);

  DISALLOW_COPY_AND_ASSIGN(Attestation);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_ATTESTATION_H_
