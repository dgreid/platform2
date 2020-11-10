// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/pkcs11_keystore.h"

#include <string>

#include <base/bind.h>
#include <base/callback.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <chaps/pkcs11/cryptoki.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <crypto/libcrypto-compat.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/pkcs11_init.h"

using base::FilePath;
using brillo::SecureBlob;

namespace cryptohome {

typedef crypto::ScopedOpenSSL<X509, X509_free> ScopedX509;

// An arbitrary application ID to identify PKCS #11 objects.
const char kApplicationID[] = "CrOS_d5bbc079d2497110feadfc97c40d718ae46f4658";

// A helper class to scope a PKCS #11 session.
class ScopedSession {
 public:
  explicit ScopedSession(CK_SLOT_ID slot) : handle_(CK_INVALID_HANDLE) {
    CK_RV rv = C_Initialize(NULL);
    if (rv != CKR_OK && rv != CKR_CRYPTOKI_ALREADY_INITIALIZED) {
      // This may be normal in a test environment.
      LOG(INFO) << "PKCS #11 is not available.";
      return;
    }
    CK_FLAGS flags = CKF_RW_SESSION | CKF_SERIAL_SESSION;
    if (C_OpenSession(slot, flags, NULL, NULL, &handle_) != CKR_OK) {
      LOG(ERROR) << "Failed to open PKCS #11 session.";
      return;
    }
  }
  ScopedSession(const ScopedSession&) = delete;
  ScopedSession& operator=(const ScopedSession&) = delete;

  ~ScopedSession() {
    if (IsValid() && (C_CloseSession(handle_) != CKR_OK)) {
      LOG(WARNING) << "Failed to close PKCS #11 session.";
      handle_ = CK_INVALID_HANDLE;
    }
  }

  CK_SESSION_HANDLE handle() { return handle_; }

  bool IsValid() { return (handle_ != CK_INVALID_HANDLE); }

 private:
  CK_SESSION_HANDLE handle_;
};

Pkcs11KeyStore::Pkcs11KeyStore()
    : default_pkcs11_init_(new Pkcs11Init),
      pkcs11_init_(default_pkcs11_init_.get()) {}

Pkcs11KeyStore::Pkcs11KeyStore(Pkcs11Init* pkcs11_init)
    : pkcs11_init_(pkcs11_init) {}

Pkcs11KeyStore::~Pkcs11KeyStore() {}

bool Pkcs11KeyStore::Read(bool is_user_specific,
                          const std::string& username,
                          const std::string& key_name,
                          SecureBlob* key_data) {
  CK_SLOT_ID slot;
  if (!GetUserSlot(is_user_specific, username, &slot))
    return false;
  ScopedSession session(slot);
  if (!session.IsValid())
    return false;
  CK_OBJECT_HANDLE key_handle = FindObject(session.handle(), key_name);
  if (key_handle == CK_INVALID_HANDLE)
    return false;
  // First get the attribute with a NULL buffer which will give us the length.
  CK_ATTRIBUTE attribute = {CKA_VALUE, NULL, 0};
  if (C_GetAttributeValue(session.handle(), key_handle, &attribute, 1) !=
      CKR_OK) {
    LOG(ERROR) << "Pkcs11KeyStore: Failed to read key data: " << key_name;
    return false;
  }
  SecureBlob value_buffer(attribute.ulValueLen);
  attribute.pValue = value_buffer.data();
  if (C_GetAttributeValue(session.handle(), key_handle, &attribute, 1) !=
      CKR_OK) {
    LOG(ERROR) << "Pkcs11KeyStore: Failed to read key data: " << key_name;
    return false;
  }
  key_data->swap(value_buffer);
  return true;
}

bool Pkcs11KeyStore::Write(bool is_user_specific,
                           const std::string& username,
                           const std::string& key_name,
                           const SecureBlob& key_data) {
  // Delete any existing key with the same name.
  if (!Delete(is_user_specific, username, key_name))
    return false;
  CK_SLOT_ID slot;
  if (!GetUserSlot(is_user_specific, username, &slot))
    return false;
  ScopedSession session(slot);
  if (!session.IsValid())
    return false;
  // Create a new data object for the key.
  CK_OBJECT_CLASS object_class = CKO_DATA;
  CK_BBOOL true_value = CK_TRUE;
  CK_BBOOL false_value = CK_FALSE;
  CK_ATTRIBUTE attributes[] = {
      {CKA_CLASS, &object_class, sizeof(object_class)},
      {CKA_LABEL, const_cast<char*>(key_name.data()), key_name.size()},
      {CKA_VALUE, const_cast<uint8_t*>(key_data.data()), key_data.size()},
      {CKA_APPLICATION, const_cast<char*>(kApplicationID),
       base::size(kApplicationID)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_MODIFIABLE, &false_value, sizeof(false_value)}};
  CK_OBJECT_HANDLE key_handle = CK_INVALID_HANDLE;
  if (C_CreateObject(session.handle(), attributes, base::size(attributes),
                     &key_handle) != CKR_OK) {
    LOG(ERROR) << "Pkcs11KeyStore: Failed to write key data: " << key_name;
    return false;
  }
  return true;
}

bool Pkcs11KeyStore::Delete(bool is_user_specific,
                            const std::string& username,
                            const std::string& key_name) {
  CK_SLOT_ID slot;
  if (!GetUserSlot(is_user_specific, username, &slot))
    return false;
  ScopedSession session(slot);
  if (!session.IsValid())
    return false;
  CK_OBJECT_HANDLE key_handle = FindObject(session.handle(), key_name);
  if (key_handle != CK_INVALID_HANDLE) {
    if (C_DestroyObject(session.handle(), key_handle) != CKR_OK) {
      LOG(ERROR) << "Pkcs11KeyStore: Failed to delete key data.";
      return false;
    }
  }
  return true;
}

bool Pkcs11KeyStore::DeleteByPrefix(bool is_user_specific,
                                    const std::string& username,
                                    const std::string& key_prefix) {
  CK_SLOT_ID slot;
  if (!GetUserSlot(is_user_specific, username, &slot))
    return false;
  ScopedSession session(slot);
  if (!session.IsValid())
    return false;
  EnumObjectsCallback callback =
      base::Bind(&Pkcs11KeyStore::DeleteIfMatchesPrefix, base::Unretained(this),
                 session.handle(), key_prefix);
  if (!EnumObjects(session.handle(), callback)) {
    LOG(ERROR) << "Pkcs11KeyStore: Failed to delete key data.";
    return false;
  }
  return true;
}

bool Pkcs11KeyStore::Register(bool is_user_specific,
                              const std::string& username,
                              const std::string& label,
                              const brillo::SecureBlob& private_key_blob,
                              const brillo::SecureBlob& public_key_der,
                              const brillo::SecureBlob& certificate) {
  const CK_ATTRIBUTE_TYPE kKeyBlobAttribute = CKA_VENDOR_DEFINED + 1;

  CK_SLOT_ID slot;
  if (!GetUserSlot(is_user_specific, username, &slot))
    return false;
  ScopedSession session(slot);
  if (!session.IsValid())
    return false;

  // Extract the modulus from the public key.
  const unsigned char* asn1_ptr = public_key_der.data();
  crypto::ScopedRSA public_key(
      d2i_RSAPublicKey(NULL, &asn1_ptr, public_key_der.size()));
  if (!public_key.get()) {
    LOG(ERROR) << "Pkcs11KeyStore: Failed to decode public key.";
    return false;
  }
  SecureBlob modulus(RSA_size(public_key.get()));
  const BIGNUM* n;
  RSA_get0_key(public_key.get(), &n, nullptr, nullptr);
  int length = BN_bn2bin(n, modulus.data());
  if (length <= 0) {
    LOG(ERROR) << "Pkcs11KeyStore: Failed to extract public key modulus.";
    return false;
  }
  modulus.resize(length);

  // Construct a PKCS #11 template for the public key object.
  CK_BBOOL true_value = CK_TRUE;
  CK_BBOOL false_value = CK_FALSE;
  CK_KEY_TYPE key_type = CKK_RSA;
  CK_OBJECT_CLASS public_key_class = CKO_PUBLIC_KEY;
  SecureBlob id = CryptoLib::Sha1(modulus);
  SecureBlob mutable_label(label);
  CK_ULONG modulus_bits = modulus.size() * 8;
  unsigned char public_exponent[] = {1, 0, 1};
  CK_ATTRIBUTE public_key_attributes[] = {
      {CKA_CLASS, &public_key_class, sizeof(public_key_class)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_DERIVE, &false_value, sizeof(false_value)},
      {CKA_WRAP, &false_value, sizeof(false_value)},
      {CKA_VERIFY, &true_value, sizeof(true_value)},
      {CKA_VERIFY_RECOVER, &false_value, sizeof(false_value)},
      {CKA_ENCRYPT, &false_value, sizeof(false_value)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_ID, id.data(), id.size()},
      {CKA_LABEL, mutable_label.data(), mutable_label.size()},
      {CKA_MODULUS_BITS, &modulus_bits, sizeof(modulus_bits)},
      {CKA_PUBLIC_EXPONENT, public_exponent, base::size(public_exponent)},
      {CKA_MODULUS, modulus.data(), modulus.size()}};

  CK_OBJECT_HANDLE object_handle = CK_INVALID_HANDLE;
  if (C_CreateObject(session.handle(), public_key_attributes,
                     base::size(public_key_attributes),
                     &object_handle) != CKR_OK) {
    LOG(ERROR) << "Pkcs11KeyStore: Failed to create public key object.";
    return false;
  }

  // Construct a PKCS #11 template for the private key object.
  CK_OBJECT_CLASS private_key_class = CKO_PRIVATE_KEY;
  CK_ATTRIBUTE private_key_attributes[] = {
      {CKA_CLASS, &private_key_class, sizeof(private_key_class)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_SENSITIVE, &true_value, sizeof(true_value)},
      {CKA_EXTRACTABLE, &false_value, sizeof(false_value)},
      {CKA_DERIVE, &false_value, sizeof(false_value)},
      {CKA_UNWRAP, &false_value, sizeof(false_value)},
      {CKA_SIGN, &true_value, sizeof(true_value)},
      {CKA_SIGN_RECOVER, &false_value, sizeof(false_value)},
      {CKA_DECRYPT, &false_value, sizeof(false_value)},
      {CKA_KEY_TYPE, &key_type, sizeof(key_type)},
      {CKA_ID, id.data(), id.size()},
      {CKA_LABEL, mutable_label.data(), mutable_label.size()},
      {CKA_PUBLIC_EXPONENT, public_exponent, base::size(public_exponent)},
      {CKA_MODULUS, modulus.data(), modulus.size()},
      {kKeyBlobAttribute, const_cast<uint8_t*>(private_key_blob.data()),
       private_key_blob.size()}};

  if (C_CreateObject(session.handle(), private_key_attributes,
                     base::size(private_key_attributes),
                     &object_handle) != CKR_OK) {
    LOG(ERROR) << "Pkcs11KeyStore: Failed to create private key object.";
    return false;
  }

  if (!certificate.empty()) {
    brillo::SecureBlob subject;
    if (!GetCertificateSubject(certificate, &subject)) {
      LOG(WARNING) << "Pkcs11KeyStore: Failed to find certificate subject.";
    }
    // Construct a PKCS #11 template for a certificate object.
    SecureBlob mutable_certificate = certificate;
    CK_OBJECT_CLASS certificate_class = CKO_CERTIFICATE;
    CK_CERTIFICATE_TYPE certificate_type = CKC_X_509;
    CK_ATTRIBUTE certificate_attributes[] = {
        {CKA_CLASS, &certificate_class, sizeof(certificate_class)},
        {CKA_TOKEN, &true_value, sizeof(true_value)},
        {CKA_PRIVATE, &false_value, sizeof(false_value)},
        {CKA_ID, id.data(), id.size()},
        {CKA_LABEL, mutable_label.data(), mutable_label.size()},
        {CKA_CERTIFICATE_TYPE, &certificate_type, sizeof(certificate_type)},
        {CKA_SUBJECT, subject.data(), subject.size()},
        {CKA_VALUE, mutable_certificate.data(), mutable_certificate.size()}};

    if (C_CreateObject(session.handle(), certificate_attributes,
                       base::size(certificate_attributes),
                       &object_handle) != CKR_OK) {
      LOG(ERROR) << "Pkcs11KeyStore: Failed to create certificate object.";
      return false;
    }
  }

  return true;
}

bool Pkcs11KeyStore::RegisterCertificate(
    bool is_user_specific,
    const std::string& username,
    const brillo::SecureBlob& certificate) {
  CK_SLOT_ID slot;
  if (!GetUserSlot(is_user_specific, username, &slot))
    return false;
  ScopedSession session(slot);
  if (!session.IsValid())
    return false;

  if (DoesCertificateExist(session.handle(), certificate)) {
    LOG(INFO) << "Pkcs11KeyStore: Certificate already exists.";
    return true;
  }
  brillo::SecureBlob subject;
  if (!GetCertificateSubject(certificate, &subject)) {
    LOG(WARNING) << "Pkcs11KeyStore: Failed to find certificate subject.";
  }
  // Construct a PKCS #11 template for a certificate object.
  SecureBlob mutable_certificate = certificate;
  CK_OBJECT_CLASS certificate_class = CKO_CERTIFICATE;
  CK_CERTIFICATE_TYPE certificate_type = CKC_X_509;
  CK_BBOOL true_value = CK_TRUE;
  CK_BBOOL false_value = CK_FALSE;
  CK_ATTRIBUTE certificate_attributes[] = {
      {CKA_CLASS, &certificate_class, sizeof(certificate_class)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &false_value, sizeof(false_value)},
      {CKA_CERTIFICATE_TYPE, &certificate_type, sizeof(certificate_type)},
      {CKA_SUBJECT, subject.data(), subject.size()},
      {CKA_VALUE, mutable_certificate.data(), mutable_certificate.size()}};
  CK_OBJECT_HANDLE object_handle = CK_INVALID_HANDLE;
  if (C_CreateObject(session.handle(), certificate_attributes,
                     base::size(certificate_attributes),
                     &object_handle) != CKR_OK) {
    LOG(ERROR) << "Pkcs11KeyStore: Failed to create certificate object.";
    return false;
  }
  return true;
}

CK_OBJECT_HANDLE Pkcs11KeyStore::FindObject(CK_SESSION_HANDLE session_handle,
                                            const std::string& key_name) {
  // Assemble a search template.
  CK_OBJECT_CLASS object_class = CKO_DATA;
  CK_BBOOL true_value = CK_TRUE;
  CK_BBOOL false_value = CK_FALSE;
  CK_ATTRIBUTE attributes[] = {
      {CKA_CLASS, &object_class, sizeof(object_class)},
      {CKA_LABEL, base::data(const_cast<std::string&>(key_name)),
       key_name.size()},
      {CKA_APPLICATION, const_cast<char*>(kApplicationID),
       base::size(kApplicationID)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_MODIFIABLE, &false_value, sizeof(false_value)}};
  CK_OBJECT_HANDLE key_handle = CK_INVALID_HANDLE;
  CK_ULONG count = 0;
  if ((C_FindObjectsInit(session_handle, attributes, base::size(attributes)) !=
       CKR_OK) ||
      (C_FindObjects(session_handle, &key_handle, 1, &count) != CKR_OK) ||
      (C_FindObjectsFinal(session_handle) != CKR_OK)) {
    LOG(ERROR) << "Key search failed: " << key_name;
    return CK_INVALID_HANDLE;
  }
  if (count == 1)
    return key_handle;
  return CK_INVALID_HANDLE;
}

bool Pkcs11KeyStore::GetUserSlot(bool is_user_specific,
                                 const std::string& username,
                                 CK_SLOT_ID_PTR slot) {
  const char kChapsDaemonName[] = "chaps";
  const char kChapsSystemToken[] = "/var/lib/chaps";
  FilePath token_path = is_user_specific
                            ? brillo::cryptohome::home::GetDaemonStorePath(
                                  username, kChapsDaemonName)
                            : FilePath(kChapsSystemToken);
  return pkcs11_init_->GetTpmTokenSlotForPath(token_path, slot);
}

bool Pkcs11KeyStore::EnumObjects(
    CK_SESSION_HANDLE session_handle,
    const Pkcs11KeyStore::EnumObjectsCallback& callback) {
  // Assemble a search template.
  CK_OBJECT_CLASS object_class = CKO_DATA;
  CK_BBOOL true_value = CK_TRUE;
  CK_BBOOL false_value = CK_FALSE;
  CK_ATTRIBUTE attributes[] = {
      {CKA_CLASS, &object_class, sizeof(object_class)},
      {CKA_APPLICATION, const_cast<char*>(kApplicationID),
       base::size(kApplicationID)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &true_value, sizeof(true_value)},
      {CKA_MODIFIABLE, &false_value, sizeof(false_value)}};
  const CK_ULONG kMaxHandles = 100;  // Arbitrary.
  CK_OBJECT_HANDLE handles[kMaxHandles];
  CK_ULONG count = 0;
  if ((C_FindObjectsInit(session_handle, attributes, base::size(attributes)) !=
       CKR_OK) ||
      (C_FindObjects(session_handle, handles, kMaxHandles, &count) != CKR_OK)) {
    LOG(ERROR) << "Key search failed.";
    return false;
  }
  while (count > 0) {
    for (CK_ULONG i = 0; i < count; ++i) {
      std::string key_name;
      if (!GetKeyName(session_handle, handles[i], &key_name)) {
        LOG(WARNING) << "Found key object but failed to get name.";
        continue;
      }
      if (!callback.Run(key_name, handles[i]))
        return false;
    }
    if (C_FindObjects(session_handle, handles, kMaxHandles, &count) != CKR_OK) {
      LOG(ERROR) << "Key search continuation failed.";
      return false;
    }
  }
  if (C_FindObjectsFinal(session_handle) != CKR_OK) {
    LOG(WARNING) << "Failed to finalize key search.";
  }
  return true;
}

bool Pkcs11KeyStore::GetKeyName(CK_SESSION_HANDLE session_handle,
                                CK_OBJECT_HANDLE object_handle,
                                std::string* key_name) {
  CK_ATTRIBUTE attribute = {CKA_LABEL, NULL, 0};
  if (C_GetAttributeValue(session_handle, object_handle, &attribute, 1) !=
      CKR_OK) {
    LOG(ERROR) << "C_GetAttributeValue(CKA_LABEL) [length] failed.";
    return false;
  }
  key_name->resize(attribute.ulValueLen);
  attribute.pValue = base::data(*key_name);
  if (C_GetAttributeValue(session_handle, object_handle, &attribute, 1) !=
      CKR_OK) {
    LOG(ERROR) << "C_GetAttributeValue(CKA_LABEL) failed.";
    return false;
  }
  return true;
}

bool Pkcs11KeyStore::DeleteIfMatchesPrefix(CK_SESSION_HANDLE session_handle,
                                           const std::string& key_prefix,
                                           const std::string& key_name,
                                           CK_OBJECT_HANDLE object_handle) {
  if (key_name.find(key_prefix) == 0) {
    if (C_DestroyObject(session_handle, object_handle) != CKR_OK) {
      LOG(ERROR) << "C_DestroyObject failed.";
      return false;
    }
  }
  return true;
}

bool Pkcs11KeyStore::GetCertificateSubject(
    const brillo::SecureBlob& certificate, brillo::SecureBlob* subject) {
  const unsigned char* asn1_ptr = certificate.data();
  ScopedX509 x509(d2i_X509(NULL, &asn1_ptr, certificate.size()));
  if (!x509.get()) {
    LOG(WARNING) << "Pkcs11KeyStore: Failed to decode certificate.";
    return false;
  }
  unsigned char* buffer = NULL;
  int length = i2d_X509_NAME(X509_get_subject_name(x509.get()), &buffer);
  crypto::ScopedOpenSSLBytes scoped_buffer(buffer);
  if (length <= 0) {
    LOG(WARNING) << "Pkcs11KeyStore: Failed to encode certificate subject.";
    return false;
  }
  SecureBlob tmp(buffer, buffer + length);
  subject->swap(tmp);
  return true;
}

bool Pkcs11KeyStore::DoesCertificateExist(
    CK_SESSION_HANDLE session_handle, const brillo::SecureBlob& certificate) {
  CK_OBJECT_CLASS object_class = CKO_CERTIFICATE;
  CK_BBOOL true_value = CK_TRUE;
  CK_BBOOL false_value = CK_FALSE;
  SecureBlob mutable_certificate = certificate;
  CK_ATTRIBUTE attributes[] = {
      {CKA_CLASS, &object_class, sizeof(object_class)},
      {CKA_TOKEN, &true_value, sizeof(true_value)},
      {CKA_PRIVATE, &false_value, sizeof(false_value)},
      {CKA_VALUE, mutable_certificate.data(), mutable_certificate.size()}};
  CK_OBJECT_HANDLE object_handle = CK_INVALID_HANDLE;
  CK_ULONG count = 0;
  if ((C_FindObjectsInit(session_handle, attributes, base::size(attributes)) !=
       CKR_OK) ||
      (C_FindObjects(session_handle, &object_handle, 1, &count) != CKR_OK) ||
      (C_FindObjectsFinal(session_handle) != CKR_OK)) {
    return false;
  }
  return (count > 0);
}

}  // namespace cryptohome
