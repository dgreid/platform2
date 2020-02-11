// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_
#define CRYPTOHOME_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_

#include <map>

#include <base/files/file_path.h>
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
  virtual void AddExistingUser(const base::FilePath& vault,
                               base::Time timestamp);

  // Updates a user in the cache with specified oldest activity timestamp.
  virtual void UpdateExistingUser(const base::FilePath& vault,
                                  base::Time timestamp);

  // Adds a user to the cache without oldest activity timestamp. Such
  // users are considered older than any existing user with timestamp.
  virtual void AddExistingUserNotime(const base::FilePath& vault);

  // Timestamp of the oldest user in the cache. May be null (check for
  // is_null) if there is no user with definite timestamp.
  virtual base::Time oldest_known_timestamp() const {
    return oldest_known_timestamp_;
  }

  // Returns true if there are no users in the cache.
  virtual bool empty() const {
    return users_timestamp_.empty();
  }

  // Removes the oldest user stored in the cache. Users without
  // a timestamp are removed first.
  virtual base::FilePath RemoveOldestUser();

  // Returns the last activity timestamp for a user. For users without a
  // timestamp it returns the oldest known timestamp.
  virtual base::Time GetLastUserActivityTimestamp(
      const base::FilePath& vault) const;

 private:
  // Updates oldest known timestamp after the user with |timestamp|
  // has been removed from cache.
  void UpdateTimestampAfterRemoval(base::Time timestamp);

  typedef std::multimap<base::Time, base::FilePath> UsersTimestamp;
  UsersTimestamp users_timestamp_;
  std::map<base::FilePath, base::Time> users_timestamp_lookup_;
  base::Time oldest_known_timestamp_;
  bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(UserOldestActivityTimestampCache);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_OLDEST_ACTIVITY_TIMESTAMP_CACHE_H_
