// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FILESYSTEM_LAYOUT_H_
#define CRYPTOHOME_FILESYSTEM_LAYOUT_H_

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/platform.h"

namespace cryptohome {

constexpr char kShadowRoot[] = "/home/.shadow";
constexpr char kSystemSaltFile[] = "salt";

bool InitializeFilesystemLayout(Platform* platform,
                                Crypto* crypto,
                                const base::FilePath& shadow_root,
                                brillo::SecureBlob* salt);

}  // namespace cryptohome

#endif  // CRYPTOHOME_FILESYSTEM_LAYOUT_H_
