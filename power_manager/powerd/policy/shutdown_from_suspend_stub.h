// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_SHUTDOWN_FROM_SUSPEND_STUB_H_
#define POWER_MANAGER_POWERD_POLICY_SHUTDOWN_FROM_SUSPEND_STUB_H_

#include "power_manager/powerd/policy/shutdown_from_suspend_interface.h"

#include <base/macros.h>

namespace power_manager {
namespace policy {

class ShutdownFromSuspendStub : public ShutdownFromSuspendInterface {
 public:
  ShutdownFromSuspendStub() = default;
  ShutdownFromSuspendStub(const ShutdownFromSuspendStub&) = delete;
  ShutdownFromSuspendStub& operator=(const ShutdownFromSuspendStub&) = delete;

  ~ShutdownFromSuspendStub() = default;

  void set_action(Action action) { action_ = action; }

  // ShutdownFromSuspendInterface implementation
  Action PrepareForSuspendAttempt() override { return action_; };
  void HandleDarkResume() override{};
  void HandleFullResume() override{};

 private:
  Action action_ = Action::SUSPEND;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_SHUTDOWN_FROM_SUSPEND_STUB_H_
