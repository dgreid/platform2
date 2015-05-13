// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/metrics_collector.h"

#include <stdint.h>

#include <algorithm>
#include <cmath>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "power_manager/common/metrics_constants.h"
#include "power_manager/common/metrics_sender.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/policy/backlight_controller.h"

namespace {
// Generates the histogram name under which dark resume wake duration metrics
// are logged for the dark resume triggered by |wake_reason|.
std::string WakeReasonToHistogramName(const std::string& wake_reason) {
  return std::string("Power.DarkResumeWakeDurationMs.").append(wake_reason);
}
}  // namespace

namespace power_manager {

// static
std::string MetricsCollector::AppendPowerSourceToEnumName(
    const std::string& enum_name,
    PowerSource power_source) {
  return enum_name +
      (power_source == POWER_AC ? kMetricACSuffix : kMetricBatterySuffix);
}

MetricsCollector::MetricsCollector()
    : prefs_(NULL),
      display_backlight_controller_(NULL),
      keyboard_backlight_controller_(NULL),
      session_state_(SESSION_STOPPED),
      battery_energy_before_suspend_(0.0),
      on_line_power_before_suspend_(false),
      report_battery_discharge_rate_while_suspended_(false) {
}

MetricsCollector::~MetricsCollector() {}

void MetricsCollector::Init(
    PrefsInterface* prefs,
    policy::BacklightController* display_backlight_controller,
    policy::BacklightController* keyboard_backlight_controller,
    const system::PowerStatus& power_status) {
  prefs_ = prefs;
  display_backlight_controller_ = display_backlight_controller;
  keyboard_backlight_controller_ = keyboard_backlight_controller;
  last_power_status_ = power_status;

  if (display_backlight_controller_ || keyboard_backlight_controller_) {
    generate_backlight_metrics_timer_.Start(FROM_HERE,
        base::TimeDelta::FromMilliseconds(kMetricBacklightLevelIntervalMs),
        this, &MetricsCollector::GenerateBacklightLevelMetrics);
  }
}

void MetricsCollector::HandleScreenDimmedChange(
    bool dimmed,
    base::TimeTicks last_user_activity_time) {
  if (dimmed) {
    base::TimeTicks now = clock_.GetCurrentTime();
    screen_dim_timestamp_ = now;
    last_idle_event_timestamp_ = now;
    last_idle_timedelta_ = now - last_user_activity_time;
  } else {
    screen_dim_timestamp_ = base::TimeTicks();
  }
}

void MetricsCollector::HandleScreenOffChange(
    bool off,
    base::TimeTicks last_user_activity_time) {
  if (off) {
    base::TimeTicks now = clock_.GetCurrentTime();
    screen_off_timestamp_ = now;
    last_idle_event_timestamp_ = now;
    last_idle_timedelta_ = now - last_user_activity_time;
  } else {
    screen_off_timestamp_ = base::TimeTicks();
  }
}

void MetricsCollector::HandleSessionStateChange(SessionState state) {
  if (state == session_state_)
    return;

  session_state_ = state;

  switch (state) {
    case SESSION_STARTED:
      session_start_time_ = clock_.GetCurrentTime();
      if (!last_power_status_.line_power_on)
        IncrementNumOfSessionsPerChargeMetric();
      if (last_power_status_.battery_is_present) {
        // Enum to avoid exponential histogram's varyingly-sized buckets.
        SendEnumMetricWithPowerSource(
            kMetricBatteryRemainingAtStartOfSessionName,
            static_cast<int>(round(last_power_status_.battery_percentage)),
            kMetricMaxPercent);
      }
      break;
    case SESSION_STOPPED: {
      if (last_power_status_.battery_is_present) {
        // Enum to avoid exponential histogram's varyingly-sized buckets.
        SendEnumMetricWithPowerSource(
            kMetricBatteryRemainingAtEndOfSessionName,
            static_cast<int>(round(last_power_status_.battery_percentage)),
            kMetricMaxPercent);
      }

      SendMetric(kMetricLengthOfSessionName,
                 (clock_.GetCurrentTime() - session_start_time_).InSeconds(),
                 kMetricLengthOfSessionMin,
                 kMetricLengthOfSessionMax,
                 kMetricDefaultBuckets);

      if (display_backlight_controller_) {
        SendMetric(kMetricNumberOfAlsAdjustmentsPerSessionName,
                   display_backlight_controller_->
                       GetNumAmbientLightSensorAdjustments(),
                   kMetricNumberOfAlsAdjustmentsPerSessionMin,
                   kMetricNumberOfAlsAdjustmentsPerSessionMax,
                   kMetricDefaultBuckets);
        SendMetricWithPowerSource(
            kMetricUserBrightnessAdjustmentsPerSessionName,
            display_backlight_controller_->GetNumUserAdjustments(),
            kMetricUserBrightnessAdjustmentsPerSessionMin,
            kMetricUserBrightnessAdjustmentsPerSessionMax,
            kMetricDefaultBuckets);
      }
      break;
    }
  }
}

void MetricsCollector::HandlePowerStatusUpdate(
    const system::PowerStatus& status) {
  const bool previously_on_line_power = last_power_status_.line_power_on;
  last_power_status_ = status;

  if (status.line_power_on && !previously_on_line_power) {
    GenerateNumOfSessionsPerChargeMetric();
    if (status.battery_is_present) {
      // Enum to avoid exponential histogram's varyingly-sized buckets.
      SendEnumMetric(kMetricBatteryRemainingWhenChargeStartsName,
                     static_cast<int>(round(status.battery_percentage)),
                     kMetricMaxPercent);
      SendEnumMetric(kMetricBatteryChargeHealthName,
                     static_cast<int>(round(100.0 * status.battery_charge_full /
                                            status.battery_charge_full_design)),
                     kMetricBatteryChargeHealthMax);
    }
  } else if (!status.line_power_on && previously_on_line_power) {
    if (session_state_ == SESSION_STARTED)
      IncrementNumOfSessionsPerChargeMetric();
  }

  GenerateBatteryDischargeRateMetric();
  GenerateBatteryDischargeRateWhileSuspendedMetric();

  SendEnumMetric(kMetricBatteryInfoSampleName,
                 BATTERY_INFO_READ,
                 BATTERY_INFO_MAX);
  // TODO(derat): Continue sending BATTERY_INFO_BAD in some situations?
  // Remove this metric entirely?
  SendEnumMetric(kMetricBatteryInfoSampleName,
                 BATTERY_INFO_GOOD,
                 BATTERY_INFO_MAX);
}

void MetricsCollector::HandleShutdown(ShutdownReason reason) {
  SendEnumMetric(kMetricShutdownReasonName, static_cast<int>(reason),
                 kMetricShutdownReasonMax);
}

void MetricsCollector::PrepareForSuspend() {
  battery_energy_before_suspend_ = last_power_status_.battery_energy;
  on_line_power_before_suspend_ = last_power_status_.line_power_on;
  time_before_suspend_ = clock_.GetCurrentWallTime();
}

void MetricsCollector::HandleResume(int num_suspend_attempts) {
  SendMetric(kMetricSuspendAttemptsBeforeSuccessName, num_suspend_attempts,
             kMetricSuspendAttemptsMin, kMetricSuspendAttemptsMax,
             kMetricSuspendAttemptsBuckets);
  // Report the discharge rate in response to the next
  // OnPowerStatusUpdate() call.
  report_battery_discharge_rate_while_suspended_ = true;
}

void MetricsCollector::HandleCanceledSuspendRequest(int num_suspend_attempts) {
  SendMetric(kMetricSuspendAttemptsBeforeCancelName, num_suspend_attempts,
             kMetricSuspendAttemptsMin, kMetricSuspendAttemptsMax,
             kMetricSuspendAttemptsBuckets);
}

void MetricsCollector::GenerateDarkResumeMetrics(
    const std::vector<policy::Suspender::DarkResumeInfo>& wake_durations,
    base::TimeDelta suspend_duration) {
  if (suspend_duration.InSeconds() <= 0) return;

  // We want to get metrics even if the system suspended for less than an hour
  // so we scale the number of wakes up.
  static const int kSecondsPerHour = 60 * 60;
  const int64 wakeups_per_hour =
      wake_durations.size() * kSecondsPerHour / suspend_duration.InSeconds();
  SendMetric(kMetricDarkResumeWakeupsPerHourName, wakeups_per_hour,
             kMetricDarkResumeWakeupsPerHourMin,
             kMetricDarkResumeWakeupsPerHourMax,
             kMetricDefaultBuckets);

  for (const auto& pair : wake_durations) {
    // Send aggregated dark resume duration metric.
    SendMetric(kMetricDarkResumeWakeDurationMsName,
               pair.second.InMilliseconds(),
               kMetricDarkResumeWakeDurationMsMin,
               kMetricDarkResumeWakeDurationMsMax,
               kMetricDefaultBuckets);
    // Send wake reason-specific dark resume duration metric.
    SendMetric(WakeReasonToHistogramName(pair.first),
               pair.second.InMilliseconds(),
               kMetricDarkResumeWakeDurationMsMin,
               kMetricDarkResumeWakeDurationMsMax,
               kMetricDefaultBuckets);
  }
}

void MetricsCollector::GenerateUserActivityMetrics() {
  if (last_idle_event_timestamp_.is_null())
    return;

  base::TimeTicks current_time = clock_.GetCurrentTime();
  base::TimeDelta event_delta = current_time - last_idle_event_timestamp_;
  base::TimeDelta total_delta = event_delta + last_idle_timedelta_;
  last_idle_event_timestamp_ = base::TimeTicks();

  SendMetricWithPowerSource(kMetricIdleName, total_delta.InMilliseconds(),
      kMetricIdleMin, kMetricIdleMax, kMetricDefaultBuckets);

  if (!screen_dim_timestamp_.is_null()) {
    base::TimeDelta dim_event_delta = current_time - screen_dim_timestamp_;
    SendMetricWithPowerSource(kMetricIdleAfterDimName,
                              dim_event_delta.InMilliseconds(),
                              kMetricIdleAfterDimMin,
                              kMetricIdleAfterDimMax,
                              kMetricDefaultBuckets);
    screen_dim_timestamp_ = base::TimeTicks();
  }
  if (!screen_off_timestamp_.is_null()) {
    base::TimeDelta screen_off_event_delta =
        current_time - screen_off_timestamp_;
    SendMetricWithPowerSource(kMetricIdleAfterScreenOffName,
                              screen_off_event_delta.InMilliseconds(),
                              kMetricIdleAfterScreenOffMin,
                              kMetricIdleAfterScreenOffMax,
                              kMetricDefaultBuckets);
    screen_off_timestamp_ = base::TimeTicks();
  }
}

void MetricsCollector::GenerateBacklightLevelMetrics() {
  if (!screen_dim_timestamp_.is_null() || !screen_off_timestamp_.is_null())
    return;

  double percent = 0.0;
  if (display_backlight_controller_ &&
      display_backlight_controller_->GetBrightnessPercent(&percent)) {
    // Enum to avoid exponential histogram's varyingly-sized buckets.
    SendEnumMetricWithPowerSource(kMetricBacklightLevelName, lround(percent),
                                  kMetricMaxPercent);
  }
  if (keyboard_backlight_controller_ &&
      keyboard_backlight_controller_->GetBrightnessPercent(&percent)) {
    // Enum to avoid exponential histogram's varyingly-sized buckets.
    SendEnumMetric(kMetricKeyboardBacklightLevelName, lround(percent),
                   kMetricMaxPercent);
  }
}

void MetricsCollector::HandlePowerButtonEvent(ButtonState state) {
  switch (state) {
    case BUTTON_DOWN:
      // Just keep track of the time when the button was pressed.
      if (!last_power_button_down_timestamp_.is_null()) {
        LOG(ERROR) << "Got power-button-down event while button was already "
                   << "down";
      }
      last_power_button_down_timestamp_ = clock_.GetCurrentTime();
      break;
    case BUTTON_UP: {
      // Metrics are sent after the button is released.
      if (last_power_button_down_timestamp_.is_null()) {
        LOG(ERROR) << "Got power-button-up event while button was already up";
      } else {
        base::TimeDelta delta =
            clock_.GetCurrentTime() - last_power_button_down_timestamp_;
        last_power_button_down_timestamp_ = base::TimeTicks();
        SendMetric(kMetricPowerButtonDownTimeName,
                   delta.InMilliseconds(),
                   kMetricPowerButtonDownTimeMin,
                   kMetricPowerButtonDownTimeMax,
                   kMetricDefaultBuckets);
      }
      break;
    }
    case BUTTON_REPEAT:
      // Ignore repeat events if we get them.
      break;
  }
}

void MetricsCollector::SendPowerButtonAcknowledgmentDelayMetric(
    base::TimeDelta delay) {
  SendMetric(kMetricPowerButtonAcknowledgmentDelayName,
             delay.InMilliseconds(),
             kMetricPowerButtonAcknowledgmentDelayMin,
             kMetricPowerButtonAcknowledgmentDelayMax,
             kMetricDefaultBuckets);
}

bool MetricsCollector::SendMetricWithPowerSource(const std::string& name,
                                                 int sample,
                                                 int min,
                                                 int max,
                                                 int num_buckets) {
  const std::string full_name = AppendPowerSourceToEnumName(
      name, last_power_status_.line_power_on ? POWER_AC : POWER_BATTERY);
  return SendMetric(full_name, sample, min, max, num_buckets);
}

bool MetricsCollector::SendEnumMetricWithPowerSource(const std::string& name,
                                                     int sample,
                                                     int max) {
  const std::string full_name = AppendPowerSourceToEnumName(
      name, last_power_status_.line_power_on ? POWER_AC : POWER_BATTERY);
  return SendEnumMetric(full_name, sample, max);
}

void MetricsCollector::GenerateBatteryDischargeRateMetric() {
  // The battery discharge rate metric is relevant and collected only
  // when running on battery.
  if (!last_power_status_.battery_is_present ||
      last_power_status_.line_power_on)
    return;

  // Converts the discharge rate from W to mW.
  int rate = static_cast<int>(
      round(last_power_status_.battery_energy_rate * 1000));
  if (rate <= 0)
    return;

  // Ensures that the metric is not generated too frequently.
  if (!last_battery_discharge_rate_metric_timestamp_.is_null() &&
      (clock_.GetCurrentTime() -
       last_battery_discharge_rate_metric_timestamp_).InSeconds() <
      kMetricBatteryDischargeRateInterval) {
    return;
  }

  if (SendMetric(kMetricBatteryDischargeRateName, rate,
                 kMetricBatteryDischargeRateMin,
                 kMetricBatteryDischargeRateMax,
                 kMetricDefaultBuckets))
    last_battery_discharge_rate_metric_timestamp_ = clock_.GetCurrentTime();
}

void MetricsCollector::GenerateBatteryDischargeRateWhileSuspendedMetric() {
  // Do nothing unless this is the first time we're called after resuming.
  if (!report_battery_discharge_rate_while_suspended_)
    return;
  report_battery_discharge_rate_while_suspended_ = false;

  if (!last_power_status_.battery_is_present ||
      on_line_power_before_suspend_ ||
      last_power_status_.line_power_on)
    return;

  base::TimeDelta elapsed_time =
      clock_.GetCurrentWallTime() - time_before_suspend_;
  if (elapsed_time.InSeconds() <
      kMetricBatteryDischargeRateWhileSuspendedMinSuspendSec)
    return;

  double discharged_watt_hours =
      battery_energy_before_suspend_ - last_power_status_.battery_energy;
  double discharge_rate_watts =
      discharged_watt_hours / (elapsed_time.InSecondsF() / 3600);

  // Maybe the charger was connected while the system was suspended but
  // disconnected before it resumed.
  if (discharge_rate_watts < 0.0)
    return;

  SendMetric(kMetricBatteryDischargeRateWhileSuspendedName,
             static_cast<int>(round(discharge_rate_watts * 1000)),
             kMetricBatteryDischargeRateWhileSuspendedMin,
             kMetricBatteryDischargeRateWhileSuspendedMax,
             kMetricDefaultBuckets);
}

void MetricsCollector::IncrementNumOfSessionsPerChargeMetric() {
  int64_t num = 0;
  prefs_->GetInt64(kNumSessionsOnCurrentChargePref, &num);
  num = std::max(num, static_cast<int64_t>(0));
  prefs_->SetInt64(kNumSessionsOnCurrentChargePref, num + 1);
}

void MetricsCollector::GenerateNumOfSessionsPerChargeMetric() {
  int64_t sample = 0;
  prefs_->GetInt64(kNumSessionsOnCurrentChargePref, &sample);
  if (sample <= 0)
    return;

  sample = std::min(
      sample, static_cast<int64_t>(kMetricNumOfSessionsPerChargeMax));
  prefs_->SetInt64(kNumSessionsOnCurrentChargePref, 0);
  SendMetric(kMetricNumOfSessionsPerChargeName,
             sample,
             kMetricNumOfSessionsPerChargeMin,
             kMetricNumOfSessionsPerChargeMax,
             kMetricDefaultBuckets);
}

}  // namespace power_manager
