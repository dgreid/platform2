// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_
#define CRYPTOHOME_MOCK_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_

#include "cryptohome/user_oldest_activity_timestamp_cache.h"

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

namespace cryptohome {

class MockUserOldestActivityTimestampCache :
    public UserOldestActivityTimestampCache {
 public:
  MockUserOldestActivityTimestampCache();
  virtual ~MockUserOldestActivityTimestampCache();

  MOCK_METHOD(void, Initialize, (), (override));
  MOCK_METHOD(bool, initialized, (), (const, override));
  MOCK_METHOD(void,
              AddExistingUser,
              (const base::FilePath&, base::Time),
              (override));
  MOCK_METHOD(void,
              UpdateExistingUser,
              (const base::FilePath&, base::Time),
              (override));
  MOCK_METHOD(void, AddExistingUserNotime, (const base::FilePath&), (override));
  MOCK_METHOD(base::Time, oldest_known_timestamp, (), (const, override));
  MOCK_METHOD(bool, empty, (), (const, override));
  MOCK_METHOD(base::FilePath, RemoveOldestUser, (), (override));
  MOCK_METHOD(base::Time,
              GetLastUserActivityTimestamp,
              (const base::FilePath&),
              (const, override));

 private:
  base::Time StubOldestKnownTimestamp() const {
    return base::Time();  // null
  }

  base::FilePath StubRemoveOldestUser() {
    return base::FilePath("/SATURATED/REMOVE/OLDEST/USER");
  }
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_
