// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THD_MECHANISM_MECHANISM_H_
#define THD_MECHANISM_MECHANISM_H_

#include <cstdint>

#include <base/macros.h>

namespace thd {

// Mechanisms are the way to take actions in thd. This is the mechanism
// interface that all actionable knobs in thd adhere to. Different logical ways
// of triggering an action (e.g. file-writing, dbus signal emission, etc)
// inherit from this, and implement the SetLevel() method.
class Mechanism {
 public:
  // Sets mechanism to requested |level|.
  // Returns true on success.
  // Returns false on failure, or if |level| is outside of the min/max bounds
  // of the mechanism.
  virtual bool SetLevel(int64_t level) = 0;

  // Set mechanism to |percent| between min level, and max level.
  // |percent| needs to be in range [0,100] otherwise SetPercent
  // will return false.
  bool SetPercent(int percent);

  // Informational getters about mechanism setup to inform
  // thermal decisions.
  virtual int64_t GetMaxLevel() = 0;
  virtual int64_t GetMinLevel() = 0;
  virtual int64_t GetDefaultLevel() = 0;

 protected:
  Mechanism();
  Mechanism(const Mechanism&) = delete;
  Mechanism& operator=(const Mechanism&) = delete;

  virtual ~Mechanism();
};

}  // namespace thd

#endif  // THD_MECHANISM_MECHANISM_H_
