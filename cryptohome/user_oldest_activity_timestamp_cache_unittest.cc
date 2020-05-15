// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for UsersOldestActivityTimestampCache.

#include "cryptohome/user_oldest_activity_timestamp_cache.h"

#include <base/logging.h>
#include <gtest/gtest.h>

namespace {
const base::Time::Exploded feb1st2011_exploded = { 2011, 2, 2, 1 };
const base::Time::Exploded mar1st2011_exploded = { 2011, 3, 2, 1 };
}  // namespace

namespace cryptohome {

TEST(UserOldestActivityTimestampCache, GetLastUserActivityTimestamp) {
  base::Time time_feb1;
  CHECK(base::Time::FromUTCExploded(feb1st2011_exploded, &time_feb1));
  base::Time time_mar1;
  CHECK(base::Time::FromUTCExploded(mar1st2011_exploded, &time_mar1));

  UserOldestActivityTimestampCache cache;
  cache.Initialize();

  cache.AddExistingUser("b", time_mar1);
  EXPECT_EQ(cache.GetLastUserActivityTimestamp("b"), time_mar1);

  cache.AddExistingUser("c", time_feb1);
  EXPECT_EQ(cache.GetLastUserActivityTimestamp("c"), time_feb1);
  cache.UpdateExistingUser("c", time_mar1);
  EXPECT_EQ(cache.GetLastUserActivityTimestamp("c"), time_mar1);
  cache.RemoveUser("c");
  EXPECT_TRUE(cache.GetLastUserActivityTimestamp("c").is_null());
}

}  // namespace cryptohome
