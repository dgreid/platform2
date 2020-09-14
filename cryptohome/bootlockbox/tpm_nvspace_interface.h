// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOTLOCKBOX_TPM_NVSPACE_INTERFACE_H_
#define CRYPTOHOME_BOOTLOCKBOX_TPM_NVSPACE_INTERFACE_H_

#include <string>

namespace cryptohome {

enum class NVSpaceState {
  kNVSpaceNormal = 0,
  kNVSpaceError = 1,  // General TPM errors.
  kNVSpaceUninitialized = 2,
  kNVSpaceUndefined = 3,  // TPM is not defined, need powerwash to define.
  kNVSpaceWriteLocked = 4
};

class TPMNVSpaceUtilityInterface {
 public:
  virtual ~TPMNVSpaceUtilityInterface() = default;

  // Override to perform initialization work. This must be called successfully
  // before calling any other methods.
  virtual bool Initialize() = 0;

  // This method defines a non-volatile storage area in TPM for bootlocboxd.
  virtual bool DefineNVSpace() = 0;

  // This method defines a non-volatile space in TPM via trunks.
  virtual bool DefineNVSpaceBeforeOwned() = 0;

  // This method writes |digest| to nvram space for bootlockboxd
  virtual bool WriteNVSpace(const std::string& digest) = 0;

  // Read nv space to nvram_data. If nvspace is defined and initialized,
  // digest contains the digest and returns true. Otherwise, returns false and
  // |state| contains the error information.
  virtual bool ReadNVSpace(std::string* digest, NVSpaceState* state) = 0;

  // Lock the bootlockbox nvspace for writing.
  virtual bool LockNVSpace() = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOTLOCKBOX_TPM_NVSPACE_INTERFACE_H_
