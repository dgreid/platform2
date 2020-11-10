// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A login manager deals with login events related to Chaps.

#ifndef CHAPS_ISOLATE_LOGIN_CLIENT_H_
#define CHAPS_ISOLATE_LOGIN_CLIENT_H_

#include <string>

#include <brillo/secure_blob.h>
#include <base/files/file_path.h>

#include "chaps/isolate.h"
#include "chaps/token_file_manager.h"
#include "chaps/token_manager_client.h"

namespace chaps {

// Manage the loading / unloading of a user's token into per-user isolates
// in Chaps when users login, logout or change their password. Sample usage:
//   IsolateLoginClient isolate_login_client(...);
//   isolate_login_client.LoginUser(...);
//   ...
//   isolate_login_client.LogoutUser(...);
//
// Only virtual to enable mocking in tests.
class IsolateLoginClient {
 public:
  // Does not take ownership of arguments.
  IsolateLoginClient(IsolateCredentialManager* isolate_manager,
                     TokenFileManager* file_manager,
                     TokenManagerClient* token_manager);
  IsolateLoginClient(const IsolateLoginClient&) = delete;
  IsolateLoginClient& operator=(const IsolateLoginClient&) = delete;

  virtual ~IsolateLoginClient();

  // Should be called whenever a user logs into a session. Will ensure that
  // chaps has an open isolate for the user and that their token is loaded into
  // this isolate, thus providing applications running in the users session
  // with access to their TPM protected keys.  Return true on success and
  // false on failure.
  virtual bool LoginUser(const std::string& user,
                         const brillo::SecureBlob& auth_data);

  // Should be called whenever a user logs out of a session. If the user has
  // logged out of all sessions, this will close their isolate and unload
  // their token.  Return true on success and false on failure.
  virtual bool LogoutUser(const std::string& user);

  // Change the authorization data used to secure the users token.
  // Return true on success and false on failure.
  virtual bool ChangeUserAuth(const std::string& user,
                              const brillo::SecureBlob& old_auth_data,
                              const brillo::SecureBlob& new_auth_data);

 private:
  IsolateCredentialManager* isolate_manager_;
  TokenFileManager* file_manager_;
  TokenManagerClient* token_manager_;
};

}  // namespace chaps

#endif  // CHAPS_ISOLATE_LOGIN_CLIENT_H_
