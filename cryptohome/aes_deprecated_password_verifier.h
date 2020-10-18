// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AES_DEPRECATED_PASSWORD_VERIFIER_H_
#define CRYPTOHOME_AES_DEPRECATED_PASSWORD_VERIFIER_H_

#include <brillo/secure_blob.h>

#include <cryptohome/password_verifier.h>

namespace cryptohome {

class AesDeprecatedPasswordVerifier final : public PasswordVerifier {
 public:
  AesDeprecatedPasswordVerifier() = default;
  ~AesDeprecatedPasswordVerifier() override = default;

  // Prohibit copy/move/assignment.
  AesDeprecatedPasswordVerifier(const AesDeprecatedPasswordVerifier&) = delete;
  AesDeprecatedPasswordVerifier(const AesDeprecatedPasswordVerifier&&) = delete;
  AesDeprecatedPasswordVerifier& operator=(
      const AesDeprecatedPasswordVerifier&) = delete;
  AesDeprecatedPasswordVerifier& operator=(
      const AesDeprecatedPasswordVerifier&&) = delete;

  bool Set(const brillo::SecureBlob& secret) override;
  bool Verify(const brillo::SecureBlob& secret) override;

 private:
  brillo::SecureBlob key_salt_;
  brillo::SecureBlob cipher_text_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AES_DEPRECATED_PASSWORD_VERIFIER_H_
