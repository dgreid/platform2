// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LEADERD_GROUP_CONFIG_H_
#define LEADERD_GROUP_CONFIG_H_

#include <map>
#include <string>

#include <chromeos/any.h>
#include <chromeos/errors/error.h>

namespace leaderd {
namespace group_options {

extern const char kMinWandererTimeoutSeconds[];
extern const char kMaxWandererTimeoutSeconds[];
extern const char kLeaderSteadyStateTimeoutSeconds[];
extern const char kIsPersistent[];

}  // namespace group_options

class GroupConfig final {
 public:
  GroupConfig() = default;

  bool Load(const std::map<std::string, chromeos::Any>& options,
            chromeos::ErrorPtr* error);
  uint64_t PickWandererTimeoutMs() const;
  uint64_t leader_steady_state_timeout_ms() const {
    return leader_steady_state_timeout_ms_;
  }

 private:
  bool is_persistent_{false};
  uint64_t min_wanderer_timeout_ms_{10 * 1000};
  uint64_t max_wanderer_timeout_ms_{30 * 1000};
  uint64_t leader_steady_state_timeout_ms_{5 * 1000};

  DISALLOW_COPY_AND_ASSIGN(GroupConfig);
};

}  // namespace leaderd

#endif  // LEADERD_GROUP_CONFIG_H_
