// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for LegacyUserSession.

#include "cryptohome/legacy_user_session.h"

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <string>

#include "cryptohome/credentials.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_platform.h"

using brillo::SecureBlob;

namespace cryptohome {

class LegacyUserSessionTest : public ::testing::Test {
 public:
  LegacyUserSessionTest() : salt() {}
  virtual ~LegacyUserSessionTest() {}

  void SetUp() {
    salt.resize(16);
    CryptoLib::GetSecureRandom(salt.data(), salt.size());
  }

 protected:
  SecureBlob salt;

 private:
  DISALLOW_COPY_AND_ASSIGN(LegacyUserSessionTest);
};

TEST_F(LegacyUserSessionTest, InitTest) {
  Credentials credentials("username", SecureBlob("password"));
  LegacyUserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
}

TEST_F(LegacyUserSessionTest, CheckUserTest) {
  Credentials credentials("username", SecureBlob("password"));
  LegacyUserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
  EXPECT_TRUE(session.CheckUser(credentials.GetObfuscatedUsername(salt)));
}

TEST_F(LegacyUserSessionTest, ReInitTest) {
  Credentials credentials("username", SecureBlob("password"));
  Credentials credentials_new("username2", SecureBlob("password2"));
  LegacyUserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
  EXPECT_TRUE(session.SetUser(credentials_new));
  EXPECT_FALSE(session.CheckUser(credentials.GetObfuscatedUsername(salt)));
  EXPECT_TRUE(session.CheckUser(credentials_new.GetObfuscatedUsername(salt)));
}

TEST_F(LegacyUserSessionTest, ResetTest) {
  Credentials credentials("username", SecureBlob("password"));
  LegacyUserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
  session.Reset();
  EXPECT_FALSE(session.CheckUser(credentials.GetObfuscatedUsername(salt)));
}

TEST_F(LegacyUserSessionTest, VerifyTest) {
  Credentials credentials("username", SecureBlob("password"));
  LegacyUserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
  EXPECT_TRUE(session.Verify(credentials));
}

}  // namespace cryptohome
