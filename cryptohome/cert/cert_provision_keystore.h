// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// KeyStore interface classes for cert_provision library.

#ifndef CRYPTOHOME_CERT_CERT_PROVISION_KEYSTORE_H_
#define CRYPTOHOME_CERT_CERT_PROVISION_KEYSTORE_H_

#include <memory>
#include <string>
#include <vector>

#include <chaps/pkcs11/cryptoki.h>
#include <google/protobuf/message_lite.h>

#include "cryptohome/cert/cert_provision_util.h"
#include "cryptohome/cert_provision.h"

namespace cert_provision {

// Provides an interface for working with the keystore through PKCS#11 API.
class KeyStore {
 public:
  KeyStore() = default;
  virtual ~KeyStore() = default;

  // Initializes the interface and opens the session used by all further calls.
  // Returns operation result.
  virtual OpResult Init() = 0;

  // Closes the session and tears down the connection to the PKCS#11 API. Safe
  // to call even after unsuccessful Init().
  virtual void TearDown() = 0;

  // Signs |data| using the signing |mechanism| and the private key with |id|
  // and |label|. Fills |signature| with the result.
  // Returns operation result.
  virtual OpResult Sign(const std::string& id,
                        const std::string& label,
                        SignMechanism mechanism,
                        const std::string& data,
                        std::string* signature) = 0;

  // Reads provision status for |label| from the keystore. Stores the result in
  // |provision_status|.
  // Returns operation result.
  virtual OpResult ReadProvisionStatus(
      const std::string& label,
      google::protobuf::MessageLite* provision_status) = 0;

  // Writes |provision_status| for |label| into the keystore.
  // Returns operation result.
  virtual OpResult WriteProvisionStatus(
      const std::string& label,
      const google::protobuf::MessageLite& provision_status) = 0;

  // Deletes all objects with |id| and |label|.
  // Returns operation result.
  virtual OpResult DeleteKeys(const std::string& id,
                              const std::string& label) = 0;

  // Returns a new scoped default KeyStore implementation unless
  // subst_obj is set. In that case, returns subst_obj.
  static Scoped<KeyStore> Create();
  static KeyStore* subst_obj;

 private:
  static std::unique_ptr<KeyStore> GetDefault();
};

class KeyStoreImpl : public KeyStore {
 public:
  KeyStoreImpl() = default;
  KeyStoreImpl(const KeyStoreImpl&) = delete;
  KeyStoreImpl& operator=(const KeyStoreImpl&) = delete;

  ~KeyStoreImpl();
  OpResult Init() override;
  void TearDown() override;
  OpResult Sign(const std::string& id,
                const std::string& label,
                SignMechanism mechanism,
                const std::string& data,
                std::string* signature) override;
  OpResult ReadProvisionStatus(
      const std::string& label,
      google::protobuf::MessageLite* provision_status) override;
  OpResult WriteProvisionStatus(
      const std::string& label,
      const google::protobuf::MessageLite& provision_status) override;
  OpResult DeleteKeys(const std::string& id, const std::string& label) override;

 private:
  OpResult KeyStoreResError(const std::string& message, CK_RV res) {
    return {Status::KeyStoreError, message + ": " + std::to_string(res)};
  }

  // Converts the signing mechanism type from the |mechanism| submitted to
  // Sign, to PKCS#11 CK_MECHANISM_TYPE.
  // Returns true for known types, false otherwise.
  bool GetMechanismType(SignMechanism mechanism, CK_MECHANISM_TYPE* type);

  // Finds |objects| in the keystore that match the given set of attributes
  // defined by the pointer to the array of the attributes |attr| of size
  // |attr_count|. May return an empty vector - not considered to be an
  // error.
  // Returns operation result.
  OpResult Find(CK_ATTRIBUTE* attr,
                CK_ULONG attr_count,
                std::vector<CK_OBJECT_HANDLE>* objects);

  bool initialized_ = false;
  CK_SESSION_HANDLE session_ = CK_INVALID_HANDLE;
};

}  // namespace cert_provision

#endif  // CRYPTOHOME_CERT_CERT_PROVISION_KEYSTORE_H_
