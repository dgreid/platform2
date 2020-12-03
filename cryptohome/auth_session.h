// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_SESSION_H_
#define CRYPTOHOME_AUTH_SESSION_H_

#include <string>

#include <base/timer/timer.h>
#include <base/unguessable_token.h>
#include <brillo/secure_blob.h>

namespace cryptohome {

// This enum holds the states an AuthSession could be in during the session.
enum class AuthStatus {
  // kAuthStatusFurtherFactorRequired is a state where the session is waiting
  // for one or more factors so that the session can continue the processes of
  // authenticating a user. This is the state the AuthSession starts in by
  // default.
  kAuthStatusFurtherFactorRequired,
  // kAuthStatusTimedOut tells the user to restart the AuthSession because
  // the session has timed out.
  kAuthStatusTimedOut
  // TODO(crbug.com/1154912): Complete the implementation of AuthStatus.
};

// This class starts a session for the user to authenticate with their
// credentials.
class AuthSession final {
 public:
  AuthSession(
      std::string username,
      base::OnceCallback<void(const base::UnguessableToken&)> on_timeout);
  ~AuthSession();

  // Returns the full unhashed user name.
  std::string username() const { return username_; }

  // Returns the token which is used to identify the current AuthSession.
  const base::UnguessableToken& token() { return token_; }

  // This function return the current status of this AuthSession.
  AuthStatus GetStatus() const { return status_; }

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
  // AuthSessionTimedOut is called when the session times out and cleans up
  // credentials that may be in memory. |on_timeout_| is also called to remove
  // this |AuthSession| reference from |UserDataAuth|.
  void AuthSessionTimedOut();

  std::string username_;
  base::UnguessableToken token_;

  AuthStatus status_ = AuthStatus::kAuthStatusFurtherFactorRequired;
  base::OneShotTimer timer_;
  base::OnceCallback<void(const base::UnguessableToken&)> on_timeout_;

  FRIEND_TEST(AuthSessionTest, TimeoutTest);
  FRIEND_TEST(UserDataAuthExTest, StartAuthSession);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_SESSION_H_
