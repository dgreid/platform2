// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for SecureString.

#include "brillo/secure_string.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brillo {

static constexpr char str1[] = "abc";
static constexpr char str2[] = "def";
static constexpr char str3[] = "abc";

static_assert(str1 != str3, "The strings should have different addresses");

TEST(SecureClear, SecureClear) {
  std::vector<uint8_t> input = {0xFF, 0xFF, 0xFF};
  SecureClear(input.data(), input.size());
  EXPECT_EQ(input, std::vector<uint8_t>({0x00, 0x00, 0x00}));
}

TEST(SecureClear, SecureClearVector) {
  std::vector<uint8_t> input = {0xFF, 0xFF, 0xFF};
  SecureClear(&input);
  EXPECT_EQ(input, std::vector<uint8_t>({0x00, 0x00, 0x00}));
}

TEST(SecureClear, SecureClearArray) {
  std::array<uint8_t, 3> input = {0xFF, 0xFF, 0xFF};
  SecureClear(&input);
  EXPECT_EQ(input, (std::array<uint8_t, 3>{0x00, 0x00, 0x00}));
}

TEST(SecureClear, SecureClearString) {
  std::string input = "abc";
  EXPECT_EQ(input.size(), 3);
  SecureClear(&input);
  // string has three NULs (plus terminating NUL)
  EXPECT_EQ(input, std::string({0, 0, 0}));
}

TEST(SecureMemcmp, Zero_Size) {
  EXPECT_EQ(SecureMemcmp(nullptr, nullptr, 0), 0);

  // memcmp has the first two arguments marked as non-null:
  // https://sourceware.org/git/?p=glibc.git;a=blob;f=string/string.h;h=b0be00c0f703ae7014fa7c424bfa8767edc500ca;hb=HEAD#l64
  // so we need to disable the warning in order to pass nullptr arguments to
  // it. Otherwise this will fail to compile.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonnull"
  EXPECT_EQ(memcmp(nullptr, nullptr, 0), 0);
#pragma clang diagnostic pop
}

TEST(SecureMemcmp, Different) {
  // The return value for this differs than memcmp, which will return a
  // negative value.
  EXPECT_EQ(SecureMemcmp(str1, str2, sizeof(str1)), 1);
  EXPECT_LT(std::memcmp(str1, str2, sizeof(str1)), 0);

  // memcmp will return a positive value.
  EXPECT_EQ(SecureMemcmp(str2, str1, sizeof(str1)), 1);
  EXPECT_GT(std::memcmp(str2, str1, sizeof(str1)), 0);
}

TEST(SecureMemcmp, Same) {
  EXPECT_EQ(SecureMemcmp(str1, str3, sizeof(str1)), 0);
  EXPECT_EQ(std::memcmp(str1, str3, sizeof(str1)), 0);
}

}  // namespace brillo
