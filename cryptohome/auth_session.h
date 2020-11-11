// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_SESSION_H_
#define CRYPTOHOME_AUTH_SESSION_H_

#include <string>

#include <base/unguessable_token.h>
#include <brillo/secure_blob.h>

namespace cryptohome {

// This class starts a session for the user to authenticate with their
// credentials.
class AuthSession final {
 public:
  explicit AuthSession(std::string username);
  ~AuthSession();

  // Returns the full unhashed user name.
  std::string username() const { return username_; }

  // Returns the token which is used to identify the current AuthSession.
  const base::UnguessableToken& token() { return token_; }

  // Static function which returns a serialized token in a vector format. The
  // token is serialized into two uint64_t values which are stored in string of
  // size 16 bytes. The first 8 bytes represent the high value of the serialized
  // token, the next 8 represent the low value of the serialized token.
  static base::Optional<std::string> GetSerializedStringFromToken(
      const base::UnguessableToken& token);

  // Static function which returns UnguessableToken object after deconstructing
  // the string formed in GetSerializedStringFromToken.
  static base::Optional<base::UnguessableToken> GetTokenFromSerializedString(
      const std::string& serialized_token);

 private:
  AuthSession() = delete;
  std::string username_;
  base::UnguessableToken token_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_H_
