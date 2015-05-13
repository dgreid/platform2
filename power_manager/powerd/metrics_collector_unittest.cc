// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/metrics_collector.h"

#include <stdint.h>

#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/format_macros.h>
#include <base/logging.h>
#include <base/timer/timer.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/metrics_constants.h"
#include "power_manager/common/metrics_sender.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/backlight_controller_stub.h"
#include "power_manager/powerd/policy/suspender.h"
#include "power_manager/powerd/system/power_supply.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Mock;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Test;

namespace power_manager {

class MetricsCollectorTest : public Test {
 public:
  MetricsCollectorTest()
      : metrics_lib_(new StrictMock<MetricsLibraryMock>),
        metrics_sender_(
            scoped_ptr<MetricsLibraryInterface>(metrics_lib_)) {
    collector_.clock_.set_current_time_for_testing(
        base::TimeTicks::FromInternalValue(1000));
    collector_.clock_.set_current_wall_time_for_testing(
        base::Time::FromInternalValue(2000));

    power_status_.battery_percentage = 100.0;
    power_status_.battery_is_present = true;
  }

 protected:
  // Initializes |collector_|.
  void Init() {
    collector_.Init(&prefs_, &display_backlight_controller_,
                    &keyboard_backlight_controller_, power_status_);
  }

  // Advances both the monotonically-increasing time and wall time by
  // |interval|.
  void AdvanceTime(base::TimeDelta interval) {
    collector_.clock_.set_current_time_for_testing(
        collector_.clock_.GetCurrentTime() + interval);
    collector_.clock_.set_current_wall_time_for_testing(
        collector_.clock_.GetCurrentWallTime() + interval);
  }

  // Adds expectations to ignore all metrics sent by HandleSessionStateChange()
  // (except ones listed in |metrics_to_test_|).
  void IgnoreHandleSessionStateChangeMetrics() {
    IgnoreEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kMetricBatteryRemainingAtStartOfSessionName, POWER_AC));
    IgnoreEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kMetricBatteryRemainingAtStartOfSessionName, POWER_BATTERY));
    IgnoreEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kMetricBatteryRemainingAtEndOfSessionName, POWER_AC));
    IgnoreEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kMetricBatteryRemainingAtEndOfSessionName, POWER_BATTERY));
    IgnoreMetric(kMetricLengthOfSessionName);
    IgnoreMetric(kMetricNumberOfAlsAdjustmentsPerSessionName);
    IgnoreMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kMetricUserBrightnessAdjustmentsPerSessionName, POWER_AC));
    IgnoreMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kMetricUserBrightnessAdjustmentsPerSessionName, POWER_BATTERY));
  }

  // Adds expectations to ignore all metrics sent by HandlePowerStatusUpdate()
  // (except ones listed in |metrics_to_test_|).
  void IgnoreHandlePowerStatusUpdateMetrics() {
    IgnoreMetric(kMetricNumOfSessionsPerChargeName);
    IgnoreEnumMetric(kMetricBatteryRemainingWhenChargeStartsName);
    IgnoreEnumMetric(kMetricBatteryChargeHealthName);
    IgnoreMetric(kMetricBatteryDischargeRateName);
    IgnoreMetric(kMetricBatteryDischargeRateWhileSuspendedName);
    IgnoreEnumMetric(kMetricBatteryInfoSampleName);
  }

  // Updates |power_status_|'s |line_power_on| member and passes it to
  // HandlePowerStatusUpdate().
  void UpdatePowerStatusLinePower(bool line_power_on) {
    power_status_.line_power_on = line_power_on;
    collector_.HandlePowerStatusUpdate(power_status_);
  }

  // Adds a metrics library mock expectation that the specified metric
  // will be generated.
  void ExpectMetric(const std::string& name,
                    int sample,
                    int min,
                    int max,
                    int buckets) {
    EXPECT_CALL(*metrics_lib_, SendToUMA(name, sample, min, max, buckets))
        .Times(1)
        .WillOnce(Return(true))
        .RetiresOnSaturation();
  }

  // Adds a metrics library mock expectation that the specified enum
  // metric will be generated.
  void ExpectEnumMetric(const std::string& name, int sample, int max) {
    EXPECT_CALL(*metrics_lib_, SendEnumToUMA(name, sample, max))
        .Times(1)
        .WillOnce(Return(true))
        .RetiresOnSaturation();
  }

  // Ignores an arbitrary number of reports of |name|.
  void IgnoreMetric(const std::string& name) {
    if (metrics_to_test_.count(name))
      return;
    EXPECT_CALL(*metrics_lib_, SendToUMA(name, _, _, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
  }

  // Ignores an arbitrary number of reports of |name|.
  void IgnoreEnumMetric(const std::string& name) {
    if (metrics_to_test_.count(name))
      return;
    EXPECT_CALL(*metrics_lib_, SendEnumToUMA(name, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
  }

  void ExpectBatteryDischargeRateMetric(int sample) {
    ExpectMetric(kMetricBatteryDischargeRateName, sample,
                 kMetricBatteryDischargeRateMin,
                 kMetricBatteryDischargeRateMax,
                 kMetricDefaultBuckets);
  }

  void ExpectNumOfSessionsPerChargeMetric(int sample) {
    ExpectMetric(kMetricNumOfSessionsPerChargeName, sample,
                 kMetricNumOfSessionsPerChargeMin,
                 kMetricNumOfSessionsPerChargeMax,
                 kMetricDefaultBuckets);
  }

  FakePrefs prefs_;
  policy::BacklightControllerStub display_backlight_controller_;
  policy::BacklightControllerStub keyboard_backlight_controller_;
  system::PowerStatus power_status_;

  // StrictMock turns all unexpected calls into hard failures.
  StrictMock<MetricsLibraryMock>* metrics_lib_;  // Weak pointer.
  MetricsSender metrics_sender_;

  MetricsCollector collector_;

  // Names of metrics that will not be ignored by calls to Ignore*(). Tests
  // should insert the metrics that they're testing into this set and then call
  // Ignore*Metrics() (and call it again whenever expectations are cleared).
  std::set<std::string> metrics_to_test_;
};

TEST_F(MetricsCollectorTest, BacklightLevel) {
  power_status_.line_power_on = false;
  Init();
  ASSERT_TRUE(collector_.generate_backlight_metrics_timer_.IsRunning());
  collector_.HandleScreenDimmedChange(true, base::TimeTicks::Now());
  collector_.GenerateBacklightLevelMetrics();
  Mock::VerifyAndClearExpectations(metrics_lib_);

  const int64_t kCurrentDisplayPercent = 57;
  display_backlight_controller_.set_percent(kCurrentDisplayPercent);
  const int64_t kCurrentKeyboardPercent = 43;
  keyboard_backlight_controller_.set_percent(kCurrentKeyboardPercent);

  collector_.HandleScreenDimmedChange(false, base::TimeTicks::Now());
  ExpectEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
                       kMetricBacklightLevelName, POWER_BATTERY),
                   kCurrentDisplayPercent, kMetricMaxPercent);
  ExpectEnumMetric(kMetricKeyboardBacklightLevelName, kCurrentKeyboardPercent,
                   kMetricMaxPercent);
  collector_.GenerateBacklightLevelMetrics();

  power_status_.line_power_on = true;
  IgnoreHandlePowerStatusUpdateMetrics();
  collector_.HandlePowerStatusUpdate(power_status_);
  ExpectEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
                       kMetricBacklightLevelName, POWER_AC),
                   kCurrentDisplayPercent, kMetricMaxPercent);
  ExpectEnumMetric(kMetricKeyboardBacklightLevelName, kCurrentKeyboardPercent,
                   kMetricMaxPercent);
  collector_.GenerateBacklightLevelMetrics();
}

TEST_F(MetricsCollectorTest, BatteryDischargeRate) {
  power_status_.line_power_on = false;
  Init();

  metrics_to_test_.insert(kMetricBatteryDischargeRateName);
  IgnoreHandlePowerStatusUpdateMetrics();

  // This much time must elapse before the discharge rate will be reported
  // again.
  const base::TimeDelta interval = base::TimeDelta::FromSeconds(
      kMetricBatteryDischargeRateInterval);

  power_status_.battery_energy_rate = 5.0;
  ExpectBatteryDischargeRateMetric(5000);
  collector_.HandlePowerStatusUpdate(power_status_);

  power_status_.battery_energy_rate = 4.5;
  ExpectBatteryDischargeRateMetric(4500);
  AdvanceTime(interval);
  collector_.HandlePowerStatusUpdate(power_status_);

  power_status_.battery_energy_rate = 6.4;
  ExpectBatteryDischargeRateMetric(6400);
  AdvanceTime(interval);
  collector_.HandlePowerStatusUpdate(power_status_);

  Mock::VerifyAndClearExpectations(metrics_lib_);
  IgnoreHandlePowerStatusUpdateMetrics();

  // Another update before the full interval has elapsed shouldn't result in
  // another report.
  AdvanceTime(interval / 2);
  collector_.HandlePowerStatusUpdate(power_status_);

  // Neither should a call while the energy rate is negative.
  AdvanceTime(interval);
  power_status_.battery_energy_rate = -4.0;
  collector_.HandlePowerStatusUpdate(power_status_);

  // Ditto for a call while the system is on AC power.
  power_status_.line_power_on = true;
  power_status_.battery_energy_rate = 4.0;
  collector_.HandlePowerStatusUpdate(power_status_);
}

TEST_F(MetricsCollectorTest, BatteryInfoWhenChargeStarts) {
  const double kBatteryPercentages[] = { 10.1, 10.7, 82.4, 82.5, 100.0 };

  power_status_.line_power_on = false;
  power_status_.battery_charge_full_design = 100.0;
  Init();

  metrics_to_test_.insert(kMetricBatteryRemainingWhenChargeStartsName);
  metrics_to_test_.insert(kMetricBatteryChargeHealthName);

  for (size_t i = 0; i < arraysize(kBatteryPercentages); ++i) {
    IgnoreHandlePowerStatusUpdateMetrics();

    power_status_.line_power_on = false;
    power_status_.battery_charge_full = kBatteryPercentages[i];
    power_status_.battery_percentage = kBatteryPercentages[i];
    collector_.HandlePowerStatusUpdate(power_status_);

    power_status_.line_power_on = true;
    ExpectEnumMetric(kMetricBatteryRemainingWhenChargeStartsName,
                     round(power_status_.battery_percentage),
                     kMetricMaxPercent);
    ExpectEnumMetric(kMetricBatteryChargeHealthName,
                     round(100.0 * power_status_.battery_charge_full /
                           power_status_.battery_charge_full_design),
                     kMetricBatteryChargeHealthMax);
    collector_.HandlePowerStatusUpdate(power_status_);

    Mock::VerifyAndClearExpectations(metrics_lib_);
  }
}

TEST_F(MetricsCollectorTest, SessionStartOrStop) {
  const uint kAlsAdjustments[] = { 0, 100 };
  const uint kUserAdjustments[] = { 0, 200 };
  const double kBatteryPercentages[] = { 10.5, 23.0 };
  const int kSessionSecs[] = { 900, kMetricLengthOfSessionMax + 10 };
  ASSERT_EQ(arraysize(kAlsAdjustments), arraysize(kUserAdjustments));
  ASSERT_EQ(arraysize(kAlsAdjustments), arraysize(kBatteryPercentages));
  ASSERT_EQ(arraysize(kAlsAdjustments), arraysize(kSessionSecs));

  power_status_.line_power_on = false;
  Init();

  for (size_t i = 0; i < arraysize(kAlsAdjustments); ++i) {
    IgnoreHandlePowerStatusUpdateMetrics();
    power_status_.battery_percentage = kBatteryPercentages[i];
    ExpectEnumMetric(
        MetricsCollector::AppendPowerSourceToEnumName(
            kMetricBatteryRemainingAtStartOfSessionName, POWER_BATTERY),
        round(kBatteryPercentages[i]), kMetricMaxPercent);
    collector_.HandlePowerStatusUpdate(power_status_);
    collector_.HandleSessionStateChange(SESSION_STARTED);
    Mock::VerifyAndClearExpectations(metrics_lib_);

    ExpectEnumMetric(
        MetricsCollector::AppendPowerSourceToEnumName(
            kMetricBatteryRemainingAtEndOfSessionName, POWER_BATTERY),
        round(kBatteryPercentages[i]), kMetricMaxPercent);

    display_backlight_controller_.set_num_als_adjustments(kAlsAdjustments[i]);
    display_backlight_controller_.set_num_user_adjustments(kUserAdjustments[i]);
    ExpectMetric(kMetricNumberOfAlsAdjustmentsPerSessionName,
                 kAlsAdjustments[i],
                 kMetricNumberOfAlsAdjustmentsPerSessionMin,
                 kMetricNumberOfAlsAdjustmentsPerSessionMax,
                 kMetricDefaultBuckets);
    ExpectMetric(
        MetricsCollector::AppendPowerSourceToEnumName(
            kMetricUserBrightnessAdjustmentsPerSessionName, POWER_BATTERY),
        kUserAdjustments[i],
        kMetricUserBrightnessAdjustmentsPerSessionMin,
        kMetricUserBrightnessAdjustmentsPerSessionMax,
        kMetricDefaultBuckets);

    AdvanceTime(base::TimeDelta::FromSeconds(kSessionSecs[i]));
    ExpectMetric(kMetricLengthOfSessionName,
                 kSessionSecs[i],
                 kMetricLengthOfSessionMin,
                 kMetricLengthOfSessionMax,
                 kMetricDefaultBuckets);

    collector_.HandleSessionStateChange(SESSION_STOPPED);
    Mock::VerifyAndClearExpectations(metrics_lib_);
  }
}

TEST_F(MetricsCollectorTest, GenerateNumOfSessionsPerChargeMetric) {
  metrics_to_test_.insert(kMetricNumOfSessionsPerChargeName);
  power_status_.line_power_on = false;
  Init();

  IgnoreHandlePowerStatusUpdateMetrics();
  UpdatePowerStatusLinePower(true);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // If the session is already started when going off line power, it should be
  // counted. Additional power status updates that don't describe a power source
  // change shouldn't increment the count.
  IgnoreHandleSessionStateChangeMetrics();
  collector_.HandleSessionStateChange(SESSION_STARTED);
  IgnoreHandlePowerStatusUpdateMetrics();
  UpdatePowerStatusLinePower(false);
  UpdatePowerStatusLinePower(false);
  UpdatePowerStatusLinePower(false);
  ExpectNumOfSessionsPerChargeMetric(1);
  UpdatePowerStatusLinePower(true);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // Sessions that start while on battery power should also be counted.
  IgnoreHandleSessionStateChangeMetrics();
  collector_.HandleSessionStateChange(SESSION_STOPPED);
  IgnoreHandlePowerStatusUpdateMetrics();
  UpdatePowerStatusLinePower(false);
  collector_.HandleSessionStateChange(SESSION_STARTED);
  collector_.HandleSessionStateChange(SESSION_STOPPED);
  collector_.HandleSessionStateChange(SESSION_STARTED);
  collector_.HandleSessionStateChange(SESSION_STOPPED);
  collector_.HandleSessionStateChange(SESSION_STARTED);
  ExpectNumOfSessionsPerChargeMetric(3);
  UpdatePowerStatusLinePower(true);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // Check that the pref is used, so the count will persist across reboots.
  IgnoreHandlePowerStatusUpdateMetrics();
  UpdatePowerStatusLinePower(false);
  prefs_.SetInt64(kNumSessionsOnCurrentChargePref, 5);
  ExpectNumOfSessionsPerChargeMetric(5);
  UpdatePowerStatusLinePower(true);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // Negative values in the pref should be ignored.
  prefs_.SetInt64(kNumSessionsOnCurrentChargePref, -2);
  IgnoreHandlePowerStatusUpdateMetrics();
  UpdatePowerStatusLinePower(false);
  ExpectNumOfSessionsPerChargeMetric(1);
  UpdatePowerStatusLinePower(true);
  Mock::VerifyAndClearExpectations(metrics_lib_);
}

TEST_F(MetricsCollectorTest, SendEnumMetric) {
  Init();
  ExpectEnumMetric("Dummy.EnumMetric", 50, 200);
  EXPECT_TRUE(SendEnumMetric("Dummy.EnumMetric", 50, 200));

  // Out-of-bounds values should be capped.
  ExpectEnumMetric("Dummy.EnumMetric2", 20, 20);
  EXPECT_TRUE(SendEnumMetric("Dummy.EnumMetric2", 21, 20));
}

TEST_F(MetricsCollectorTest, SendMetric) {
  Init();
  ExpectMetric("Dummy.Metric", 3, 1, 100, 50);
  EXPECT_TRUE(SendMetric("Dummy.Metric", 3, 1, 100, 50));

  // Out-of-bounds values should not be capped (so they can instead land in the
  // underflow or overflow bucket).
  ExpectMetric("Dummy.Metric2", -1, 0, 20, 4);
  EXPECT_TRUE(SendMetric("Dummy.Metric2", -1, 0, 20, 4));
  ExpectMetric("Dummy.Metric3", 30, 5, 25, 6);
  EXPECT_TRUE(SendMetric("Dummy.Metric3", 30, 5, 25, 6));
}

TEST_F(MetricsCollectorTest, SendMetricWithPowerSource) {
  power_status_.line_power_on = false;
  Init();
  ExpectMetric("Dummy.MetricOnBattery", 3, 1, 100, 50);
  EXPECT_TRUE(collector_.SendMetricWithPowerSource(
      "Dummy.Metric", 3, 1, 100, 50));

  IgnoreHandlePowerStatusUpdateMetrics();
  power_status_.line_power_on = true;
  collector_.HandlePowerStatusUpdate(power_status_);
  ExpectMetric("Dummy.MetricOnAC", 6, 2, 200, 80);
  EXPECT_TRUE(collector_.SendMetricWithPowerSource(
      "Dummy.Metric", 6, 2, 200, 80));
}

TEST_F(MetricsCollectorTest, PowerButtonDownMetric) {
  Init();

  // We should ignore a button release that wasn't preceded by a press.
  collector_.HandlePowerButtonEvent(BUTTON_UP);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // Presses that are followed by additional presses should also be ignored.
  collector_.HandlePowerButtonEvent(BUTTON_DOWN);
  collector_.HandlePowerButtonEvent(BUTTON_DOWN);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // Send a regular sequence of events and check that the duration is reported.
  collector_.HandlePowerButtonEvent(BUTTON_DOWN);
  const base::TimeDelta kDuration = base::TimeDelta::FromMilliseconds(243);
  AdvanceTime(kDuration);
  ExpectMetric(kMetricPowerButtonDownTimeName,
               kDuration.InMilliseconds(),
               kMetricPowerButtonDownTimeMin,
               kMetricPowerButtonDownTimeMax,
               kMetricDefaultBuckets);
  collector_.HandlePowerButtonEvent(BUTTON_UP);
}

TEST_F(MetricsCollectorTest, GatherDarkResumeMetrics) {
  Init();

  std::vector<policy::Suspender::DarkResumeInfo> wake_durations;
  base::TimeDelta suspend_duration;
  base::TimeDelta kTimeDelta1 = base::TimeDelta::FromSeconds(2);
  base::TimeDelta kTimeDelta2 = base::TimeDelta::FromSeconds(6);
  base::TimeDelta kTimeDelta3 = base::TimeDelta::FromMilliseconds(573);
  base::TimeDelta kTimeDelta4 = base::TimeDelta::FromSeconds(7);
  std::string kWakeReason1 = "WiFi.Pattern";
  std::string kWakeReason2 = "WiFi.Disconnect";
  std::string kWakeReason3 = "WiFi.SSID";
  std::string kWakeReason4 = "Other";
  std::string kExpectedHistogramPrefix = "Power.DarkResumeWakeDurationMs.";
  std::string kExpectedHistogram1 = kExpectedHistogramPrefix + kWakeReason1;
  std::string kExpectedHistogram2 = kExpectedHistogramPrefix + kWakeReason2;
  std::string kExpectedHistogram3 = kExpectedHistogramPrefix + kWakeReason3;
  std::string kExpectedHistogram4 = kExpectedHistogramPrefix + kWakeReason4;

  // First test the basic case.
  wake_durations.push_back(std::make_pair(kWakeReason1, kTimeDelta1));
  wake_durations.push_back(std::make_pair(kWakeReason2, kTimeDelta2));
  wake_durations.push_back(std::make_pair(kWakeReason3, kTimeDelta3));
  wake_durations.push_back(std::make_pair(kWakeReason4, kTimeDelta4));

  suspend_duration = base::TimeDelta::FromHours(2);

  ExpectMetric(kMetricDarkResumeWakeupsPerHourName,
               wake_durations.size() / suspend_duration.InHours(),
               kMetricDarkResumeWakeupsPerHourMin,
               kMetricDarkResumeWakeupsPerHourMax, kMetricDefaultBuckets);
  for (const auto& pair : wake_durations) {
    const base::TimeDelta& duration = pair.second;
    ExpectMetric(kMetricDarkResumeWakeDurationMsName, duration.InMilliseconds(),
                 kMetricDarkResumeWakeDurationMsMin,
                 kMetricDarkResumeWakeDurationMsMax, kMetricDefaultBuckets);
  }
  ExpectMetric(kExpectedHistogram1, kTimeDelta1.InMilliseconds(),
               kMetricDarkResumeWakeDurationMsMin,
               kMetricDarkResumeWakeDurationMsMax, kMetricDefaultBuckets);
  ExpectMetric(kExpectedHistogram2, kTimeDelta2.InMilliseconds(),
               kMetricDarkResumeWakeDurationMsMin,
               kMetricDarkResumeWakeDurationMsMax, kMetricDefaultBuckets);
  ExpectMetric(kExpectedHistogram3, kTimeDelta3.InMilliseconds(),
               kMetricDarkResumeWakeDurationMsMin,
               kMetricDarkResumeWakeDurationMsMax, kMetricDefaultBuckets);
  ExpectMetric(kExpectedHistogram4, kTimeDelta4.InMilliseconds(),
               kMetricDarkResumeWakeDurationMsMin,
               kMetricDarkResumeWakeDurationMsMax, kMetricDefaultBuckets);

  collector_.GenerateDarkResumeMetrics(wake_durations, suspend_duration);

  // If the suspend lasts for less than an hour, the wakeups per hour should be
  // scaled up.
  Mock::VerifyAndClearExpectations(metrics_lib_);
  wake_durations.clear();

  wake_durations.push_back(
      std::make_pair(kWakeReason1, base::TimeDelta::FromMilliseconds(359)));
  suspend_duration = base::TimeDelta::FromMinutes(13);

  IgnoreMetric(kMetricDarkResumeWakeDurationMsName);
  IgnoreMetric(kExpectedHistogram1);
  ExpectMetric(kMetricDarkResumeWakeupsPerHourName, 4,
               kMetricDarkResumeWakeupsPerHourMin,
               kMetricDarkResumeWakeupsPerHourMax, kMetricDefaultBuckets);

  collector_.GenerateDarkResumeMetrics(wake_durations, suspend_duration);
}

TEST_F(MetricsCollectorTest, BatteryDischargeRateWhileSuspended) {
  const double kEnergyBeforeSuspend = 60;
  const double kEnergyAfterResume = 50;
  const base::TimeDelta kSuspendDuration = base::TimeDelta::FromHours(1);

  metrics_to_test_.insert(kMetricBatteryDischargeRateWhileSuspendedName);
  power_status_.line_power_on = false;
  power_status_.battery_energy = kEnergyAfterResume;
  Init();

  // We shouldn't send a sample if we haven't suspended.
  IgnoreHandlePowerStatusUpdateMetrics();
  collector_.HandlePowerStatusUpdate(power_status_);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // Ditto if the system is on AC before suspending...
  power_status_.line_power_on = true;
  power_status_.battery_energy = kEnergyBeforeSuspend;
  IgnoreHandlePowerStatusUpdateMetrics();
  collector_.HandlePowerStatusUpdate(power_status_);
  collector_.PrepareForSuspend();
  AdvanceTime(kSuspendDuration);
  ExpectMetric(kMetricSuspendAttemptsBeforeSuccessName, 1,
               kMetricSuspendAttemptsMin, kMetricSuspendAttemptsMax,
               kMetricSuspendAttemptsBuckets);
  collector_.HandleResume(1);
  power_status_.line_power_on = false;
  power_status_.battery_energy = kEnergyAfterResume;
  collector_.HandlePowerStatusUpdate(power_status_);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // ... or after resuming...
  power_status_.line_power_on = false;
  power_status_.battery_energy = kEnergyBeforeSuspend;
  IgnoreHandlePowerStatusUpdateMetrics();
  collector_.HandlePowerStatusUpdate(power_status_);
  collector_.PrepareForSuspend();
  AdvanceTime(kSuspendDuration);
  ExpectMetric(kMetricSuspendAttemptsBeforeSuccessName, 2,
               kMetricSuspendAttemptsMin, kMetricSuspendAttemptsMax,
               kMetricSuspendAttemptsBuckets);
  collector_.HandleResume(2);
  power_status_.line_power_on = true;
  power_status_.battery_energy = kEnergyAfterResume;
  collector_.HandlePowerStatusUpdate(power_status_);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // ... or if the battery's energy increased while the system was
  // suspended (i.e. it was temporarily connected to AC while suspended).
  power_status_.line_power_on = false;
  power_status_.battery_energy = kEnergyBeforeSuspend;
  IgnoreHandlePowerStatusUpdateMetrics();
  collector_.HandlePowerStatusUpdate(power_status_);
  collector_.PrepareForSuspend();
  AdvanceTime(kSuspendDuration);
  ExpectMetric(kMetricSuspendAttemptsBeforeSuccessName, 1,
               kMetricSuspendAttemptsMin, kMetricSuspendAttemptsMax,
               kMetricSuspendAttemptsBuckets);
  collector_.HandleResume(1);
  power_status_.battery_energy = kEnergyBeforeSuspend + 5.0;
  collector_.HandlePowerStatusUpdate(power_status_);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // The sample also shouldn't be reported if the system wasn't suspended
  // for very long.
  power_status_.battery_energy = kEnergyBeforeSuspend;
  IgnoreHandlePowerStatusUpdateMetrics();
  collector_.HandlePowerStatusUpdate(power_status_);
  collector_.PrepareForSuspend();
  AdvanceTime(base::TimeDelta::FromSeconds(
      kMetricBatteryDischargeRateWhileSuspendedMinSuspendSec - 1));
  ExpectMetric(kMetricSuspendAttemptsBeforeSuccessName, 1,
               kMetricSuspendAttemptsMin, kMetricSuspendAttemptsMax,
               kMetricSuspendAttemptsBuckets);
  collector_.HandleResume(1);
  power_status_.battery_energy = kEnergyAfterResume;
  collector_.HandlePowerStatusUpdate(power_status_);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // The sample should be reported if the energy decreased over a long
  // enough time.
  power_status_.battery_energy = kEnergyBeforeSuspend;
  IgnoreHandlePowerStatusUpdateMetrics();
  collector_.HandlePowerStatusUpdate(power_status_);
  collector_.PrepareForSuspend();
  AdvanceTime(kSuspendDuration);
  ExpectMetric(kMetricSuspendAttemptsBeforeSuccessName, 1,
               kMetricSuspendAttemptsMin, kMetricSuspendAttemptsMax,
               kMetricSuspendAttemptsBuckets);
  collector_.HandleResume(1);
  power_status_.battery_energy = kEnergyAfterResume;
  const int rate_mw = static_cast<int>(round(
      1000 * (kEnergyBeforeSuspend - kEnergyAfterResume) /
      (kSuspendDuration.InSecondsF() / 3600)));
  ExpectMetric(kMetricBatteryDischargeRateWhileSuspendedName, rate_mw,
               kMetricBatteryDischargeRateWhileSuspendedMin,
               kMetricBatteryDischargeRateWhileSuspendedMax,
               kMetricDefaultBuckets);
  collector_.HandlePowerStatusUpdate(power_status_);
}

}  // namespace power_manager
