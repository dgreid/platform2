// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Library that provides certificate provisioning/signing interface.

#ifndef CRYPTOHOME_CERT_PROVISION_H_
#define CRYPTOHOME_CERT_PROVISION_H_

#include <string>

#include <base/callback.h>

// Use this for the interface exported by this library.
#define CERT_PROVISION_EXPORT __attribute__((__visibility__("default")))

namespace cert_provision {

enum class Status {
  Success = 0,
  NotPrepared = 1,
  NotProvisioned = 2,
  HttpError = 3,
  ServerError = 4,
  DBusError = 5,
  CryptohomeError = 6,
  KeyStoreError = 7,
  AttestationError = 8,
};

// Privacy CA types. These values match PCAType values in attestation.h,
// and are implicitly converted to cryptohome::Attestation::PCAType when
// calling cryptohome.
enum PCAType {
  kDefaultPCA = 0,  // The Google-operated Privacy CA.
  kTestPCA = 1,     // The test instance of the Google-operated Privacy CA.
};

// Attestation certificate profiles. These values match CertificateProfile
// values in attestation.proto, where descriptions for profiles are provided.
// They are implicitly converted to cryptohome::Attestation::PCAType when
// calling cryptohome.
enum CertificateProfile {
  ENTERPRISE_MACHINE_CERTIFICATE = 0,
  ENTERPRISE_USER_CERTIFICATE = 1,
  CONTENT_PROTECTION_CERTIFICATE = 2,
  CONTENT_PROTECTION_CERTIFICATE_WITH_STABLE_ID = 3,
  CAST_CERTIFICATE = 4,
  GFSC_CERTIFICATE = 5,
  JETSTREAM_CERTIFICATE = 6,
  ENTERPRISE_ENROLLMENT_CERTIFICATE = 7,
  XTS_CERTIFICATE = 8,
  ENTERPRISE_VTPM_EK_CERTIFICATE = 9,
};

// Supported signing mechanisms.
enum SignMechanism {
  SHA1_RSA_PKCS = 0,    // Sign SHA-1 hash using RSASSA-PKCS1-v1_5
  SHA256_RSA_PKCS = 1,  // Sign SHA-256 hash using RSASSA-PKCS1-v1_5
  SHA256_RSA_PSS = 2,
};

using ProgressCallback = base::Callback<void(
    Status status, int progress, const std::string& message)>;

// Synchronously obtains a new certificate with |cert_profile| from the PCA.
// The PCA is identified by the |pca_type|. If |pca_url| is not empty, it
// overrides the default PCA url. Stores the obtained certificate, its private
// and public keys in the keystore under |label|.
//
// |progress_callback| is called after major internal steps or on errors:
// - on steps: status is set to Status::Success, progress is the number between
//   0 and 100 that roughly defines the completeness percentage, and message
//   is the description of the current step.
// - on errors: status is set to the appropriate error, progress is set to 100,
//   and message provides error details.
//
// Returns Status::Success if the certificate was successfully obtained, and
// an appropriate other status on errors.
CERT_PROVISION_EXPORT Status
ProvisionCertificate(PCAType pca_type,
                     const std::string& pca_url,
                     const std::string& label,
                     CertificateProfile cert_profile,
                     const ProgressCallback& progress_callback);

// Synchronously obtains a new certificate with |cert_profile| from the PCA.
// The PCA is identified by the |pca_type|. Stores the obtained certificate, its
// private and public keys in the keystore under |label|.
//
// |progress_callback| is called after major internal steps or on errors:
// - on steps: status is set to Status::Success, progress is the number between
//   0 and 100 that roughly defines the completeness percentage, and message
//   is the description of the current step.
// - on errors: status is set to the appropriate error, progress is set to 100,
//   and message provides error details.
//
// Returns Status::Success if the certificate was successfully obtained, and
// an appropriate other status on errors.
CERT_PROVISION_EXPORT Status
ProvisionCertificate(PCAType pca_type,
                     const std::string& label,
                     CertificateProfile cert_profile,
                     const ProgressCallback& progress_callback);

// Enroll with the PCA regardless of the current status (re-enroll if already
// enrolled).
// The PCA is identified by the |pca_type|. If |pca_url| is not empty, it
// overrides the default PCA url.
//
// |progress_callback| is called after major internal steps or on errors:
// - on steps: status is set to Status::Success, progress is the number between
//   0 and 100 that roughly defines the completeness percentage, and message
//   is the description of the current step.
// - on errors: status is set to the appropriate error, progress is set to 100,
//   and message provides error details.
//
// Returns Status::Success if the certificate was successfully obtained, and
// an appropriate other status on errors.
CERT_PROVISION_EXPORT Status
ForceEnroll(PCAType pca_type,
            const std::string& pca_url,
            const ProgressCallback& progress_callback);

// Enroll with the PCA regardless of the current status (re-enroll if already
// enrolled). The PCA is identified by the |pca_type|.
//
// |progress_callback| is called after major internal steps or on errors:
// - on steps: status is set to Status::Success, progress is the number between
//   0 and 100 that roughly defines the completeness percentage, and message
//   is the description of the current step.
// - on errors: status is set to the appropriate error, progress is set to 100,
//   and message provides error details.
//
// Returns Status::Success if the certificate was successfully obtained, and
// an appropriate other status on errors.
CERT_PROVISION_EXPORT Status
ForceEnroll(PCAType pca_type, const ProgressCallback& progress_callback);

// Retrieves the provisioned certificate identified by |label| into |cert| in
// PEM format. If |include_intermediate| is true, all intermediate certificates
// in its chain are also obtained.
//
// Returns Status::Success if the certificate was successfully obtained, and
// an appropriate other status on errors.
CERT_PROVISION_EXPORT Status GetCertificate(const std::string& label,
                                            bool include_intermediate,
                                            std::string* cert);

// Signs |data| with the private key of the certificate identified by |label|
// using signing |mechanism|. Stores the resulting signature in |signature|.
//
// Returns Status::Success if the certificate was successfully obtained, and
// an appropriate other status on errors.
CERT_PROVISION_EXPORT Status Sign(const std::string& label,
                                  SignMechanism mechanism,
                                  const std::string& data,
                                  std::string* signature);

// Gets endorsement public key of the TPM.
CERT_PROVISION_EXPORT Status
GetEndorsementPublicKey(std::string* ek_public_key);

}  // namespace cert_provision

#endif  // CRYPTOHOME_CERT_PROVISION_H_
