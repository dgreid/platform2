// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_
#define CRYPTOHOME_MOCK_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_

#include "cryptohome/user_oldest_activity_timestamp_cache.h"

#include <string>

#include <base/time/time.h>

#include <gmock/gmock.h>

namespace cryptohome {

class MockUserOldestActivityTimestampCache :
    public UserOldestActivityTimestampCache {
 public:
  MockUserOldestActivityTimestampCache() = default;
  virtual ~MockUserOldestActivityTimestampCache() = default;

  MOCK_METHOD(void, Initialize, (), (override));
  MOCK_METHOD(bool, initialized, (), (const, override));
  MOCK_METHOD(void,
              AddExistingUser,
              (const std::string&, base::Time),
              (override));
  MOCK_METHOD(void,
              UpdateExistingUser,
              (const std::string&, base::Time),
              (override));
  MOCK_METHOD(void, RemoveUser, (const std::string&), (override));
  MOCK_METHOD(base::Time,
              GetLastUserActivityTimestamp,
              (const std::string&),
              (const, override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_
