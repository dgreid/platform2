// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_SUSPEND_DELAY_OBSERVER_H_
#define POWER_MANAGER_POWERD_POLICY_SUSPEND_DELAY_OBSERVER_H_

#include <base/observer_list_types.h>

namespace power_manager {
namespace policy {

class SuspendDelayController;

class SuspendDelayObserver : public base::CheckedObserver {
 public:
  virtual ~SuspendDelayObserver() {}

  // Called when all clients that previously registered suspend delays have
  // reported that they're ready for the system to be suspended.  |suspend_id|
  // identifies the current suspend attempt. If in dark resume, also waits for a
  // minimum delay in anticipation of external monitor enumeration.
  virtual void OnReadyForSuspend(SuspendDelayController* controller,
                                 int suspend_id) = 0;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_SUSPEND_DELAY_OBSERVER_H_
