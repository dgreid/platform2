// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_H_
#define CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_H_

#include <base/macros.h>
#include <brillo/secure_blob.h>

#include "cryptohome/boot_lockbox_key.pb.h"

namespace cryptohome {

class Crypto;
class Platform;
class Tpm;

// This class implements a boot-lockbox using a TPM key which is restricted to a
// 0 value of TPM PCR 1.  Every boot this key can be used to sign data and until
// a user session starts at which time the PCR is extended and the key can no
// longer be used.  In this way the signed data is tamper-evident to all
// modifications except during the window right after boot.
//
// A normal usage flow for BootLockbox would be something like:
//
// BootLockbox lockbox(tpm);
// success = lockbox.Sign(data, &signature);
// ...
// lockbox.FinalizeBoot();
// ...
// success = lockbox.Verify(data, signature);
class BootLockbox {
 public:
  // Does not take ownership of pointers.
  BootLockbox(Tpm* tpm, Platform* platform, Crypto* crypto);
  BootLockbox(const BootLockbox&) = delete;
  BootLockbox& operator=(const BootLockbox&) = delete;

  virtual ~BootLockbox();

  // Signs |data| for boot-time tamper evidence.  This always fails after
  // FinalizeBoot() has been called.  On success returns true and sets the
  // |signature| value.  The signature scheme will be RSA-PKCS1-SHA256.
  virtual bool Sign(const brillo::Blob& data, brillo::SecureBlob* signature);

  // Verifies that |signature| is valid for |data| and that it was generated
  // before FinalizeBoot() on a current or prior boot.
  virtual bool Verify(const brillo::Blob& data,
                      const brillo::SecureBlob& signature);

  // Locks the key used by Sign() so it cannot be used again until the next
  // boot.  Returns true on success.
  virtual bool FinalizeBoot();

  // Checks if BootLockbox has been finalized.
  virtual bool IsFinalized();

  // This function tries to preload the lockbox key if available.
  virtual bool PreLoadKey();

 protected:
  // Returns the TPM |key_blob| for the lockbox key.  Returns true on success.
  bool GetKeyBlob(brillo::SecureBlob* key_blob);

  // Returns the DER-encoded lockbox |public_key|.  Returns true on success.
  bool GetPublicKey(brillo::Blob* public_key);

  // Returns the |creation_blob| for the lockbox key.  Returns true on success.
  bool GetCreationBlob(brillo::SecureBlob* creation_blob);

  // Loads the lockbox key protobuf from a file.  Returns true on success.
  bool LoadKey();

  // Saves the lockbox key protobuf to a file.  Returns true on success.
  bool SaveKey();

  // Creates a new lockbox key.  On success returns true and populates |key_|.
  bool CreateKey();

  // Verifies an RSA-PKCS1-SHA256 signature as created by Sign().
  bool VerifySignature(const brillo::Blob& public_key,
                       const brillo::Blob& signed_data,
                       const brillo::SecureBlob& signature);

  Tpm* tpm_;
  Platform* platform_;
  Crypto* crypto_;
  BootLockboxKey key_;
  const brillo::Blob initial_pcr_value_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_H_
