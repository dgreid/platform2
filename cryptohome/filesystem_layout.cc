// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/filesystem_layout.h"

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/platform.h"

namespace cryptohome {

bool InitializeFilesystemLayout(Platform* platform,
                                Crypto* crypto,
                                const base::FilePath& shadow_root,
                                brillo::SecureBlob* salt) {
  if (!platform->DirectoryExists(shadow_root)) {
    platform->CreateDirectory(shadow_root);
    platform->RestoreSELinuxContexts(shadow_root, true);
  }
  base::FilePath salt_file = shadow_root.Append(cryptohome::kSystemSaltFile);
  if (!crypto->GetOrCreateSalt(salt_file, CRYPTOHOME_DEFAULT_SALT_LENGTH, false,
                               salt)) {
    LOG(ERROR) << "Failed to create system salt.";
    return false;
  }
  return true;
}

}  // namespace cryptohome
