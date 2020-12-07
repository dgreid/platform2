// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/filesystem_layout.h"

#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/platform.h"

namespace cryptohome {

namespace {
constexpr char kShadowRoot[] = "/home/.shadow";
constexpr char kSystemSaltFile[] = "salt";
constexpr char kSkelPath[] = "/etc/skel";
}  // namespace

base::FilePath ShadowRoot() {
  return base::FilePath(kShadowRoot);
}

base::FilePath SaltFile() {
  return ShadowRoot().Append(kSystemSaltFile);
}

base::FilePath SkelDir() {
  return base::FilePath(kSkelPath);
}

base::FilePath VaultKeysetPath(const std::string& obfuscated, int index) {
  return ShadowRoot()
      .Append(obfuscated)
      .Append(kKeyFile)
      .AddExtension(base::NumberToString(index));
}

base::FilePath UserActivityTimestampPath(const std::string& obfuscated,
                                         int index) {
  return VaultKeysetPath(obfuscated, index).AddExtension(kTsFile);
}

base::FilePath GetEcryptfsUserVaultPath(const std::string& obfuscated) {
  return ShadowRoot().Append(obfuscated).Append(kEcryptfsVaultDir);
}

base::FilePath GetUserMountDirectory(const std::string& obfuscated_username) {
  return ShadowRoot().Append(obfuscated_username).Append(kMountDir);
}

bool InitializeFilesystemLayout(Platform* platform,
                                Crypto* crypto,
                                brillo::SecureBlob* salt) {
  const base::FilePath shadow_root = ShadowRoot();
  if (!platform->DirectoryExists(shadow_root)) {
    platform->CreateDirectory(shadow_root);
    platform->RestoreSELinuxContexts(shadow_root, true);
  }
  const base::FilePath salt_file = SaltFile();
  if (!crypto->GetOrCreateSalt(salt_file, CRYPTOHOME_DEFAULT_SALT_LENGTH, false,
                               salt)) {
    LOG(ERROR) << "Failed to create system salt.";
    return false;
  }
  return true;
}

}  // namespace cryptohome
