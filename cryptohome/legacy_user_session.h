// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// LegacyUserSession - the LegacyUserSession class is used in re-authenticating
// the currently logged-in user.  It allows offline credentials verification
// post-login without the expense of a TPM crypto operation (when the TPM is
// used for added security).  User session works by generating a random blob and
// encrypting it using the user's credentials at login.  When an offline
// credentials check occurs for this user, LegacyUserSession attempts to decrypt
// the encrypted representation of that blob.  A successful decryption means
// that the supplied credentials are correct.

#ifndef CRYPTOHOME_LEGACY_USER_SESSION_H_
#define CRYPTOHOME_LEGACY_USER_SESSION_H_

#include <string>

#include <base/macros.h>
#include <brillo/secure_blob.h>

#include "cryptohome/credentials.h"

namespace cryptohome {

class LegacyUserSession {
 public:
  LegacyUserSession();
  virtual ~LegacyUserSession();

  // Initializes the LegacyUserSession object.
  //
  // Parameters
  //   crypto - The crypto context to use for this session
  //   salt - The salt to use for the username
  virtual void Init(const brillo::SecureBlob& salt);

  // Assigns a user to the LegacyUserSession object.  The random blob is created
  // and encrypted with the supplied credentials.
  //
  // Parameters
  //   username - The user credentials to initialize this session with
  virtual bool SetUser(const Credentials& username);

  // Resets the LegacyUserSession, clearing the current user and cipher text
  // used for verification.
  virtual void Reset();

  // Checks that the user supplied is the user associated with this session
  //
  // Parameters
  //   obfuscated_username - The user to check this session against
  virtual bool CheckUser(const std::string& obfuscated_username) const;

  // Checks that the user's credentials successfully decrypt the ciphertext
  // associated with this session (and are therefore valid for this user).
  //
  // Parameters
  //   credentials - The user credentials to attempt decryption with
  virtual bool Verify(const Credentials& credentials) const;

  // Get the obfuscated username of this session. Returns an empty string if
  // no user is currently assigned to the session.
  //
  // Parameters
  //   username (OUT) - the username
  virtual void GetObfuscatedUsername(std::string* username) const;

  std::string username() const { return username_; }

  // Assigns a key to the LegacyUserSession object.  This indicates which key on
  // disk is associated to the LegacyUserSession.
  //
  // Parameters
  //   index - index of the vault keyset
  virtual void set_key_index(int index) { key_index_ = index; }

  // Get the current key index of this session
  // Returns -1 as indication key data is not initialized and log a warning.
  // The caller should handle this case gracefully.
  int key_index() const;

  // Allow updating outside of construction
  void set_key_data(const KeyData& data) { key_data_ = data; }

  // Get the current key data of this session.
  const KeyData& key_data() const { return key_data_; }

 private:
  std::string obfuscated_username_;
  std::string username_;
  brillo::SecureBlob username_salt_;
  brillo::SecureBlob key_salt_;
  brillo::SecureBlob cipher_;
  int key_index_ = -1;
  KeyData key_data_;

  DISALLOW_COPY_AND_ASSIGN(LegacyUserSession);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_LEGACY_USER_SESSION_H_
