// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for AuthSession.

#include "cryptohome/auth_session.h"

#include <string>
#include <utility>

#include <base/test/task_environment.h>
#include <base/timer/mock_timer.h>
#include <gtest/gtest.h>

namespace cryptohome {

// Fake username to be used in this test suite.
const char fake_username[] = "test_username";

TEST(AuthSessionTest, TimeoutTest) {
  base::test::SingleThreadTaskEnvironment task_environment;
  bool called = false;
  auto on_timeout = base::BindOnce(
      [](bool* called, const base::UnguessableToken&) { *called = true; },
      base::Unretained(&called));
  AuthSession auth_session(fake_username, std::move(on_timeout));
  EXPECT_EQ(auth_session.GetStatus(),
            AuthStatus::kAuthStatusFurtherFactorRequired);
  ASSERT_TRUE(auth_session.timer_.IsRunning());
  auth_session.timer_.FireNow();
  EXPECT_EQ(auth_session.GetStatus(), AuthStatus::kAuthStatusTimedOut);
  EXPECT_TRUE(called);
}

TEST(AuthSessionTest, SerializedStringFromNullToken) {
  base::UnguessableToken token = base::UnguessableToken::Null();
  base::Optional<std::string> serialized_token =
      AuthSession::GetSerializedStringFromToken(token);
  EXPECT_FALSE(serialized_token.has_value());
}

TEST(AuthSessionTest, TokenFromEmptyString) {
  std::string serialized_string = "";
  base::Optional<base::UnguessableToken> unguessable_token =
      AuthSession::GetTokenFromSerializedString(serialized_string);
  EXPECT_FALSE(unguessable_token.has_value());
}

TEST(AuthSessionTest, TokenFromUnexpectedSize) {
  std::string serialized_string = "unexpected_sized_string";
  base::Optional<base::UnguessableToken> unguessable_token =
      AuthSession::GetTokenFromSerializedString(serialized_string);
  EXPECT_FALSE(unguessable_token.has_value());
}

TEST(AuthSessionTest, TokenFromString) {
  base::UnguessableToken original_token = base::UnguessableToken::Create();
  base::Optional<std::string> serialized_token =
      AuthSession::GetSerializedStringFromToken(original_token);
  EXPECT_TRUE(serialized_token.has_value());
  base::Optional<base::UnguessableToken> deserialized_token =
      AuthSession::GetTokenFromSerializedString(serialized_token.value());
  EXPECT_TRUE(deserialized_token.has_value());
  EXPECT_EQ(deserialized_token.value(), original_token);
}

}  // namespace cryptohome
