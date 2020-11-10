// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A mock-able key storage interface.

#ifndef CRYPTOHOME_KEYSTORE_H_
#define CRYPTOHOME_KEYSTORE_H_

#include <string>

#include <base/macros.h>
#include <brillo/secure_blob.h>

namespace cryptohome {

class KeyStore {
 public:
  KeyStore() {}
  KeyStore(const KeyStore&) = delete;
  KeyStore& operator=(const KeyStore&) = delete;

  virtual ~KeyStore() {}

  // Reads key data from the store for the key identified by |key_name| and by
  // |username| if |is_user_specific|. On success true is returned and
  // |key_data| is populated.
  virtual bool Read(bool is_user_specific,
                    const std::string& username,
                    const std::string& key_name,
                    brillo::SecureBlob* key_data) = 0;

  // Writes key data to the store for the key identified by |key_name| and by
  // |username| if |is_user_specific|. If such a key already exists the existing
  // data will be overwritten.
  virtual bool Write(bool is_user_specific,
                     const std::string& username,
                     const std::string& key_name,
                     const brillo::SecureBlob& key_data) = 0;

  // Deletes key data for the key identified by |key_name| and by |username| if
  // |is_user_specific|. Returns false if key data exists but could not be
  // deleted.
  virtual bool Delete(bool is_user_specific,
                      const std::string& username,
                      const std::string& key_name) = 0;

  // Deletes key data for all keys identified by |key_prefix| and by |username|
  // if |is_user_specific|. Returns false if key data exists but could not be
  // deleted.
  virtual bool DeleteByPrefix(bool is_user_specific,
                              const std::string& username,
                              const std::string& key_prefix) = 0;

  // Registers a key to be associated with |username| if |is_user_specific|.
  // The provided |label| will be associated with all registered objects.
  // |private_key_blob| holds the private key in some opaque format and
  // |public_key_der| holds the public key in PKCS #1 RSAPublicKey format.
  // If a non-empty |certificate| is provided it will be registered along with
  // the key. Returns true on success.
  virtual bool Register(bool is_user_specific,
                        const std::string& username,
                        const std::string& label,
                        const brillo::SecureBlob& private_key_blob,
                        const brillo::SecureBlob& public_key_der,
                        const brillo::SecureBlob& certificate) = 0;

  // Registers a |certificate| that is not associated to a registered key. The
  // certificate will be associated with |username| if |is_user_specific|.
  virtual bool RegisterCertificate(bool is_user_specific,
                                   const std::string& username,
                                   const brillo::SecureBlob& certificate) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEYSTORE_H_
