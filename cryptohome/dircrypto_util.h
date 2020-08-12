// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DIRCRYPTO_UTIL_H_
#define CRYPTOHOME_DIRCRYPTO_UTIL_H_

#include <base/files/file_path.h>
#include <brillo/secure_blob.h>

extern "C" {
#include <keyutils.h>
}

namespace dircrypto {

// State of the directory's encryption key.
enum class KeyState {
  UNKNOWN,  // Cannot get the state.
  NOT_SUPPORTED,  // The directory doesn't support dircrypto.
  NO_KEY,  // No key is set.
  ENCRYPTED,  // Key is set.
};

// keyutils functions use -1 as the invalid key serial value.
constexpr key_serial_t kInvalidKeySerial = -1;

// Sets the directory key.
bool SetDirectoryKey(const base::FilePath& dir,
                     const brillo::SecureBlob& key_descriptor);

// Returns the directory's key state, or returns UNKNOWN on errors.
KeyState GetDirectoryKeyState(const base::FilePath& dir);

// Adds the key to the dircrypto keyring. Returns -1 on errors.
key_serial_t AddKeyToKeyring(const brillo::SecureBlob& key,
                             const brillo::SecureBlob& key_descriptor);

// Unlinks the key from the dircrypto keyring.
bool UnlinkKey(key_serial_t key);

// Unlinks the key from the dircrypto keyring using a descriptor.
bool UnlinkKeyByDescriptor(const brillo::SecureBlob& key_descriptor);

}  // namespace dircrypto

#endif  // CRYPTOHOME_DIRCRYPTO_UTIL_H_
