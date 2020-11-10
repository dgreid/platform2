// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A token manager provides a set of methods for login agents to create and
// validate token storage.

#ifndef CHAPS_TOKEN_FILE_MANAGER_H_
#define CHAPS_TOKEN_FILE_MANAGER_H_

#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

namespace chaps {

class TokenFileManager {
 public:
  TokenFileManager(uid_t chapsd_uid, gid_t chapsd_gid);
  TokenFileManager(const TokenFileManager&) = delete;
  TokenFileManager& operator=(const TokenFileManager&) = delete;

  virtual ~TokenFileManager();

  // Returns true if the user's token exists at the expected location, and
  // returns this location.
  //  user: Name of the user.
  //  token_path: Set to the expected location of the user's token, regardless
  //              of whether the token exists already.
  virtual bool GetUserTokenPath(const std::string& user,
                                base::FilePath* token_path);

  // Creates a token directory at the given path with the correct
  // permissions, returning true on success.
  //  token_path: The location of the token.
  virtual bool CreateUserTokenDirectory(const base::FilePath& token_path);

  // Checks the permissions of the token directory path, returning true if
  // they are valid.
  //  token_path: The location of the token.
  virtual bool CheckUserTokenPermissions(const base::FilePath& token_path);

  // Salts the authorization data using the token's stored salt value.
  //  token_path: The location of the token.
  //  auth_data: The authorization data to salt.
  //  salted_auth_data: Returns the salted authorization data.
  virtual bool SaltAuthData(const base::FilePath& token_path,
                            const brillo::SecureBlob& auth_data,
                            brillo::SecureBlob* salted_auth_data);

 private:
  uid_t chapsd_uid_;
  gid_t chapsd_gid_;
};

}  // namespace chaps

#endif  // CHAPS_TOKEN_FILE_MANAGER_H_
