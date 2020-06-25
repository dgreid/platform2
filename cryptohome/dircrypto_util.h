// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DIRCRYPTO_UTIL_H_
#define CRYPTOHOME_DIRCRYPTO_UTIL_H_

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

extern "C" {
#include <keyutils.h>
#include <linux/fscrypt.h>
}

namespace dircrypto {

// State of the directory's encryption key.
enum class KeyState {
  UNKNOWN,  // Cannot get the state.
  NOT_SUPPORTED,  // The directory doesn't support dircrypto.
  NO_KEY,  // No key is set.
  ENCRYPTED,  // Key is set.
};

// KeyReference describes an in-use fscrypt key.
struct KeyReference {
  // Policy version: FSCRYPT_POLICY_V2 is only supported on kernels >= 5.4.
  int policy_version = FSCRYPT_POLICY_V1;
  // Key identifier/descriptor.
  brillo::SecureBlob reference;
};

// keyutils functions use -1 as the invalid key serial value.
constexpr key_serial_t kInvalidKeySerial = -1;

// Sets the directory key.
bool SetDirectoryKey(const base::FilePath& dir,
                     const KeyReference& key_reference);

// Returns the directory's key state, or returns UNKNOWN on errors.
KeyState GetDirectoryKeyState(const base::FilePath& dir);

// Adds the key to the dircrypto keyring. Returns -1 on errors.
bool AddKeyToKeyring(const brillo::SecureBlob& key,
                     KeyReference* key_reference);

// Unlinks the key from the dircrypto keyring.
bool UnlinkKey(const KeyReference& key_reference);

// Invalidate session key and clears cache for mounted partition.
bool InvalidateSessionKey(const KeyReference& key_reference,
                          const base::FilePath& mount_root);

}  // namespace dircrypto

#endif  // CRYPTOHOME_DIRCRYPTO_UTIL_H_
