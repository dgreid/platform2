// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_oldest_activity_timestamp_cache.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/time/time.h>

using base::FilePath;

namespace cryptohome {

void UserOldestActivityTimestampCache::Initialize() {
  CHECK(initialized_ == false);
  initialized_ = true;
}

void UserOldestActivityTimestampCache::AddExistingUser(
    const FilePath& vault, base::Time timestamp) {
  CHECK(initialized_);
  users_timestamp_lookup_.insert(std::make_pair(vault, timestamp));
}

void UserOldestActivityTimestampCache::UpdateExistingUser(
    const FilePath& vault, base::Time timestamp) {
  CHECK(initialized_);
  users_timestamp_lookup_[vault] = timestamp;
}

void UserOldestActivityTimestampCache::RemoveUser(const base::FilePath& vault) {
  CHECK(initialized_);
  users_timestamp_lookup_.erase(vault);
}

base::Time UserOldestActivityTimestampCache::GetLastUserActivityTimestamp(
    const base::FilePath& vault) const {
  CHECK(initialized_);
  auto it = users_timestamp_lookup_.find(vault);

  if (it == users_timestamp_lookup_.end()) {
    return base::Time();
  } else {
    return it->second;
  }
}

}  // namespace cryptohome
