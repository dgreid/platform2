// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A token manager provides a set of methods for login agents to create and
// validate token files.

#include "chaps/token_file_manager.h"

#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/secure_blob.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "chaps/chaps_utility.h"

using base::FilePath;
using brillo::SecureBlob;
using std::string;

namespace chaps {

namespace {

const char kTokenFilePath[] = "/var/lib/chaps/tokens/";

const mode_t kTokenDirectoryPermissions = (S_IRUSR | S_IWUSR | S_IXUSR);
const mode_t kFilePermissionsMask = (S_IRWXU | S_IRWXG | S_IRWXO);

const char kSaltFileName[] = "salt";
const uint32_t kSaltIterations = 4096;
const size_t kSaltBytes = 32;
const size_t kSaltedKeyBytes = 32;

}  // namespace

TokenFileManager::TokenFileManager(uid_t chapsd_uid, gid_t chapsd_gid)
    : chapsd_uid_(chapsd_uid), chapsd_gid_(chapsd_gid) {}

TokenFileManager::~TokenFileManager() {}

bool TokenFileManager::GetUserTokenPath(const string& user,
                                        FilePath* token_path) {
  CHECK(token_path);
  *token_path = FilePath(kTokenFilePath).Append(user);
  return base::DirectoryExists(*token_path);
}

bool TokenFileManager::CreateUserTokenDirectory(const FilePath& token_path) {
  if (base::DirectoryExists(token_path)) {
    LOG(ERROR) << "Tried to create " << token_path.value()
               << " which already exists";
    return false;
  }
  if (!base::CreateDirectory(token_path)) {
    LOG(ERROR) << "Failed to create " << token_path.value();
    return false;
  }

  // Change permissions to be readable and writable only by user chaps.
  if (chmod(token_path.value().c_str(), kTokenDirectoryPermissions)) {
    LOG(ERROR) << "Failed to change permissions of token directory "
               << token_path.value();
    return false;
  }
  if (chown(token_path.value().c_str(), chapsd_uid_, chapsd_gid_)) {
    LOG(ERROR) << "Failed to change ownership of token directory "
               << token_path.value();
    return false;
  }

  // Create random salt file.
  string salt_string;
  salt_string.resize(kSaltBytes);
  if (1 !=
      RAND_bytes(ConvertStringToByteBuffer(salt_string.data()), kSaltBytes)) {
    LOG(ERROR) << "Failed to generate random salt for token directory "
               << token_path.value();
    return false;
  }
  SecureBlob salt(salt_string);
  brillo::SecureClear(&salt_string);

  FilePath salt_file = token_path.Append(kSaltFileName);
  int bytes_written = base::WriteFile(
      salt_file, reinterpret_cast<const char*>(salt.data()), kSaltBytes);
  if (bytes_written != static_cast<int>(kSaltBytes)) {
    LOG(ERROR) << "Failed to write salt file in token directory "
               << token_path.value();
    return false;
  }

  return true;
}

bool TokenFileManager::CheckUserTokenPermissions(const FilePath& token_path) {
  struct stat token_stat;
  if (stat(token_path.value().c_str(), &token_stat)) {
    LOG(ERROR) << "Failed to stat token directory " << token_path.value();
    return false;
  }
  if ((token_stat.st_mode & kFilePermissionsMask) !=
      kTokenDirectoryPermissions) {
    LOG(ERROR) << "Incorrect permissions on token directory "
               << token_path.value();
    return false;
  }
  if (token_stat.st_uid != chapsd_uid_) {
    LOG(ERROR) << "Incorrect owner for token directory " << token_path.value();
    return false;
  }
  if (token_stat.st_gid != chapsd_gid_) {
    LOG(ERROR) << "Incorrect group for token directory " << token_path.value();
    return false;
  }
  return true;
}

bool TokenFileManager::SaltAuthData(const FilePath& token_path,
                                    const SecureBlob& auth_data,
                                    SecureBlob* salted_auth_data) {
  string salt_string;
  FilePath salt_file = token_path.Append(kSaltFileName);
  if (!base::ReadFileToString(salt_file, &salt_string)) {
    LOG(ERROR) << "Failed to read salt file in token directory "
               << token_path.value();
    return false;
  }
  SecureBlob salt(salt_string);
  brillo::SecureClear(&salt_string);
  if (salt.size() != kSaltBytes) {
    LOG(ERROR) << "Salt invalid for token directory " << token_path.value();
    return false;
  }

  SecureBlob out_key(kSaltedKeyBytes);
  if (1 != PKCS5_PBKDF2_HMAC(auth_data.char_data(), auth_data.size(),
                             salt.data(), kSaltBytes, kSaltIterations,
                             EVP_sha512(), kSaltedKeyBytes, out_key.data())) {
    LOG(ERROR) << "Could not salt authorization data.";
    return false;
  }

  salted_auth_data->swap(out_key);
  return true;
}

}  // namespace chaps
