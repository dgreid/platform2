// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/user_oldest_activity_timestamp_cache.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <base/time/time.h>

namespace cryptohome {

void UserOldestActivityTimestampCache::Initialize() {
  CHECK(initialized_ == false);
  initialized_ = true;
}

void UserOldestActivityTimestampCache::AddExistingUser(const std::string& user,
                                                       base::Time timestamp) {
  CHECK(initialized_);
  users_timestamp_lookup_.insert(std::make_pair(user, timestamp));
}

void UserOldestActivityTimestampCache::UpdateExistingUser(
    const std::string& user, base::Time timestamp) {
  CHECK(initialized_);
  users_timestamp_lookup_[user] = timestamp;
}

void UserOldestActivityTimestampCache::RemoveUser(const std::string& user) {
  CHECK(initialized_);
  users_timestamp_lookup_.erase(user);
}

base::Time UserOldestActivityTimestampCache::GetLastUserActivityTimestamp(
    const std::string& user) const {
  CHECK(initialized_);
  auto it = users_timestamp_lookup_.find(user);

  if (it == users_timestamp_lookup_.end()) {
    return base::Time();
  } else {
    return it->second;
  }
}

}  // namespace cryptohome
