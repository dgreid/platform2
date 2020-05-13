// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/crypto_provider.h"

#include <string>

#include <gtest/gtest.h>

#include "shill/crypto_rot47.h"

using testing::Test;

namespace shill {

namespace {
const char kEmptyText[] = "";
const char kPlainText[] = "This is a test!";
const char kROT47Text[] = "rot47:%9:D :D 2 E6DEP";
}  // namespace

class CryptoProviderTest : public Test {
 protected:
  CryptoProvider provider_;
};

TEST_F(CryptoProviderTest, Encrypt) {
  EXPECT_EQ(kROT47Text, provider_.Encrypt(kPlainText));
}

TEST_F(CryptoProviderTest, DecryptNonROT47Fails) {
  EXPECT_FALSE(provider_.Decrypt(kPlainText));
  EXPECT_FALSE(provider_.Decrypt(kEmptyText));
}

TEST_F(CryptoProviderTest, DecryptROT47Succeeds) {
  EXPECT_EQ(kPlainText, provider_.Decrypt(kROT47Text));
}

}  // namespace shill
