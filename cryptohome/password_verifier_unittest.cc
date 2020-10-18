// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cryptohome/password_verifier.h>

#include <memory>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include <cryptohome/aes_deprecated_password_verifier.h>

namespace cryptohome {

class VerifierTest : public ::testing::TestWithParam<PasswordVerifier*> {
 public:
  VerifierTest() { password_verifier_.reset(GetParam()); }

 protected:
  std::unique_ptr<PasswordVerifier> password_verifier_;
};

INSTANTIATE_TEST_SUITE_P(
    AesDeprecated,
    VerifierTest,
    ::testing::Values(new AesDeprecatedPasswordVerifier()));

TEST_P(VerifierTest, Ok) {
  brillo::SecureBlob secret("good");
  EXPECT_TRUE(password_verifier_->Set(secret));
  EXPECT_TRUE(password_verifier_->Verify(secret));
}

TEST_P(VerifierTest, Fail) {
  brillo::SecureBlob secret("good");
  brillo::SecureBlob wrong_secret("wrong");
  EXPECT_TRUE(password_verifier_->Set(secret));
  EXPECT_FALSE(password_verifier_->Verify(wrong_secret));
}

TEST_P(VerifierTest, NotSet) {
  brillo::SecureBlob secret("not set secret");
  EXPECT_FALSE(password_verifier_->Verify(secret));
}

}  // namespace cryptohome
