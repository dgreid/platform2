// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Cryptohome interface classes for cert_provision library.

#ifndef CRYPTOHOME_CERT_CERT_PROVISION_CRYPTOHOME_H_
#define CRYPTOHOME_CERT_CERT_PROVISION_CRYPTOHOME_H_

#include <memory>
#include <string>

#include <brillo/glib/dbus.h>
#include <brillo/secure_blob.h>

#include "cryptohome/cert/cert_provision_util.h"
#include "cryptohome/cert_provision.h"

namespace cert_provision {

// Proxy object for exchanging messages with cryptohomed over dbus.
class CryptohomeProxy {
 public:
  CryptohomeProxy() = default;

  virtual ~CryptohomeProxy() {}

  // Initializes the proxy.
  // Returns operation result.
  virtual OpResult Init() = 0;

  // Sends TpmIsAttestationPrepared to check if the tpm is owned and
  // enrollment is prepared.
  // Returns operation result. If successful, sets |is_prepared|.
  virtual OpResult CheckIfPrepared(bool* is_prepared) = 0;

  // Sends TpmIsAttestationEnrolled to check if the device is enrolled.
  // Returns operation result. If successful, sets |is_enrolled|.
  virtual OpResult CheckIfEnrolled(bool* is_enrolled) = 0;

  // Sends TpmAttestationCreateEnrollRequest to creates Enroll |request| for
  // the PCA of type |pca_type|.
  // Returns operation result.
  virtual OpResult CreateEnrollRequest(PCAType pca_type,
                                       brillo::SecureBlob* request) = 0;

  // Sends TpmAttestationEnroll to process Enroll |response| received from
  // the PCA of type |pca_type|.
  // Returns operation result.
  virtual OpResult ProcessEnrollResponse(
      PCAType pca_type, const brillo::SecureBlob& response) = 0;

  // Sends TpmAttestationCreateCertRequest to creates Sign |request| for the
  // PCA of type |pca_type| and the certificate of profile |cert_profile|.
  // Returns operation result.
  virtual OpResult CreateCertRequest(PCAType pca_type,
                                     CertificateProfile cert_profile,
                                     brillo::SecureBlob* request) = 0;

  // Sends TpmAttestationFinishCertRequest to process Sign |response| received
  // from the PCA and uses |label| to store the received data.
  // Returns operation result.
  virtual OpResult ProcessCertResponse(const std::string& label,
                                       const brillo::SecureBlob& response,
                                       brillo::SecureBlob* cert) = 0;

  // Sends TpmAttestationGetPublicKey to get the |public_key| for |label|.
  // Returns operation result.
  virtual OpResult GetPublicKey(const std::string& label,
                                brillo::SecureBlob* public_key) = 0;

  // Sends TpmAttestationRegisterKey to register the keypair from |label| in
  // the keystore.
  // Returns operation result.
  virtual OpResult Register(const std::string& label) = 0;

  // Returns a new scoped default CryptohomeProxy implementation unless
  // subst_obj is set. In that case, returns subst_obj.
  static Scoped<CryptohomeProxy> Create();
  static CryptohomeProxy* subst_obj;

 private:
  static std::unique_ptr<CryptohomeProxy> GetDefault();
};

class CryptohomeProxyImpl : public CryptohomeProxy {
 public:
  CryptohomeProxyImpl();
  CryptohomeProxyImpl(const CryptohomeProxyImpl&) = delete;
  CryptohomeProxyImpl& operator=(const CryptohomeProxyImpl&) = delete;

  OpResult Init() override;
  OpResult CheckIfPrepared(bool* is_prepared) override;
  OpResult CheckIfEnrolled(bool* is_enrolled) override;
  OpResult CreateEnrollRequest(PCAType pca_type,
                               brillo::SecureBlob* request) override;
  OpResult ProcessEnrollResponse(PCAType pca_type,
                                 const brillo::SecureBlob& response) override;
  OpResult CreateCertRequest(PCAType pca_type,
                             CertificateProfile cert_profile,
                             brillo::SecureBlob* request) override;
  OpResult ProcessCertResponse(const std::string& label,
                               const brillo::SecureBlob& response,
                               brillo::SecureBlob* cert) override;
  OpResult GetPublicKey(const std::string& label,
                        brillo::SecureBlob* public_key) override;
  OpResult Register(const std::string& label) override;

 private:
  // Default D-Bus timeout. Wait for up to 5 minutes - some operations are
  // slow or can be stuck in the cryptohomed queue behind slow operations.
  const int kDefaultTimeoutMs = 300000;

  OpResult DBusError(std::string request, ::GError* error) {
    return OpResult{Status::DBusError, request + " failed: " + error->message};
  }

  brillo::dbus::BusConnection bus_;
  brillo::dbus::Proxy proxy_;
  ::DBusGProxy* gproxy_;
};

}  // namespace cert_provision

#endif  // CRYPTOHOME_CERT_CERT_PROVISION_CRYPTOHOME_H_
