// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FAKE_PLATFORM_H_
#define CRYPTOHOME_FAKE_PLATFORM_H_

#include <string>
#include <sys/types.h>
#include <unordered_map>

#include "cryptohome/platform.h"

namespace cryptohome {

namespace fake_platform {
// Common constants
constexpr char kRoot[] = "root";
constexpr char kChapsUser[] = "chaps";
constexpr char kChronosUser[] = "chronos";
constexpr char kSharedGroup[] = "chronos-access";

constexpr uid_t kRootUID = 0;
constexpr gid_t kRootGID = 0;
constexpr uid_t kChapsUID = 42;
constexpr gid_t kChapsGID = 43;
constexpr uid_t kChronosUID = 44;
constexpr gid_t kChronosGID = 45;
constexpr gid_t kSharedGID = 46;
}  // namespace fake_platform

class FakePlatform final : public Platform {
 public:
  FakePlatform() = default;
  ~FakePlatform() override = default;

  // Prohibit copy/move/assignment.
  FakePlatform(const FakePlatform&) = delete;
  FakePlatform(const FakePlatform&&) = delete;
  FakePlatform& operator=(const FakePlatform&) = delete;
  FakePlatform& operator=(const FakePlatform&&) = delete;

  // Platform API

  bool GetUserId(const std::string& user,
                 uid_t* user_id,
                 gid_t* group_id) const override;

  bool GetGroupId(const std::string& group, gid_t* group_id) const override;

  // Test API

  void SetStandardUsersAndGroups();

 private:
  std::unordered_map<std::string, uid_t> uids_;
  std::unordered_map<std::string, gid_t> gids_;

  void SetUserId(const std::string& user, uid_t user_id);
  void SetGroupId(const std::string& group, gid_t group_id);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FAKE_PLATFORM_H_
