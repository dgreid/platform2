// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Platform

#include "cryptohome/fake_platform.h"

#include <string>
#include <sys/types.h>

namespace cryptohome {

// Platform API

bool FakePlatform::GetUserId(const std::string& user,
                             uid_t* user_id,
                             gid_t* group_id) const {
  CHECK(user_id);
  CHECK(group_id);

  if (uids_.find(user) == uids_.end() || gids_.find(user) == gids_.end()) {
    LOG(ERROR) << "No user: " << user;
    return false;
  }

  *user_id = uids_.at(user);
  *group_id = gids_.at(user);
  return true;
}

bool FakePlatform::GetGroupId(const std::string& group, gid_t* group_id) const {
  CHECK(group_id);

  if (gids_.find(group) == gids_.end()) {
    LOG(ERROR) << "No group: " << group;
    return false;
  }

  *group_id = gids_.at(group);
  return true;
}

// Test API

void FakePlatform::SetUserId(const std::string& user, uid_t user_id) {
  CHECK(uids_.find(user) == uids_.end());

  uids_[user] = user_id;
}

void FakePlatform::SetGroupId(const std::string& group, gid_t group_id) {
  CHECK(gids_.find(group) == gids_.end());

  gids_[group] = group_id;
}

void FakePlatform::SetStandardUsersAndGroups() {
  SetUserId(fake_platform::kRoot, fake_platform::kRootUID);
  SetGroupId(fake_platform::kRoot, fake_platform::kRootGID);
  SetUserId(fake_platform::kChapsUser, fake_platform::kChapsUID);
  SetGroupId(fake_platform::kChapsUser, fake_platform::kChapsGID);
  SetUserId(fake_platform::kChronosUser, fake_platform::kChronosUID);
  SetGroupId(fake_platform::kChronosUser, fake_platform::kChronosGID);
  SetGroupId(fake_platform::kSharedGroup, fake_platform::kSharedGID);
}

}  // namespace cryptohome
