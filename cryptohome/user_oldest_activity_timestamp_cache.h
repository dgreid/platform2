// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_
#define CRYPTOHOME_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_

#include <map>
#include <string>

#include <base/macros.h>
#include <base/time/time.h>

namespace cryptohome {

// Cache of last access timestamp for existing users.
class UserOldestActivityTimestampCache {
 public:
  UserOldestActivityTimestampCache() : initialized_(false) { }
  virtual ~UserOldestActivityTimestampCache() { }

  // Initialize the cache. This must be done only once. No methods
  // must be accessed before that.  Chrome initializes cache and
  // starts using it when hourly cleanup callback faces lack of disk
  // space.  If cryptohomed restarts for some reason, cache becomes
  // uninitialized and will be re-initialized (and filled) again on
  // the nearest convenience (cleanup callback).
  virtual void Initialize();
  virtual bool initialized() const {
    return initialized_;
  }

  // Adds a user to the cache with specified oldest activity timestamp.
  virtual void AddExistingUser(const std::string& user, base::Time timestamp);

  // Updates a user in the cache with specified oldest activity timestamp.
  virtual void UpdateExistingUser(const std::string& user,
                                  base::Time timestamp);

  // Remove a user from the cache.
  virtual void RemoveUser(const std::string& user);

  // Returns the last activity timestamp for a user. For users without a
  // timestamp it returns a NULL time.
  virtual base::Time GetLastUserActivityTimestamp(
      const std::string& user) const;

 private:
  std::map<std::string, base::Time> users_timestamp_lookup_;
  bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(UserOldestActivityTimestampCache);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_
