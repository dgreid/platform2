// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FILESYSTEM_KEY_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FILESYSTEM_KEY_H_

#include <brillo/secure_blob.h>

namespace cryptohome {

typedef struct {
  brillo::SecureBlob fek;
  brillo::SecureBlob fnek;
  brillo::SecureBlob fek_salt;
  brillo::SecureBlob fnek_salt;
} FileSystemKey;

typedef struct {
  brillo::SecureBlob fek_sig;
  brillo::SecureBlob fnek_sig;
} FileSystemKeyReference;

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FILESYSTEM_KEY_H_
