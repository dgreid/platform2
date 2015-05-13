// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_METRICS_COLLECTOR_H_
#define POWER_MANAGER_POWERD_METRICS_COLLECTOR_H_

#include <string>
#include <vector>

#include <base/compiler_specific.h>
#include <base/macros.h>
#include <base/timer/timer.h>
#include <base/time/time.h>
#include <gtest/gtest_prod.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/suspender.h"
#include "power_manager/powerd/system/power_supply.h"

namespace power_manager {

class PrefsInterface;

namespace policy {
class BacklightController;
}

// Used by Daemon to report metrics by way of Chrome.
//
// This class handles the reporting of complex metrics (e.g. tracking the
// session start time and reporting related metrics after the session stops).
//
// Classes that just need to report simple metrics in response to an event
// should use the convenience functions declared in common/metrics_sender.h to
// send metrics directly.
class MetricsCollector {
 public:
  // Returns a copy of |enum_name| with a suffix describing |power_source|
  // appended to it. Public so it can be called by tests.
  static std::string AppendPowerSourceToEnumName(
      const std::string& enum_name,
      PowerSource power_source);

  MetricsCollector();
  ~MetricsCollector();

  // Initializes the object and starts |generate_backlight_metrics_timer_|.
  // Ownership of pointers remains with the caller.
  void Init(PrefsInterface* prefs,
            policy::BacklightController* display_backlight_controller,
            policy::BacklightController* keyboard_backlight_controller,
            const system::PowerStatus& power_status);

  // Records changes to system state.
  void HandleScreenDimmedChange(bool dimmed,
                                base::TimeTicks last_user_activity_time);
  void HandleScreenOffChange(bool off, base::TimeTicks last_user_activity_time);
  void HandleSessionStateChange(SessionState state);
  void HandlePowerStatusUpdate(const system::PowerStatus& status);
  void HandleShutdown(ShutdownReason reason);

  // Called at the beginning of a suspend request (which may consist of multiple
  // suspend attempts).
  void PrepareForSuspend();

  // Called at the end of a successful suspend request. |num_suspend_attempts|
  // contains the number of attempts up to and including the one in which the
  // system successfully suspended.
  void HandleResume(int num_suspend_attempts);

  // Called after a suspend request (that is, a series of one or more suspend
  // attempts performed in response to e.g. the lid being closed) is canceled.
  void HandleCanceledSuspendRequest(int num_suspend_attempts);

  // Called after a suspend request has completed (successfully or not).
  // Generates UMA metrics for dark resume.  The size of |wake_durations| is the
  // number of times the system woke up in dark resume during the suspend
  // request and the value of each element is the time spent in dark resume for
  // the corresponding wake.  |suspend_duration| is the total time the system
  // spent in user-visible suspend (including the time spent in dark resume).
  void GenerateDarkResumeMetrics(
      const std::vector<policy::Suspender::DarkResumeInfo>& wake_durations,
      base::TimeDelta suspend_duration);

  // Generates UMA metrics on when leaving the idle state.
  void GenerateUserActivityMetrics();

  // Generates UMA metrics about the current backlight level.
  void GenerateBacklightLevelMetrics();

  // Handles the power button being pressed or released.
  void HandlePowerButtonEvent(ButtonState state);

  // Sends a metric reporting the amount of time that Chrome took to acknowledge
  // a power button event.
  void SendPowerButtonAcknowledgmentDelayMetric(base::TimeDelta delay);

 private:
  friend class MetricsCollectorTest;
  FRIEND_TEST(MetricsCollectorTest, BacklightLevel);
  FRIEND_TEST(MetricsCollectorTest, SendMetricWithPowerSource);
  FRIEND_TEST(MetricsCollectorTest, WakeReasonToHistogramName);
  FRIEND_TEST(MetricsCollectorTest, GatherDarkResumeMetrics);

  // These methods append the current power source to |name|.
  bool SendMetricWithPowerSource(const std::string& name,
                                 int sample,
                                 int min,
                                 int max,
                                 int num_buckets);
  bool SendEnumMetricWithPowerSource(const std::string& name,
                                     int sample,
                                     int max);

  // Generates a battery discharge rate UMA metric sample. Returns
  // true if a sample was sent to UMA, false otherwise.
  void GenerateBatteryDischargeRateMetric();

  // Sends a histogram sample containing the rate at which the battery
  // discharged while the system was suspended if the system was on battery
  // power both before suspending and after resuming.  Called by
  // GenerateMetricsOnPowerEvent().  Returns true if the sample was sent.
  void GenerateBatteryDischargeRateWhileSuspendedMetric();

  // Increments the number of user sessions that have been active on the
  // current battery charge.
  void IncrementNumOfSessionsPerChargeMetric();

  // Generates number of sessions per charge UMA metric sample if the current
  // stored value is greater then 0.
  void GenerateNumOfSessionsPerChargeMetric();

  PrefsInterface* prefs_;
  policy::BacklightController* display_backlight_controller_;
  policy::BacklightController* keyboard_backlight_controller_;

  Clock clock_;

  // Last power status passed to HandlePowerStatusUpdate().
  system::PowerStatus last_power_status_;

  // Current session state.
  SessionState session_state_;

  // Time at which the current session (if any) started.
  base::TimeTicks session_start_time_;

  // Runs GenerateBacklightLevelMetric().
  base::RepeatingTimer<MetricsCollector> generate_backlight_metrics_timer_;

  // Timestamp of the last generated battery discharge rate metric.
  base::TimeTicks last_battery_discharge_rate_metric_timestamp_;

  // Timestamp of the last time the power button was down.
  base::TimeTicks last_power_button_down_timestamp_;

  // Timestamp of the last idle event (that is, either
  // |screen_dim_timestamp_| or |screen_off_timestamp_|).
  base::TimeTicks last_idle_event_timestamp_;

  // Idle duration as of the last idle event.
  base::TimeDelta last_idle_timedelta_;

  // Timestamps of the last idle-triggered power state transitions.
  base::TimeTicks screen_dim_timestamp_;
  base::TimeTicks screen_off_timestamp_;

  // Information recorded by PrepareForSuspend() just before the system
  // suspends.  |time_before_suspend_| is intentionally base::Time rather
  // than base::TimeTicks because the latter doesn't increase while the
  // system is suspended.
  double battery_energy_before_suspend_;
  bool on_line_power_before_suspend_;
  base::Time time_before_suspend_;

  // Set by HandleResume() to indicate that
  // GenerateBatteryDischargeRateWhileSuspendedMetric() should send a
  // sample when it is next called.
  bool report_battery_discharge_rate_while_suspended_;

  DISALLOW_COPY_AND_ASSIGN(MetricsCollector);
};

}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_METRICS_COLLECTOR_H_
