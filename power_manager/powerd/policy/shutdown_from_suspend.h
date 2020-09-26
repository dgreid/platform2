// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_SHUTDOWN_FROM_SUSPEND_H_
#define POWER_MANAGER_POWERD_POLICY_SHUTDOWN_FROM_SUSPEND_H_

#include "power_manager/powerd/policy/shutdown_from_suspend_interface.h"

#include <memory>

#include <base/macros.h>
#include <base/time/time.h>
#include <components/timers/alarm_timer_chromeos.h>

namespace power_manager {

class PrefsInterface;

namespace system {

class PowerSupplyInterface;

}  // namespace system

namespace policy {

class ShutdownFromSuspend : public ShutdownFromSuspendInterface {
 public:
  ShutdownFromSuspend();
  ~ShutdownFromSuspend() override;

  void Init(PrefsInterface* prefs, system::PowerSupplyInterface* power_supply);

  bool enabled_for_testing() const { return enabled_; }

  // ShutdownFromSuspendInterface implementation.
  Action PrepareForSuspendAttempt() override;
  void HandleDarkResume() override;
  void HandleFullResume() override;

 private:
  ShutdownFromSuspend(std::unique_ptr<timers::SimpleAlarmTimer> alarm_timer);
  friend class ShutdownFromSuspendTest;

  // Invoked by |alarm_timer_| after spending |shutdown_delay_| in suspend.
  void OnTimerWake();

  bool ShouldShutdown();

  // Is shutdown after x enabled ?
  bool enabled_ = false;
  // Time in suspend after which the device wakes up to shut down.
  base::TimeDelta shutdown_delay_;
  // Is the device in dark resume currently ?
  bool in_dark_resume_ = false;
  // Has |alarm_timer_| fired since last full resume.
  bool timer_fired_ = false;
  // Timer to wake the system from suspend after |shutdown_delay_|.
  std::unique_ptr<timers::SimpleAlarmTimer> alarm_timer_;

  system::PowerSupplyInterface* power_supply_ = nullptr;  // weak

  double low_battery_shutdown_percent_ = 0.0;

  DISALLOW_COPY_AND_ASSIGN(ShutdownFromSuspend);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_SHUTDOWN_FROM_SUSPEND_H_
