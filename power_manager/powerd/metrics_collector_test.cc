// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/metrics_collector.h"

#include <stdint.h>

#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
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
namespace metrics {

class MetricsCollectorTest : public Test {
 public:
  MetricsCollectorTest()
      : metrics_lib_(new StrictMock<MetricsLibraryMock>),
        metrics_sender_(
            std::unique_ptr<MetricsLibraryInterface>(metrics_lib_)) {
    collector_.clock_.set_current_time_for_testing(
        base::TimeTicks::FromInternalValue(1000));
    collector_.clock_.set_current_boot_time_for_testing(
        base::TimeTicks::FromInternalValue(2000));
    CHECK(temp_root_dir_.CreateUniqueTempDir());
    collector_.set_prefix_path_for_testing(temp_root_dir_.GetPath());

    power_status_.battery_percentage = 100.0;
    power_status_.battery_charge_full = 100.0;
    power_status_.battery_charge_full_design = 100.0;
    power_status_.battery_is_present = true;
    power_status_.line_power_type = "Mains";
  }

 protected:
  // Initializes |collector_|.
  void Init() {
    collector_.Init(&prefs_, &display_backlight_controller_,
                    &keyboard_backlight_controller_, power_status_,
                    first_run_after_boot_);
  }

  // Advances both the monotonically-increasing time and wall time by
  // |interval|.
  void AdvanceTime(base::TimeDelta interval) {
    collector_.clock_.set_current_time_for_testing(
        collector_.clock_.GetCurrentTime() + interval);
    collector_.clock_.set_current_boot_time_for_testing(
        collector_.clock_.GetCurrentBootTime() + interval);
  }

  // Adds expectations to ignore all metrics sent by HandleSessionStateChange()
  // (except ones listed in |metrics_to_test_|).
  void IgnoreHandleSessionStateChangeMetrics() {
    IgnoreEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kBatteryRemainingAtStartOfSessionName, PowerSource::AC));
    IgnoreEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kBatteryRemainingAtStartOfSessionName, PowerSource::BATTERY));
    IgnoreEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kBatteryRemainingAtEndOfSessionName, PowerSource::AC));
    IgnoreEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kBatteryRemainingAtEndOfSessionName, PowerSource::BATTERY));
    IgnoreMetric(kLengthOfSessionName);
    IgnoreMetric(kNumberOfAlsAdjustmentsPerSessionName);
    IgnoreMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kUserBrightnessAdjustmentsPerSessionName, PowerSource::AC));
    IgnoreMetric(MetricsCollector::AppendPowerSourceToEnumName(
        kUserBrightnessAdjustmentsPerSessionName, PowerSource::BATTERY));
  }

  // Adds expectations to ignore all metrics sent by HandlePowerStatusUpdate()
  // (except ones listed in |metrics_to_test_|).
  void IgnoreHandlePowerStatusUpdateMetrics() {
    IgnoreMetric(kNumOfSessionsPerChargeName);
    IgnoreEnumMetric(kBatteryRemainingWhenChargeStartsName);
    IgnoreEnumMetric(kBatteryChargeHealthName);
    IgnoreMetric(kBatteryDischargeRateName);
    IgnoreMetric(kBatteryDischargeRateWhileSuspendedName);
    IgnoreEnumMetric(kBatteryInfoSampleName);
    IgnoreEnumMetric(kPowerSupplyTypeName);
    IgnoreEnumMetric(kPowerSupplyMaxVoltageName);
    IgnoreEnumMetric(kPowerSupplyMaxPowerName);
    IgnoreEnumMetric(kConnectedChargingPortsName);
  }

  // Updates |power_status_|'s |line_power_on| member and passes it to
  // HandlePowerStatusUpdate().
  void UpdatePowerStatusLinePower(bool line_power_on) {
    power_status_.line_power_on = line_power_on;
    collector_.HandlePowerStatusUpdate(power_status_);
  }

  // Adds a metrics library mock expectation that the specified metric
  // will be generated.
  void ExpectMetric(
      const std::string& name, int sample, int min, int max, int buckets) {
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
    ExpectMetric(kBatteryDischargeRateName, sample, kBatteryDischargeRateMin,
                 kBatteryDischargeRateMax, kDefaultDischargeBuckets);
  }

  void ExpectNumOfSessionsPerChargeMetric(int sample) {
    ExpectMetric(kNumOfSessionsPerChargeName, sample,
                 kNumOfSessionsPerChargeMin, kNumOfSessionsPerChargeMax,
                 kDefaultBuckets);
  }

  // Returns |orig| rooted within the temporary root dir created for testing.
  base::FilePath GetPath(const base::FilePath& orig) const {
    return temp_root_dir_.GetPath().Append(orig.value().substr(1));
  }

  FakePrefs prefs_;
  policy::BacklightControllerStub display_backlight_controller_;
  policy::BacklightControllerStub keyboard_backlight_controller_;
  system::PowerStatus power_status_;
  bool first_run_after_boot_ = false;

  // StrictMock turns all unexpected calls into hard failures.
  StrictMock<MetricsLibraryMock>* metrics_lib_;  // Weak pointer.
  MetricsSender metrics_sender_;

  MetricsCollector collector_;

  // Names of metrics that will not be ignored by calls to Ignore*(). Tests
  // should insert the metrics that they're testing into this set and then call
  // Ignore*Metrics() (and call it again whenever expectations are cleared).
  std::set<std::string> metrics_to_test_;

  base::ScopedTempDir temp_root_dir_;
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
                       kBacklightLevelName, PowerSource::BATTERY),
                   kCurrentDisplayPercent, kMaxPercent);
  ExpectEnumMetric(kKeyboardBacklightLevelName, kCurrentKeyboardPercent,
                   kMaxPercent);
  collector_.GenerateBacklightLevelMetrics();

  power_status_.line_power_on = true;
  IgnoreHandlePowerStatusUpdateMetrics();
  collector_.HandlePowerStatusUpdate(power_status_);
  ExpectEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
                       kBacklightLevelName, PowerSource::AC),
                   kCurrentDisplayPercent, kMaxPercent);
  ExpectEnumMetric(kKeyboardBacklightLevelName, kCurrentKeyboardPercent,
                   kMaxPercent);
  collector_.GenerateBacklightLevelMetrics();
}

TEST_F(MetricsCollectorTest, BatteryDischargeRate) {
  power_status_.line_power_on = false;
  Init();

  metrics_to_test_.insert(kBatteryDischargeRateName);
  IgnoreHandlePowerStatusUpdateMetrics();

  // This much time must elapse before the discharge rate will be reported
  // again.
  const base::TimeDelta interval =
      base::TimeDelta::FromSeconds(kBatteryDischargeRateIntervalSec);

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
  const double kBatteryPercentages[] = {10.1, 10.7, 82.4, 82.5, 100.0};

  power_status_.line_power_on = false;
  power_status_.battery_charge_full_design = 100.0;
  Init();

  metrics_to_test_.insert(kBatteryRemainingWhenChargeStartsName);
  metrics_to_test_.insert(kBatteryChargeHealthName);

  for (size_t i = 0; i < base::size(kBatteryPercentages); ++i) {
    IgnoreHandlePowerStatusUpdateMetrics();

    power_status_.line_power_on = false;
    power_status_.battery_charge_full = kBatteryPercentages[i];
    power_status_.battery_percentage = kBatteryPercentages[i];
    collector_.HandlePowerStatusUpdate(power_status_);

    power_status_.line_power_on = true;
    ExpectEnumMetric(kBatteryRemainingWhenChargeStartsName,
                     round(power_status_.battery_percentage), kMaxPercent);
    ExpectEnumMetric(kBatteryChargeHealthName,
                     round(100.0 * power_status_.battery_charge_full /
                           power_status_.battery_charge_full_design),
                     kBatteryChargeHealthMax);
    collector_.HandlePowerStatusUpdate(power_status_);

    Mock::VerifyAndClearExpectations(metrics_lib_);
  }
}

TEST_F(MetricsCollectorTest, SessionStartOrStop) {
  const uint kAlsAdjustments[] = {0, 100};
  const uint kUserAdjustments[] = {0, 200};
  const double kBatteryPercentages[] = {10.5, 23.0};
  const int kSessionSecs[] = {900, kLengthOfSessionMax + 10};
  ASSERT_EQ(base::size(kAlsAdjustments), base::size(kUserAdjustments));
  ASSERT_EQ(base::size(kAlsAdjustments), base::size(kBatteryPercentages));
  ASSERT_EQ(base::size(kAlsAdjustments), base::size(kSessionSecs));

  power_status_.line_power_on = false;
  Init();

  for (size_t i = 0; i < base::size(kAlsAdjustments); ++i) {
    IgnoreHandlePowerStatusUpdateMetrics();
    power_status_.battery_percentage = kBatteryPercentages[i];
    ExpectEnumMetric(
        MetricsCollector::AppendPowerSourceToEnumName(
            kBatteryRemainingAtStartOfSessionName, PowerSource::BATTERY),
        round(kBatteryPercentages[i]), kMaxPercent);
    collector_.HandlePowerStatusUpdate(power_status_);
    collector_.HandleSessionStateChange(SessionState::STARTED);
    Mock::VerifyAndClearExpectations(metrics_lib_);

    ExpectEnumMetric(
        MetricsCollector::AppendPowerSourceToEnumName(
            kBatteryRemainingAtEndOfSessionName, PowerSource::BATTERY),
        round(kBatteryPercentages[i]), kMaxPercent);

    display_backlight_controller_.set_num_als_adjustments(kAlsAdjustments[i]);
    display_backlight_controller_.set_num_user_adjustments(kUserAdjustments[i]);
    ExpectMetric(kNumberOfAlsAdjustmentsPerSessionName, kAlsAdjustments[i],
                 kNumberOfAlsAdjustmentsPerSessionMin,
                 kNumberOfAlsAdjustmentsPerSessionMax, kDefaultBuckets);
    ExpectMetric(
        MetricsCollector::AppendPowerSourceToEnumName(
            kUserBrightnessAdjustmentsPerSessionName, PowerSource::BATTERY),
        kUserAdjustments[i], kUserBrightnessAdjustmentsPerSessionMin,
        kUserBrightnessAdjustmentsPerSessionMax, kDefaultBuckets);

    AdvanceTime(base::TimeDelta::FromSeconds(kSessionSecs[i]));
    ExpectMetric(kLengthOfSessionName, kSessionSecs[i], kLengthOfSessionMin,
                 kLengthOfSessionMax, kDefaultBuckets);

    collector_.HandleSessionStateChange(SessionState::STOPPED);
    Mock::VerifyAndClearExpectations(metrics_lib_);
  }
}

TEST_F(MetricsCollectorTest, GenerateNumOfSessionsPerChargeMetric) {
  metrics_to_test_.insert(kNumOfSessionsPerChargeName);
  power_status_.line_power_on = false;
  Init();

  IgnoreHandlePowerStatusUpdateMetrics();
  UpdatePowerStatusLinePower(true);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // If the session is already started when going off line power, it should be
  // counted. Additional power status updates that don't describe a power source
  // change shouldn't increment the count.
  IgnoreHandleSessionStateChangeMetrics();
  collector_.HandleSessionStateChange(SessionState::STARTED);
  IgnoreHandlePowerStatusUpdateMetrics();
  UpdatePowerStatusLinePower(false);
  UpdatePowerStatusLinePower(false);
  UpdatePowerStatusLinePower(false);
  ExpectNumOfSessionsPerChargeMetric(1);
  UpdatePowerStatusLinePower(true);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // Sessions that start while on battery power should also be counted.
  IgnoreHandleSessionStateChangeMetrics();
  collector_.HandleSessionStateChange(SessionState::STOPPED);
  IgnoreHandlePowerStatusUpdateMetrics();
  UpdatePowerStatusLinePower(false);
  collector_.HandleSessionStateChange(SessionState::STARTED);
  collector_.HandleSessionStateChange(SessionState::STOPPED);
  collector_.HandleSessionStateChange(SessionState::STARTED);
  collector_.HandleSessionStateChange(SessionState::STOPPED);
  collector_.HandleSessionStateChange(SessionState::STARTED);
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
  EXPECT_TRUE(
      collector_.SendMetricWithPowerSource("Dummy.Metric", 3, 1, 100, 50));

  IgnoreHandlePowerStatusUpdateMetrics();
  power_status_.line_power_on = true;
  collector_.HandlePowerStatusUpdate(power_status_);
  ExpectMetric("Dummy.MetricOnAC", 6, 2, 200, 80);
  EXPECT_TRUE(
      collector_.SendMetricWithPowerSource("Dummy.Metric", 6, 2, 200, 80));
}

TEST_F(MetricsCollectorTest, PowerButtonDownMetric) {
  Init();

  // We should ignore a button release that wasn't preceded by a press.
  collector_.HandlePowerButtonEvent(ButtonState::UP);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // Presses that are followed by additional presses should also be ignored.
  collector_.HandlePowerButtonEvent(ButtonState::DOWN);
  collector_.HandlePowerButtonEvent(ButtonState::DOWN);
  Mock::VerifyAndClearExpectations(metrics_lib_);

  // Send a regular sequence of events and check that the duration is reported.
  collector_.HandlePowerButtonEvent(ButtonState::DOWN);
  const base::TimeDelta kDuration = base::TimeDelta::FromMilliseconds(243);
  AdvanceTime(kDuration);
  ExpectMetric(kPowerButtonDownTimeName, kDuration.InMilliseconds(),
               kPowerButtonDownTimeMin, kPowerButtonDownTimeMax,
               kDefaultBuckets);
  collector_.HandlePowerButtonEvent(ButtonState::UP);
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

  ExpectMetric(kDarkResumeWakeupsPerHourName,
               wake_durations.size() / suspend_duration.InHours(),
               kDarkResumeWakeupsPerHourMin, kDarkResumeWakeupsPerHourMax,
               kDefaultBuckets);
  for (const auto& pair : wake_durations) {
    const base::TimeDelta& duration = pair.second;
    ExpectMetric(kDarkResumeWakeDurationMsName, duration.InMilliseconds(),
                 kDarkResumeWakeDurationMsMin, kDarkResumeWakeDurationMsMax,
                 kDefaultBuckets);
  }
  ExpectMetric(kExpectedHistogram1, kTimeDelta1.InMilliseconds(),
               kDarkResumeWakeDurationMsMin, kDarkResumeWakeDurationMsMax,
               kDefaultBuckets);
  ExpectMetric(kExpectedHistogram2, kTimeDelta2.InMilliseconds(),
               kDarkResumeWakeDurationMsMin, kDarkResumeWakeDurationMsMax,
               kDefaultBuckets);
  ExpectMetric(kExpectedHistogram3, kTimeDelta3.InMilliseconds(),
               kDarkResumeWakeDurationMsMin, kDarkResumeWakeDurationMsMax,
               kDefaultBuckets);
  ExpectMetric(kExpectedHistogram4, kTimeDelta4.InMilliseconds(),
               kDarkResumeWakeDurationMsMin, kDarkResumeWakeDurationMsMax,
               kDefaultBuckets);

  collector_.GenerateDarkResumeMetrics(wake_durations, suspend_duration);

  // If the suspend lasts for less than an hour, the wakeups per hour should be
  // scaled up.
  Mock::VerifyAndClearExpectations(metrics_lib_);
  wake_durations.clear();

  wake_durations.push_back(
      std::make_pair(kWakeReason1, base::TimeDelta::FromMilliseconds(359)));
  suspend_duration = base::TimeDelta::FromMinutes(13);

  IgnoreMetric(kDarkResumeWakeDurationMsName);
  IgnoreMetric(kExpectedHistogram1);
  ExpectMetric(kDarkResumeWakeupsPerHourName, 4, kDarkResumeWakeupsPerHourMin,
               kDarkResumeWakeupsPerHourMax, kDefaultBuckets);

  collector_.GenerateDarkResumeMetrics(wake_durations, suspend_duration);
}

TEST_F(MetricsCollectorTest, BatteryDischargeRateWhileSuspended) {
  const double kEnergyBeforeSuspend = 60;
  const double kEnergyAfterResume = 50;
  const base::TimeDelta kSuspendDuration = base::TimeDelta::FromHours(1);

  metrics_to_test_.insert(kBatteryDischargeRateWhileSuspendedName);
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
  ExpectMetric(kSuspendAttemptsBeforeSuccessName, 1, kSuspendAttemptsMin,
               kSuspendAttemptsMax, kSuspendAttemptsBuckets);
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
  ExpectMetric(kSuspendAttemptsBeforeSuccessName, 2, kSuspendAttemptsMin,
               kSuspendAttemptsMax, kSuspendAttemptsBuckets);
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
  ExpectMetric(kSuspendAttemptsBeforeSuccessName, 1, kSuspendAttemptsMin,
               kSuspendAttemptsMax, kSuspendAttemptsBuckets);
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
      kBatteryDischargeRateWhileSuspendedMinSuspendSec - 1));
  ExpectMetric(kSuspendAttemptsBeforeSuccessName, 1, kSuspendAttemptsMin,
               kSuspendAttemptsMax, kSuspendAttemptsBuckets);
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
  ExpectMetric(kSuspendAttemptsBeforeSuccessName, 1, kSuspendAttemptsMin,
               kSuspendAttemptsMax, kSuspendAttemptsBuckets);
  collector_.HandleResume(1);
  power_status_.battery_energy = kEnergyAfterResume;
  const int rate_mw = static_cast<int>(
      round(1000 * (kEnergyBeforeSuspend - kEnergyAfterResume) /
            (kSuspendDuration.InSecondsF() / 3600)));
  ExpectMetric(kBatteryDischargeRateWhileSuspendedName, rate_mw,
               kBatteryDischargeRateWhileSuspendedMin,
               kBatteryDischargeRateWhileSuspendedMax,
               kDefaultDischargeBuckets);
  collector_.HandlePowerStatusUpdate(power_status_);
}

TEST_F(MetricsCollectorTest, PowerSupplyMaxVoltageAndPower) {
  metrics_to_test_ = {
      kPowerSupplyMaxVoltageName,
      kPowerSupplyMaxPowerName,
  };
  IgnoreHandlePowerStatusUpdateMetrics();
  power_status_.line_power_on = false;
  Init();

  power_status_.line_power_on = true;
  power_status_.line_power_max_voltage = 4.2;
  power_status_.line_power_max_current = 12.7;
  ExpectEnumMetric(
      kPowerSupplyMaxVoltageName,
      static_cast<int>(round(power_status_.line_power_max_voltage)),
      kPowerSupplyMaxVoltageMax);
  ExpectEnumMetric(
      kPowerSupplyMaxPowerName,
      static_cast<int>(round(power_status_.line_power_max_voltage *
                             power_status_.line_power_max_current)),
      kPowerSupplyMaxPowerMax);
  collector_.HandlePowerStatusUpdate(power_status_);

  // Nothing should be reported when line power is off.
  power_status_.line_power_on = false;
  collector_.HandlePowerStatusUpdate(power_status_);
}

TEST_F(MetricsCollectorTest, PowerSupplyType) {
  metrics_to_test_ = {kPowerSupplyTypeName};
  IgnoreHandlePowerStatusUpdateMetrics();
  power_status_.line_power_on = false;
  Init();

  power_status_.line_power_on = true;
  power_status_.line_power_type = system::PowerSupply::kUsbPdType;
  ExpectEnumMetric(kPowerSupplyTypeName,
                   static_cast<int>(PowerSupplyType::USB_PD),
                   static_cast<int>(PowerSupplyType::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);

  power_status_.line_power_type = system::PowerSupply::kBrickIdType;
  ExpectEnumMetric(kPowerSupplyTypeName,
                   static_cast<int>(PowerSupplyType::BRICK_ID),
                   static_cast<int>(PowerSupplyType::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);

  power_status_.line_power_type = "BOGUS";
  ExpectEnumMetric(kPowerSupplyTypeName,
                   static_cast<int>(PowerSupplyType::OTHER),
                   static_cast<int>(PowerSupplyType::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);

  // Nothing should be reported when line power is off.
  power_status_.line_power_on = false;
  collector_.HandlePowerStatusUpdate(power_status_);
}

TEST_F(MetricsCollectorTest, ConnectedChargingPorts) {
  metrics_to_test_ = {kConnectedChargingPortsName};
  IgnoreHandlePowerStatusUpdateMetrics();
  Init();

  // Start out without any ports.
  ExpectEnumMetric(kConnectedChargingPortsName,
                   static_cast<int>(ConnectedChargingPorts::NONE),
                   static_cast<int>(ConnectedChargingPorts::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);

  // Add a single disconnected port.
  power_status_.ports.emplace_back();
  ExpectEnumMetric(kConnectedChargingPortsName,
                   static_cast<int>(ConnectedChargingPorts::NONE),
                   static_cast<int>(ConnectedChargingPorts::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);

  // Connect the port to a dedicated charger.
  power_status_.ports[0].role =
      system::PowerStatus::Port::Role::DEDICATED_SOURCE;
  ExpectEnumMetric(kConnectedChargingPortsName,
                   static_cast<int>(ConnectedChargingPorts::PORT1),
                   static_cast<int>(ConnectedChargingPorts::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);

  // Add a second disconnected port.
  power_status_.ports.emplace_back();
  ExpectEnumMetric(kConnectedChargingPortsName,
                   static_cast<int>(ConnectedChargingPorts::PORT1),
                   static_cast<int>(ConnectedChargingPorts::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);

  // Connect the second port to a dual-role device.
  power_status_.ports[1].role = system::PowerStatus::Port::Role::DUAL_ROLE;
  ExpectEnumMetric(kConnectedChargingPortsName,
                   static_cast<int>(ConnectedChargingPorts::PORT1_PORT2),
                   static_cast<int>(ConnectedChargingPorts::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);

  // Disconnect the first port.
  power_status_.ports[0].role = system::PowerStatus::Port::Role::NONE;
  ExpectEnumMetric(kConnectedChargingPortsName,
                   static_cast<int>(ConnectedChargingPorts::PORT2),
                   static_cast<int>(ConnectedChargingPorts::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);

  // Add a third port, which this code doesn't support.
  power_status_.ports.emplace_back();
  ExpectEnumMetric(kConnectedChargingPortsName,
                   static_cast<int>(ConnectedChargingPorts::TOO_MANY_PORTS),
                   static_cast<int>(ConnectedChargingPorts::MAX));
  collector_.HandlePowerStatusUpdate(power_status_);
}

TEST_F(MetricsCollectorTest, TestBatteryMetricsAtBootOnBattery) {
  ExpectEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
                       kBatteryRemainingAtBootName, PowerSource::BATTERY),
                   power_status_.battery_percentage, kMaxPercent);
  first_run_after_boot_ = true;
  Init();
}

TEST_F(MetricsCollectorTest, TestBatteryMetricsAtBootOnAC) {
  power_status_.line_power_on = true;
  ExpectEnumMetric(MetricsCollector::AppendPowerSourceToEnumName(
                       kBatteryRemainingAtBootName, PowerSource::AC),
                   power_status_.battery_percentage, kMaxPercent);
  first_run_after_boot_ = true;
  Init();
}

// Base class for S0ix residency rate related tests.
class S0ixResidencyMetricsTest : public MetricsCollectorTest {
 public:
  S0ixResidencyMetricsTest() = default;

 protected:
  enum class ResidencyFileType {
    BIG_CORE,
    SMALL_CORE,
    NONE,
  };

  // Creates file of type |residency_file_type| (if needed) root within
  // |temp_root_dir_|. Also sets |kSuspendToIdlePref| pref to
  // |suspend_to_idle| and initializes |collector_|.
  void Init(ResidencyFileType residency_file_type,
            bool suspend_to_idle = true) {
    if (suspend_to_idle)
      prefs_.SetInt64(kSuspendToIdlePref, 1);

    if (residency_file_type == ResidencyFileType::BIG_CORE) {
      residency_path_ =
          GetPath(base::FilePath(MetricsCollector::kBigCoreS0ixResidencyPath));
    } else if (residency_file_type == ResidencyFileType::SMALL_CORE) {
      residency_path_ = GetPath(
          base::FilePath(MetricsCollector::kSmallCoreS0ixResidencyPath));
    }

    if (!residency_path_.empty()) {
      // Create all required parent directories.
      CHECK(base::CreateDirectory(residency_path_.DirName()));
      // Create empty file.
      CHECK_EQ(base::WriteFile(residency_path_, "", 0), 0);
    }

    MetricsCollectorTest::Init();
  }

  // Does suspend and resume. Also writes residency to |residency_path_| (if not
  // empty) before and after suspend.
  void SuspendAndResume() {
    if (!residency_path_.empty())
      WriteResidency(residency_before_suspend_);

    collector_.PrepareForSuspend();
    AdvanceTime(suspend_duration_);
    // Adds expectations to ignore |kSuspendAttemptsBeforeSuccessName| metric
    // sent by HandleResume().
    IgnoreMetric(kSuspendAttemptsBeforeSuccessName);

    if (!residency_path_.empty())
      WriteResidency(residency_before_resume_);

    collector_.HandleResume(1);
  }

  // Expect |kS0ixResidencyRateName| enum metric will be generated.
  void ExpectResidencyRateMetricCall() {
    int expected_s0ix_percentage =
        MetricsCollector::GetExpectedS0ixResidencyPercent(
            suspend_duration_,
            residency_before_resume_ - residency_before_suspend_);
    ExpectEnumMetric(kS0ixResidencyRateName, expected_s0ix_percentage,
                     kMaxPercent);
  }

  // Writes |residency| to |residency_path_|.
  void WriteResidency(const base::TimeDelta& residency) {
    std::string buf = base::NumberToString(
        static_cast<uint64_t>(llabs(residency.InMicroseconds())));
    ASSERT_EQ(base::WriteFile(residency_path_, buf.data(), buf.size()),
              buf.size());
  }

  base::FilePath residency_path_;
  base::TimeDelta residency_before_suspend_ = base::TimeDelta::FromMinutes(50);
  base::TimeDelta residency_before_resume_ = base::TimeDelta::FromMinutes(100);
  base::TimeDelta suspend_duration_ = base::TimeDelta::FromHours(1);
};

// Test S0ix UMA metrics are not reported when residency files do not exist.
TEST_F(S0ixResidencyMetricsTest, S0ixResidencyMetricsNoResidencyFiles) {
  suspend_duration_ = base::TimeDelta::FromHours(1);
  Init(ResidencyFileType::NONE);
  SuspendAndResume();
  // |metrics_lib_| is strict mock. Unexpected method call will fail this test.
  Mock::VerifyAndClearExpectations(metrics_lib_);
}

// Test S0ix UMA metrics are reported when |kSmallCoreS0ixResidencyPath| exist.
TEST_F(S0ixResidencyMetricsTest, SmallCorePathExist) {
  Init(ResidencyFileType::SMALL_CORE);
  ExpectResidencyRateMetricCall();
  SuspendAndResume();
  Mock::VerifyAndClearExpectations(metrics_lib_);
}

// Test S0ix UMA metrics are reported when |kBigCoreS0ixResidencyPath| exist.
TEST_F(S0ixResidencyMetricsTest, BigCorePathExist) {
  Init(ResidencyFileType::BIG_CORE);
  ExpectResidencyRateMetricCall();
  SuspendAndResume();
  Mock::VerifyAndClearExpectations(metrics_lib_);
}

// Test S0ix UMA metrics are not reported when suspend to idle is not enabled.
TEST_F(S0ixResidencyMetricsTest, S0ixResidencyMetricsS0ixNotEnabled) {
  Init(ResidencyFileType::SMALL_CORE, false /*suspend_to_idle*/);
  SuspendAndResume();
  // |metrics_lib_| is strict mock. Unexpected method call will fail this test.
  Mock::VerifyAndClearExpectations(metrics_lib_);
}

// Test metrics are not reported when device suspends less than
// |KS0ixOverheadTime|.
TEST_F(S0ixResidencyMetricsTest, ShortSuspend) {
  suspend_duration_ =
      MetricsCollector::KS0ixOverheadTime - base::TimeDelta::FromSeconds(1);
  Init(ResidencyFileType::SMALL_CORE);
  SuspendAndResume();
  // |metrics_lib_| is strict mock. Unexpected method call will fail this test.
  Mock::VerifyAndClearExpectations(metrics_lib_);
}

// Test metrics are not reported when the residency counter overflows.
TEST_F(S0ixResidencyMetricsTest, ResidencyCounterOverflow) {
  residency_before_resume_ =
      residency_before_suspend_ - base::TimeDelta::FromMinutes(1);
  Init(ResidencyFileType::SMALL_CORE);
  SuspendAndResume();
  // |metrics_lib_| is strict mock. Unexpected method call will fail this test.
  Mock::VerifyAndClearExpectations(metrics_lib_);
}

// Test metrics are not reported when suspend time is more than max residency.
TEST_F(S0ixResidencyMetricsTest, SuspendTimeMoreThanMaxResidency) {
  suspend_duration_ =
      base::TimeDelta::FromMicroseconds(100 * (int64_t)UINT32_MAX + 1);
  Init(ResidencyFileType::BIG_CORE);
  SuspendAndResume();
  // |metrics_lib_| is strict Mock. Unexpected method call will fail this test.
  Mock::VerifyAndClearExpectations(metrics_lib_);
}

}  // namespace metrics
}  // namespace power_manager
