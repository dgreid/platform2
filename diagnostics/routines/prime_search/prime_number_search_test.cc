// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include <base/macros.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/routines/prime_search/prime_number_search.h"

namespace diagnostics {

namespace {

using ::testing::Return;

class MockPrimeNumberSearchTest : public PrimeNumberSearch {
 public:
  explicit MockPrimeNumberSearchTest(uint64_t max_num)
      : PrimeNumberSearch(max_num) {}
  ~MockPrimeNumberSearchTest() {}

  MOCK_METHOD(bool, IsPrime, (uint64_t num), (const, override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockPrimeNumberSearchTest);
};

}  // namespace

TEST(PrimeNumberSearchTest, IsPrime) {
  PrimeNumberSearch prime_search(4);

  EXPECT_FALSE(prime_search.IsPrime(0));
  EXPECT_FALSE(prime_search.IsPrime(1));
  EXPECT_TRUE(prime_search.IsPrime(2));
  EXPECT_TRUE(prime_search.IsPrime(3));
  EXPECT_FALSE(prime_search.IsPrime(4));
  EXPECT_TRUE(prime_search.IsPrime(999983));
  EXPECT_FALSE(prime_search.IsPrime(999984));
  EXPECT_TRUE(prime_search.IsPrime(360289));
  EXPECT_FALSE(prime_search.IsPrime(360290));
  EXPECT_TRUE(prime_search.IsPrime(122477));
  EXPECT_FALSE(prime_search.IsPrime(122478));
  EXPECT_TRUE(prime_search.IsPrime(828587));
  EXPECT_FALSE(prime_search.IsPrime(828588));
  EXPECT_TRUE(prime_search.IsPrime(87119));
  EXPECT_FALSE(prime_search.IsPrime(87120));
}

// Test Run() returns true when IsPrime() calculates
// correctly.
TEST(PrimeNumberSearchTest, RunPass) {
  MockPrimeNumberSearchTest prime_search(8);

  EXPECT_CALL(prime_search, IsPrime(2)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(3)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(4)).WillOnce(Return(false));
  EXPECT_CALL(prime_search, IsPrime(5)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(6)).WillOnce(Return(false));
  EXPECT_CALL(prime_search, IsPrime(7)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(8)).WillOnce(Return(false));

  EXPECT_TRUE(prime_search.Run());
}

// Test Run() returns false when IsPrime() miscalculate a prime number as
// nonprime when no more prime number between miscalculated number to max_num.
// The error will be discovered by unexpected amount of calculated prime numbers
// when Run() finished the traversing of the whole number sequences.
TEST(PrimeNumberSearchTest,
     RunFailUnexpectedPrimeNumberFollowedWithNoMorePrime) {
  MockPrimeNumberSearchTest prime_search(6);

  EXPECT_CALL(prime_search, IsPrime(2)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(3)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(4)).WillOnce(Return(false));
  // 5 should be prime number and is miscalcuated here.
  EXPECT_CALL(prime_search, IsPrime(5)).WillOnce(Return(false));
  EXPECT_CALL(prime_search, IsPrime(6)).WillOnce(Return(false));

  EXPECT_FALSE(prime_search.Run());
}

// Test Run() returns false when IsPrime() miscalculate a prime number as
// nonprime when there is still some prime numbers between miscalculate number
// to max_num. The error will be discovered by detecting the next correctedly
// calculated prime number's misposistioning in the prime number table.
TEST(PrimeNumberSearchTest, RunFailUnexpectedPrimeNumberFollowedWithPrime) {
  MockPrimeNumberSearchTest prime_search(8);

  EXPECT_CALL(prime_search, IsPrime(2)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(3)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(4)).WillOnce(Return(false));
  // 5 should be prime number and is miscalcuated here.
  EXPECT_CALL(prime_search, IsPrime(5)).WillOnce(Return(false));
  EXPECT_CALL(prime_search, IsPrime(6)).WillOnce(Return(false));
  EXPECT_CALL(prime_search, IsPrime(7)).WillOnce(Return(true));
  // Run() shall stop when mispositioned prime number is discovered.
  EXPECT_CALL(prime_search, IsPrime(8)).Times(0);

  EXPECT_FALSE(prime_search.Run());
}

// Test Run() returns false when IsPrime() miscalculate a nonprime number as
// prime. The error will be discovered by detecting the inequality between this
// miscalculated prime number and the expected value in prime number table.
TEST(PrimeNumberSearchTest, RunFailUnexpectedNonPrimeNumber) {
  MockPrimeNumberSearchTest prime_search(7);

  EXPECT_CALL(prime_search, IsPrime(2)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(3)).WillOnce(Return(true));
  EXPECT_CALL(prime_search, IsPrime(4)).WillOnce(Return(false));
  EXPECT_CALL(prime_search, IsPrime(5)).WillOnce(Return(true));
  // 6 should be nonprime number and is miscalcuated here.
  EXPECT_CALL(prime_search, IsPrime(6)).WillOnce(Return(true));
  // Run() shall stop when unequal prime number value is discovered.
  EXPECT_CALL(prime_search, IsPrime(7)).Times(0);

  EXPECT_FALSE(prime_search.Run());
}

}  // namespace diagnostics
