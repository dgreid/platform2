// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for UserSession.

#include "cryptohome/user_session.h"

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <string>

#include "cryptohome/credentials.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_platform.h"

using brillo::SecureBlob;

namespace cryptohome {

class UserSessionTest : public ::testing::Test {
 public:
  UserSessionTest() : salt() {}
  virtual ~UserSessionTest() {}

  void SetUp() {
    salt.resize(16);
    CryptoLib::GetSecureRandom(salt.data(), salt.size());
  }

 protected:
  SecureBlob salt;

 private:
  DISALLOW_COPY_AND_ASSIGN(UserSessionTest);
};

TEST_F(UserSessionTest, InitTest) {
  Credentials credentials("username", SecureBlob("password"));
  UserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
}

TEST_F(UserSessionTest, CheckUserTest) {
  Credentials credentials("username", SecureBlob("password"));
  UserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
  EXPECT_TRUE(session.CheckUser(credentials.GetObfuscatedUsername(salt)));
}

TEST_F(UserSessionTest, ReInitTest) {
  Credentials credentials("username", SecureBlob("password"));
  Credentials credentials_new("username2", SecureBlob("password2"));
  UserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
  EXPECT_TRUE(session.SetUser(credentials_new));
  EXPECT_FALSE(session.CheckUser(credentials.GetObfuscatedUsername(salt)));
  EXPECT_TRUE(session.CheckUser(credentials_new.GetObfuscatedUsername(salt)));
}

TEST_F(UserSessionTest, ResetTest) {
  Credentials credentials("username", SecureBlob("password"));
  UserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
  session.Reset();
  EXPECT_FALSE(session.CheckUser(credentials.GetObfuscatedUsername(salt)));
}

TEST_F(UserSessionTest, VerifyTest) {
  Credentials credentials("username", SecureBlob("password"));
  UserSession session;
  session.Init(salt);
  EXPECT_TRUE(session.SetUser(credentials));
  EXPECT_TRUE(session.Verify(credentials));
}

}  // namespace cryptohome
