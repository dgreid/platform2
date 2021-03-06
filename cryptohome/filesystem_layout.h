// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FILESYSTEM_LAYOUT_H_
#define CRYPTOHOME_FILESYSTEM_LAYOUT_H_

#include <string>

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/platform.h"

namespace cryptohome {

// Name of the vault directory which is used with eCryptfs cryptohome.
constexpr char kEcryptfsVaultDir[] = "vault";
// Name of the mount directory.
constexpr char kMountDir[] = "mount";

constexpr mode_t kKeyFilePermissions = 0600;
constexpr int kKeyFileMax = 100;  // master.0 ... master.99
constexpr char kKeyFile[] = "master";
constexpr char kKeyLegacyPrefix[] = "legacy-";

constexpr int kInitialKeysetIndex = 0;
constexpr char kTsFile[] = "timestamp";

base::FilePath ShadowRoot();
base::FilePath SaltFile();
base::FilePath SkelDir();
base::FilePath VaultKeysetPath(const std::string& obfuscated, int index);
base::FilePath UserActivityTimestampPath(const std::string& obfuscated,
                                         int index);

base::FilePath GetEcryptfsUserVaultPath(const std::string& obfuscated_username);
base::FilePath GetUserMountDirectory(const std::string& obfuscated_username);

bool InitializeFilesystemLayout(Platform* platform,
                                Crypto* crypto,
                                brillo::SecureBlob* salt);

}  // namespace cryptohome

#endif  // CRYPTOHOME_FILESYSTEM_LAYOUT_H_
