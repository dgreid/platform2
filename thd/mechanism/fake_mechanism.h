// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THD_MECHANISM_FAKE_MECHANISM_H_
#define THD_MECHANISM_FAKE_MECHANISM_H_

#include "thd/mechanism/mechanism.h"

#include <cstdint>
#include <string>

#include <base/macros.h>

namespace thd {

// A fake mechanism for debugging that only logs what level
// the mechanism would have been set to.
class FakeMechanism : public Mechanism {
 public:
  FakeMechanism(int64_t max_level,
                int64_t min_level,
                int64_t default_level,
                const std::string& name);
  FakeMechanism(const FakeMechanism&) = delete;
  FakeMechanism& operator=(const FakeMechanism&) = delete;

  ~FakeMechanism() override;

  int64_t GetCurrentLevel();

  // Mechanism:
  bool SetLevel(int64_t level) override;
  int64_t GetDefaultLevel() override;
  int64_t GetMinLevel() override;
  int64_t GetMaxLevel() override;

 private:
  int64_t max_level_;
  int64_t min_level_;
  int64_t default_level_;
  int64_t current_level_;
  std::string name_;
};

}  // namespace thd

#endif  // THD_MECHANISM_FAKE_MECHANISM_H_
