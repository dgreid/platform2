// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_PASSWORD_VERIFIER_H_
#define CRYPTOHOME_PASSWORD_VERIFIER_H_

#include <brillo/secure_blob.h>

namespace cryptohome {

class PasswordVerifier {
 public:
  PasswordVerifier() = default;
  virtual ~PasswordVerifier() = default;

  // Prohibit copy/move/assignment.
  PasswordVerifier(const PasswordVerifier&) = delete;
  PasswordVerifier(const PasswordVerifier&&) = delete;
  PasswordVerifier& operator=(const PasswordVerifier&) = delete;
  PasswordVerifier& operator=(const PasswordVerifier&&) = delete;

  // Sets internal state for |secret| Verify().
  virtual bool Set(const brillo::SecureBlob& secret) = 0;

  // Verifies the |secret| against previously Set() state.
  virtual bool Verify(const brillo::SecureBlob& secret) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PASSWORD_VERIFIER_H_
