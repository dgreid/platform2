// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/power_supply.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_ptr.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/test_main_loop_runner.h"
#include "power_manager/powerd/system/udev_stub.h"
#include "power_manager/proto_bindings/power_supply_properties.pb.h"

namespace power_manager {
namespace system {

namespace {

const char kAcType[] = "Mains";
const char kBatteryType[] = "Battery";
const char kUsbType[] = "USB";

const char kCharging[] = "Charging";
const char kDischarging[] = "Discharging";

// Default voltage reported by sysfs.
const double kVoltage = 2.5;

// Default value for kPowerSupplyFullFactorPref.
const double kFullFactor = 0.98;

// Starting value used by |power_supply_| as "now".
const base::TimeTicks kStartTime = base::TimeTicks::FromInternalValue(1000);

class TestObserver : public PowerSupplyObserver {
 public:
  TestObserver() {}
  virtual ~TestObserver() {}

  // Runs the event loop until OnPowerStatusUpdate() is invoked or a timeout is
  // hit. Returns true if the method was invoked and false if it wasn't.
  bool WaitForNotification() {
    return runner_.StartLoop(base::TimeDelta::FromSeconds(10));
  }

  // PowerSupplyObserver overrides:
  void OnPowerStatusUpdate() override {
    runner_.StopLoop();
  }

 private:
  TestMainLoopRunner runner_;

  DISALLOW_COPY_AND_ASSIGN(TestObserver);
};

}  // namespace

class PowerSupplyTest : public ::testing::Test {
 public:
  PowerSupplyTest() {}

  virtual void SetUp() {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(temp_dir_.IsValid());

    prefs_.SetInt64(kLowBatteryShutdownTimePref, 180);
    prefs_.SetDouble(kPowerSupplyFullFactorPref, kFullFactor);
    prefs_.SetInt64(kMaxCurrentSamplesPref, 5);
    prefs_.SetInt64(kMaxChargeSamplesPref, 5);

    power_supply_.reset(new PowerSupply);
    test_api_.reset(new PowerSupply::TestApi(power_supply_.get()));
    test_api_->SetCurrentTime(kStartTime);

    ac_dir_ = temp_dir_.path().Append("ac");
    battery_dir_ = temp_dir_.path().Append("battery");
  }

 protected:
  // Initializes |power_supply_|.
  void Init() {
    power_supply_->Init(temp_dir_.path(), &prefs_, &udev_);
  }

  // Sets the time so that |power_supply_| will believe that the current
  // has stabilized.
  void SetStabilizedTime() {
    const base::TimeTicks now = test_api_->GetCurrentTime();
    if (power_supply_->battery_stabilized_timestamp() > now)
      test_api_->SetCurrentTime(power_supply_->battery_stabilized_timestamp());
  }

  // Writes |value| to |filename| within |dir_|.
  void WriteValue(const base::FilePath& dir,
                  const std::string& filename,
                  const std::string& value) {
    CHECK(base::WriteFile(dir.Append(filename), value.c_str(), value.length()));
  }

  // Converts |value| to the format used by sysfs and passes it to WriteValue().
  void WriteDoubleValue(const base::FilePath& dir,
                        const std::string& filename,
                        double value) {
    // sysfs stores doubles by multiplying them by 1000000.
    const int int_value = round(value * 1000000);
    WriteValue(dir, filename, base::IntToString(int_value));
  }

  // Writes reasonable default values to |temp_dir_|.
  // The battery's max charge is initialized to 1.0 to make things simple.
  void WriteDefaultValues(PowerSource source) {
    base::CreateDirectory(ac_dir_);
    base::CreateDirectory(battery_dir_);

    UpdatePowerSourceAndBatteryStatus(source, kAcType,
        source == POWER_AC ? kCharging : kDischarging);
    WriteValue(battery_dir_, "type", kBatteryType);
    WriteValue(battery_dir_, "present", "1");

    UpdateChargeAndCurrent(1.0, 0.0);
    WriteDoubleValue(battery_dir_, "charge_full", 1.0);
    WriteDoubleValue(battery_dir_, "charge_full_design", 1.0);
    WriteDoubleValue(battery_dir_, "voltage_now", kVoltage);
    WriteDoubleValue(battery_dir_, "voltage_min_design", kVoltage);
    WriteValue(battery_dir_, "cycle_count", base::IntToString(10000));
  }

  // Updates the files describing the power source and battery status.
  void UpdatePowerSourceAndBatteryStatus(PowerSource power_source,
                                         const std::string& ac_type,
                                         const std::string& battery_status) {
    WriteValue(ac_dir_, "online", power_source == POWER_AC ? "1" : "0");
    WriteValue(ac_dir_, "type", ac_type);
    WriteValue(battery_dir_, "status", battery_status);
  }

  // Updates the files describing the battery's charge and current.
  void UpdateChargeAndCurrent(double charge, double current) {
    WriteDoubleValue(battery_dir_, "charge_now", charge);
    WriteDoubleValue(battery_dir_, "current_now", current);
  }

  // Returns a string describing battery estimates. If |time_to_empty_sec| is
  // nonzero, the appropriate time-to-shutdown estimate will be calculated
  // based on kLowBatteryShutdownTimePref.
  std::string MakeEstimateString(bool calculating,
                                 int time_to_empty_sec,
                                 int time_to_full_sec) {
    int time_to_shutdown_sec = time_to_empty_sec;
    int64 shutdown_sec = 0;
    if (time_to_empty_sec > 0 &&
        prefs_.GetInt64(kLowBatteryShutdownTimePref, &shutdown_sec)) {
      time_to_shutdown_sec =
          std::max(time_to_empty_sec - static_cast<int>(shutdown_sec), 0);
    }
    return base::StringPrintf(
        "calculating=%d empty=%d shutdown=%d full=%d", calculating,
        time_to_empty_sec, time_to_shutdown_sec, time_to_full_sec);
  }

  // Call UpdateStatus() and return a string describing the returned battery
  // estimates, suitable for comparison with a string built via
  // MakeEstimateString().
  std::string UpdateAndGetEstimateString() {
    PowerStatus status;
    if (!UpdateStatus(&status))
      return std::string();
    return base::StringPrintf(
        "calculating=%d empty=%d shutdown=%d full=%d",
        status.is_calculating_battery_time,
        static_cast<int>(status.battery_time_to_empty.InSeconds()),
        static_cast<int>(status.battery_time_to_shutdown.InSeconds()),
        static_cast<int>(status.battery_time_to_full.InSeconds()));
  }

  // Refreshes and updates |status|.  Returns false if the refresh failed.
  bool UpdateStatus(PowerStatus* status) WARN_UNUSED_RESULT {
    CHECK(status);
    if (!power_supply_->RefreshImmediately())
      return false;

    *status = power_supply_->GetPowerStatus();
    return true;
  }

  FakePrefs prefs_;
  base::ScopedTempDir temp_dir_;
  base::FilePath ac_dir_;
  base::FilePath battery_dir_;
  UdevStub udev_;
  scoped_ptr<PowerSupply> power_supply_;
  scoped_ptr<PowerSupply::TestApi> test_api_;
};

// Test system without power supply sysfs (e.g. virtual machine).
TEST_F(PowerSupplyTest, TestNoPowerSupplySysfs) {
  Init();
  PowerStatus power_status;
  ASSERT_TRUE(UpdateStatus(&power_status));
  // In absence of power supply sysfs, default assumption is line power on, no
  // battery present.
  EXPECT_TRUE(power_status.line_power_on);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_AC,
            power_status.external_power);
  EXPECT_FALSE(power_status.battery_is_present);
  EXPECT_EQ(PowerSupplyProperties_BatteryState_NOT_PRESENT,
            power_status.battery_state);
}

// Test line power without battery.
TEST_F(PowerSupplyTest, TestNoBattery) {
  WriteDefaultValues(POWER_AC);
  base::DeleteFile(battery_dir_, true);
  Init();
  PowerStatus power_status;
  ASSERT_TRUE(UpdateStatus(&power_status));
  EXPECT_TRUE(power_status.line_power_on);
  EXPECT_EQ(kAcType, power_status.line_power_type);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_AC,
            power_status.external_power);
  EXPECT_FALSE(power_status.battery_is_present);
  EXPECT_EQ(PowerSupplyProperties_BatteryState_NOT_PRESENT,
            power_status.battery_state);
}

// Test battery charging and discharging status.
TEST_F(PowerSupplyTest, TestChargingAndDischarging) {
  const double kCharge = 0.5;
  const double kCurrent = 1.0;
  WriteDefaultValues(POWER_AC);
  UpdateChargeAndCurrent(kCharge, kCurrent);
  Init();
  PowerStatus power_status;
  ASSERT_TRUE(UpdateStatus(&power_status));
  EXPECT_TRUE(power_status.line_power_on);
  EXPECT_EQ(kAcType, power_status.line_power_type);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_AC,
            power_status.external_power);
  EXPECT_TRUE(power_status.battery_is_present);
  EXPECT_EQ(PowerSupplyProperties_BatteryState_CHARGING,
            power_status.battery_state);
  EXPECT_DOUBLE_EQ(kCharge * kVoltage, power_status.battery_energy);
  EXPECT_DOUBLE_EQ(kCurrent * kVoltage, power_status.battery_energy_rate);
  EXPECT_DOUBLE_EQ(50.0, power_status.battery_percentage);

  // Switch to battery.
  UpdatePowerSourceAndBatteryStatus(POWER_BATTERY, kAcType, kDischarging);
  ASSERT_TRUE(UpdateStatus(&power_status));
  EXPECT_FALSE(power_status.line_power_on);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_DISCONNECTED,
            power_status.external_power);
  EXPECT_TRUE(power_status.battery_is_present);
  EXPECT_EQ(PowerSupplyProperties_BatteryState_DISCHARGING,
            power_status.battery_state);
  EXPECT_DOUBLE_EQ(kCharge * kVoltage, power_status.battery_energy);
  EXPECT_DOUBLE_EQ(kCurrent * kVoltage, power_status.battery_energy_rate);
  EXPECT_DOUBLE_EQ(50.0, power_status.battery_percentage);

  // Test with a negative current.
  UpdateChargeAndCurrent(kCharge, -kCurrent);
  ASSERT_TRUE(UpdateStatus(&power_status));
  EXPECT_EQ(PowerSupplyProperties_BatteryState_DISCHARGING,
            power_status.battery_state);
  EXPECT_DOUBLE_EQ(kCharge * kVoltage, power_status.battery_energy);
  EXPECT_DOUBLE_EQ(kCurrent * kVoltage, power_status.battery_energy_rate);
}

// Tests that the line power source doesn't need to be named "Mains".
TEST_F(PowerSupplyTest, TestNonMainsLinePower) {
  const char kType[] = "ArbitraryName";
  WriteDefaultValues(POWER_AC);
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kType, kCharging);
  Init();
  PowerStatus power_status;
  ASSERT_TRUE(UpdateStatus(&power_status));
  EXPECT_TRUE(power_status.line_power_on);
  EXPECT_EQ(kType, power_status.line_power_type);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_AC,
            power_status.external_power);
  EXPECT_TRUE(power_status.battery_is_present);
}

// Test battery reporting energy instead of charge.
TEST_F(PowerSupplyTest, TestEnergyDischarging) {
  WriteDefaultValues(POWER_BATTERY);
  base::DeleteFile(battery_dir_.Append("charge_full"), false);
  base::DeleteFile(battery_dir_.Append("charge_full_design"), false);
  base::DeleteFile(battery_dir_.Append("charge_now"), false);
  base::DeleteFile(battery_dir_.Append("current_now"), false);

  const double kChargeFull = 2.40;
  const double kChargeNow = 1.80;
  const double kCurrentNow = 0.20;
  const double kEnergyFull = kChargeFull * kVoltage;
  const double kEnergyNow = kChargeNow * kVoltage;
  const double kPowerNow = kCurrentNow * kVoltage;
  const double kEnergyRate = kCurrentNow * kVoltage;
  const double kPercentage = 100.0 * kChargeNow / kChargeFull;
  WriteDoubleValue(battery_dir_, "energy_full", kEnergyFull);
  WriteDoubleValue(battery_dir_, "energy_full_design", kEnergyFull);
  WriteDoubleValue(battery_dir_, "energy_now", kEnergyNow);
  WriteDoubleValue(battery_dir_, "power_now", kPowerNow);

  Init();
  PowerStatus power_status;
  ASSERT_TRUE(UpdateStatus(&power_status));
  EXPECT_FALSE(power_status.line_power_on);
  EXPECT_TRUE(power_status.battery_is_present);
  EXPECT_EQ(PowerSupplyProperties_BatteryState_DISCHARGING,
            power_status.battery_state);
  EXPECT_DOUBLE_EQ(kEnergyNow, power_status.battery_energy);
  EXPECT_DOUBLE_EQ(kEnergyRate, power_status.battery_energy_rate);
  EXPECT_DOUBLE_EQ(kPercentage, power_status.battery_percentage);

  // Charge values should be computed.
  EXPECT_DOUBLE_EQ(kChargeFull, power_status.battery_charge_full);
  EXPECT_DOUBLE_EQ(kChargeFull, power_status.battery_charge_full_design);
  EXPECT_DOUBLE_EQ(kChargeNow, power_status.battery_charge);
  EXPECT_DOUBLE_EQ(kCurrentNow, power_status.battery_current);

  WriteDoubleValue(battery_dir_, "power_now", -kPowerNow);
  ASSERT_TRUE(UpdateStatus(&power_status));
  EXPECT_EQ(PowerSupplyProperties_BatteryState_DISCHARGING,
            power_status.battery_state);
  EXPECT_DOUBLE_EQ(kEnergyNow, power_status.battery_energy);
  EXPECT_DOUBLE_EQ(kEnergyRate, power_status.battery_energy_rate);
  EXPECT_DOUBLE_EQ(kPercentage, power_status.battery_percentage);
}

TEST_F(PowerSupplyTest, PollDelays) {
  WriteDefaultValues(POWER_AC);

  const base::TimeDelta kPollDelay = base::TimeDelta::FromSeconds(30);
  const base::TimeDelta kStartupDelay = base::TimeDelta::FromSeconds(6);
  const base::TimeDelta kACDelay = base::TimeDelta::FromSeconds(7);
  const base::TimeDelta kBatteryDelay = base::TimeDelta::FromSeconds(8);
  const base::TimeDelta kResumeDelay = base::TimeDelta::FromSeconds(10);
  const base::TimeDelta kSlack = base::TimeDelta::FromMilliseconds(
      PowerSupply::kBatteryStabilizedSlackMs);

  prefs_.SetInt64(kBatteryPollIntervalPref, kPollDelay.InMilliseconds());
  prefs_.SetInt64(kBatteryStabilizedAfterStartupMsPref,
                  kStartupDelay.InMilliseconds());
  prefs_.SetInt64(kBatteryStabilizedAfterLinePowerConnectedMsPref,
                  kACDelay.InMilliseconds());
  prefs_.SetInt64(kBatteryStabilizedAfterLinePowerDisconnectedMsPref,
                  kBatteryDelay.InMilliseconds());
  prefs_.SetInt64(kBatteryStabilizedAfterResumeMsPref,
                  kResumeDelay.InMilliseconds());

  base::TimeTicks current_time = kStartTime;
  Init();

  // The battery times should be reported as "calculating" just after
  // initialization.
  PowerStatus status;
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_TRUE(status.line_power_on);
  EXPECT_TRUE(status.is_calculating_battery_time);
  EXPECT_EQ((kStartupDelay + kSlack).InMilliseconds(),
            test_api_->current_poll_delay().InMilliseconds());

  // After enough time has elapsed, the battery times should be reported.
  current_time += kStartupDelay + kSlack;
  test_api_->SetCurrentTime(current_time);
  ASSERT_TRUE(test_api_->TriggerPollTimeout());
  status = power_supply_->GetPowerStatus();
  EXPECT_TRUE(status.line_power_on);
  EXPECT_FALSE(status.is_calculating_battery_time);
  EXPECT_EQ(kPollDelay.InMilliseconds(),
            test_api_->current_poll_delay().InMilliseconds());

  // Polling should stop when the system is about to suspend.
  power_supply_->SetSuspended(true);
  EXPECT_EQ(0, test_api_->current_poll_delay().InMilliseconds());

  // After resuming, the status should be updated immediately and the
  // battery times should be reported as "calculating" again.
  current_time += base::TimeDelta::FromSeconds(120);
  test_api_->SetCurrentTime(current_time);
  UpdatePowerSourceAndBatteryStatus(POWER_BATTERY, kAcType, kDischarging);
  power_supply_->SetSuspended(false);
  status = power_supply_->GetPowerStatus();
  EXPECT_FALSE(status.line_power_on);
  EXPECT_TRUE(status.is_calculating_battery_time);
  EXPECT_EQ((kResumeDelay + kSlack).InMilliseconds(),
            test_api_->current_poll_delay().InMilliseconds());

  // Check that the updated times are returned after a delay.
  current_time += kResumeDelay + kSlack;
  test_api_->SetCurrentTime(current_time);
  ASSERT_TRUE(test_api_->TriggerPollTimeout());
  status = power_supply_->GetPowerStatus();
  EXPECT_FALSE(status.line_power_on);
  EXPECT_FALSE(status.is_calculating_battery_time);

  // Connect AC, report a udev event, and check that the status is updated.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kAcType, kCharging);
  power_supply_->OnUdevEvent(
      PowerSupply::kUdevSubsystem, "AC", UDEV_ACTION_CHANGE);
  status = power_supply_->GetPowerStatus();
  EXPECT_TRUE(status.line_power_on);
  EXPECT_TRUE(status.is_calculating_battery_time);
  EXPECT_EQ((kACDelay + kSlack).InMilliseconds(),
            test_api_->current_poll_delay().InMilliseconds());

  // After the delay, estimates should be made again.
  current_time += kACDelay + kSlack;
  test_api_->SetCurrentTime(current_time);
  ASSERT_TRUE(test_api_->TriggerPollTimeout());
  status = power_supply_->GetPowerStatus();
  EXPECT_TRUE(status.line_power_on);
  EXPECT_FALSE(status.is_calculating_battery_time);

  // Now test the delay when going back to battery power.
  UpdatePowerSourceAndBatteryStatus(POWER_BATTERY, kAcType, kDischarging);
  power_supply_->OnUdevEvent(
      PowerSupply::kUdevSubsystem, "AC", UDEV_ACTION_CHANGE);
  status = power_supply_->GetPowerStatus();
  EXPECT_FALSE(status.line_power_on);
  EXPECT_TRUE(status.is_calculating_battery_time);
  EXPECT_EQ((kBatteryDelay + kSlack).InMilliseconds(),
            test_api_->current_poll_delay().InMilliseconds());

  // After the delay, estimates should be made again.
  current_time += kBatteryDelay + kSlack;
  test_api_->SetCurrentTime(current_time);
  ASSERT_TRUE(test_api_->TriggerPollTimeout());
  status = power_supply_->GetPowerStatus();
  EXPECT_FALSE(status.line_power_on);
  EXPECT_FALSE(status.is_calculating_battery_time);
}

TEST_F(PowerSupplyTest, UpdateBatteryTimeEstimates) {
  // Start out with the battery 50% full and an unset current.
  WriteDefaultValues(POWER_AC);
  UpdateChargeAndCurrent(0.5, 0.0);
  prefs_.SetDouble(kPowerSupplyFullFactorPref, 1.0);
  // To simplify this test, average just the last two samples.
  prefs_.SetInt64(kMaxCurrentSamplesPref, 2);
  Init();

  EXPECT_EQ(MakeEstimateString(true, 0, 0), UpdateAndGetEstimateString());

  // Set the current such that it'll take an hour to charge fully and
  // advance the clock so the current will be used.
  UpdateChargeAndCurrent(0.5, 0.5);
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, 0, 3600), UpdateAndGetEstimateString());

  // Let half an hour pass and report that the battery is 75% full.
  test_api_->AdvanceTime(base::TimeDelta::FromMinutes(30));
  UpdateChargeAndCurrent(0.75, 0.5);
  EXPECT_EQ(MakeEstimateString(false, 0, 1800), UpdateAndGetEstimateString());

  // After a current reading of 1.0, the averaged current should be (0.5 + 1.0)
  // / 2 = 0.75. The remaining 0.25 of charge to get to 100% should take twenty
  // minutes.
  UpdateChargeAndCurrent(0.75, 1.0);
  EXPECT_EQ(MakeEstimateString(false, 0, 1200), UpdateAndGetEstimateString());

  // Fifteen minutes later, set the current to 0.25 (giving an average of (1.0 +
  // 0.25) / 2 = 0.625) and report an increased charge. There should be 0.125 /
  // 0.625 * 3600 = 720 seconds until the battery is full.
  test_api_->AdvanceTime(base::TimeDelta::FromMinutes(15));
  UpdateChargeAndCurrent(0.875, 0.25);
  EXPECT_EQ(MakeEstimateString(false, 0, 720), UpdateAndGetEstimateString());

  // Disconnect the charger and report an immediate drop in charge and
  // current. The current shouldn't be used yet.
  UpdatePowerSourceAndBatteryStatus(POWER_BATTERY, kAcType, kDischarging);
  UpdateChargeAndCurrent(0.5, -0.5);
  EXPECT_EQ(MakeEstimateString(true, 0, 0), UpdateAndGetEstimateString());

  // After the current has had time to stabilize, the average should be
  // reset and the time-to-empty should be estimated.
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, 3600, 0), UpdateAndGetEstimateString());

  // Thirty minutes later, decrease the charge and report a significantly
  // higher current.
  test_api_->AdvanceTime(base::TimeDelta::FromMinutes(30));
  UpdateChargeAndCurrent(0.25, -1.5);
  EXPECT_EQ(MakeEstimateString(false, 900, 0), UpdateAndGetEstimateString());

  // A current report of 0 should be ignored.
  UpdateChargeAndCurrent(0.25, 0.0);
  EXPECT_EQ(MakeEstimateString(false, 900, 0), UpdateAndGetEstimateString());

  // Suspend, change the current, and resume. The battery time should be
  // reported as "calculating".
  power_supply_->SetSuspended(true);
  UpdateChargeAndCurrent(0.25, -2.5);
  test_api_->AdvanceTime(base::TimeDelta::FromSeconds(8));
  power_supply_->SetSuspended(false);
  EXPECT_EQ(MakeEstimateString(true, 0, 0), UpdateAndGetEstimateString());

  // Wait for the current to stabilize. The last valid sample (-1.5) should be
  // averaged with the latest one.
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, 450, 0), UpdateAndGetEstimateString());

  // Switch back to line power. Since the current delivered on line power can
  // vary greatly, the previous sample should be discarded.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kAcType, kCharging);
  UpdateChargeAndCurrent(0.5, 0.25);
  EXPECT_EQ(MakeEstimateString(true, 0, 0), UpdateAndGetEstimateString());
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, 0, 7200), UpdateAndGetEstimateString());

  // Go back to battery and check that the previous on-battery current sample
  // (-2.5) is included in the average.
  UpdatePowerSourceAndBatteryStatus(POWER_BATTERY, kAcType, kDischarging);
  UpdateChargeAndCurrent(0.5, -1.5);
  EXPECT_EQ(MakeEstimateString(true, 0, 0), UpdateAndGetEstimateString());
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, 900, 0), UpdateAndGetEstimateString());
}

TEST_F(PowerSupplyTest, UsbBatteryTimeEstimates) {
  WriteDefaultValues(POWER_AC);
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kUsbType, kCharging);
  UpdateChargeAndCurrent(0.5, 1.0);
  prefs_.SetDouble(kPowerSupplyFullFactorPref, 1.0);
  prefs_.SetInt64(kMaxCurrentSamplesPref, 2);
  Init();

  // Start out charging on USB power.
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, 0, 1800), UpdateAndGetEstimateString());

  // Now discharge while still on USB. Since the averaged charge is still
  // positive, we should avoid providing a time-to-empty estimate.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kUsbType, kDischarging);
  UpdateChargeAndCurrent(0.5, -0.5);
  EXPECT_EQ(MakeEstimateString(false, -1, 0), UpdateAndGetEstimateString());

  // After another sample brings the average current to -1.0,
  // time-to-empty/shutdown should be calculated.
  UpdateChargeAndCurrent(0.5, -1.5);
  EXPECT_EQ(MakeEstimateString(false, 1800, 0), UpdateAndGetEstimateString());

  // Now start charging. Since the average current is still negative, we should
  // avoid computing time-to-full.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kUsbType, kCharging);
  UpdateChargeAndCurrent(0.5, 0.5);
  EXPECT_EQ(MakeEstimateString(false, 0, -1), UpdateAndGetEstimateString());

  // Switch to battery power.
  UpdatePowerSourceAndBatteryStatus(POWER_BATTERY, kAcType, kDischarging);
  UpdateChargeAndCurrent(0.5, -1.0);
  EXPECT_EQ(MakeEstimateString(true, 0, 0), UpdateAndGetEstimateString());
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, 1800, 0), UpdateAndGetEstimateString());

  // Go back to USB.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kAcType, kCharging);
  UpdateChargeAndCurrent(0.5, 1.0);
  EXPECT_EQ(MakeEstimateString(true, 0, 0), UpdateAndGetEstimateString());

  // Since different USB chargers can provide different current, the previous
  // on-line-power average should be thrown out.
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, 0, 1800), UpdateAndGetEstimateString());
}

TEST_F(PowerSupplyTest, BatteryTimeEstimatesWithZeroCurrent) {
  WriteDefaultValues(POWER_AC);
  UpdateChargeAndCurrent(0.5, 0.1 * kEpsilon);
  Init();

  // When the only available current readings are close to 0 (which would
  // result in very large time estimates), -1 estimates should be provided
  // instead.
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, 0, -1), UpdateAndGetEstimateString());

  UpdatePowerSourceAndBatteryStatus(POWER_BATTERY, kAcType, kDischarging);
  EXPECT_EQ(MakeEstimateString(true, 0, 0), UpdateAndGetEstimateString());
  SetStabilizedTime();
  EXPECT_EQ(MakeEstimateString(false, -1, 0), UpdateAndGetEstimateString());
}

TEST_F(PowerSupplyTest, FullFactor) {
  // When the battery has reached the full factor, it should be reported as
  // fully charged regardless of the current.
  WriteDefaultValues(POWER_AC);
  UpdateChargeAndCurrent(kFullFactor, 1.0);
  Init();
  PowerStatus status;
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(PowerSupplyProperties_BatteryState_FULL, status.battery_state);
  EXPECT_DOUBLE_EQ(100.0, status.display_battery_percentage);

  // It should stay full when the current goes to zero.
  UpdateChargeAndCurrent(kFullFactor, 0.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(PowerSupplyProperties_BatteryState_FULL, status.battery_state);
  EXPECT_DOUBLE_EQ(100.0, status.display_battery_percentage);
}

TEST_F(PowerSupplyTest, DisplayBatteryPercent) {
  static const double kShutdownPercent = 5.0;
  prefs_.SetDouble(kLowBatteryShutdownPercentPref, kShutdownPercent);

  // Start out with a full battery on AC power.
  WriteDefaultValues(POWER_AC);
  UpdateChargeAndCurrent(1.0, 0.0);
  Init();

  // 100% should be reported both on AC and battery power.
  PowerStatus status;
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(100.0, status.display_battery_percentage);
  UpdatePowerSourceAndBatteryStatus(POWER_BATTERY, kAcType, kDischarging);
  UpdateChargeAndCurrent(1.0, -1.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(100.0, status.display_battery_percentage);

  // Decrease the battery charge, but keep it above the full-factor-derived
  // "full" threshold. Batteries sometimes report a lower charge as soon
  // as line power has been disconnected.
  const double kFullCharge = kFullFactor;
  UpdateChargeAndCurrent(kFullCharge, 0.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(100.0, status.display_battery_percentage);

  // Lower charges should be scaled.
  const double kLowerCharge = 0.92;
  UpdateChargeAndCurrent(kLowerCharge, 0.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(100.0 * (100.0 * kLowerCharge - kShutdownPercent) /
                   (100.0 * kFullFactor - kShutdownPercent),
                   status.display_battery_percentage);

  // Switch to AC and check that the scaling remains the same.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kAcType, kCharging);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(100.0 * (100.0 * kLowerCharge - kShutdownPercent) /
                   (100.0 * kFullFactor - kShutdownPercent),
                   status.display_battery_percentage);

  UpdateChargeAndCurrent(0.85, 0.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(100 * (85.0 - kShutdownPercent) /
                   (100.0 * kFullFactor - kShutdownPercent),
                   status.display_battery_percentage);

  UpdateChargeAndCurrent(kShutdownPercent / 100.0, 0.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(0.0, status.display_battery_percentage);

  UpdateChargeAndCurrent(0.0, 0.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(0.0, status.display_battery_percentage);

  UpdateChargeAndCurrent(-0.1, 0.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(0.0, status.display_battery_percentage);
}

TEST_F(PowerSupplyTest, CheckForLowBattery) {
  const double kShutdownPercent = 5.0;
  const double kCurrent = -1.0;
  prefs_.SetDouble(kLowBatteryShutdownPercentPref, kShutdownPercent);

  WriteDefaultValues(POWER_BATTERY);
  UpdateChargeAndCurrent((kShutdownPercent + 1.0) / 100.0, kCurrent);
  Init();

  PowerStatus status;
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_FALSE(status.battery_below_shutdown_threshold);

  UpdateChargeAndCurrent((kShutdownPercent - 1.0) / 100.0, kCurrent);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_TRUE(status.battery_below_shutdown_threshold);

  // If the charge is zero, assume that something is being misreported and
  // avoid shutting down.
  UpdateChargeAndCurrent(0.0, kCurrent);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_FALSE(status.battery_below_shutdown_threshold);

  // Don't shut down when on AC power when the battery's charge isn't observed
  // to be decreasing.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kAcType, kDischarging);
  UpdateChargeAndCurrent((kShutdownPercent - 1.0) / 100.0, kCurrent);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_FALSE(status.battery_below_shutdown_threshold);

  // Don't shut down for other chargers in this situation, either.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kUsbType, kDischarging);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_FALSE(status.battery_below_shutdown_threshold);

  // Test that the system shuts down while on AC power if the charge appears to
  // be falling (i.e. the charger isn't able to deliver enough current).
  SetStabilizedTime();
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kAcType, kDischarging);
  UpdateChargeAndCurrent((kShutdownPercent - 1.0) / 100.0, kCurrent);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_FALSE(status.battery_below_shutdown_threshold);

  // After just half of the observation period has elapsed, the system should
  // still be up.
  const base::TimeDelta kObservationTime = base::TimeDelta::FromMilliseconds(
      PowerSupply::kObservedBatteryChargeRateMinMs);
  UpdateChargeAndCurrent((kShutdownPercent - 1.5) / 100.0, kCurrent);
  test_api_->AdvanceTime(kObservationTime / 2);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_FALSE(status.battery_below_shutdown_threshold);

  // If the charge is still trending downward after the full observation period
  // has elapsed, the system should shut down.
  UpdateChargeAndCurrent((kShutdownPercent - 2.0) / 100.0, kCurrent);
  test_api_->AdvanceTime(kObservationTime / 2);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_TRUE(status.battery_below_shutdown_threshold);
}

TEST_F(PowerSupplyTest, LowPowerCharger) {
  // If a charger is connected but the current is zero and the battery
  // isn't full, the battery should be reported as discharging.
  WriteDefaultValues(POWER_AC);
  UpdateChargeAndCurrent(0.5, 0.0);
  Init();
  PowerStatus status;
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_AC,
            status.external_power);
  EXPECT_EQ(PowerSupplyProperties_BatteryState_DISCHARGING,
            status.battery_state);

  // If the current is nonzero but the kernel-reported status is
  // "Discharging", the battery should be reported as discharging.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kAcType, kDischarging);
  UpdateChargeAndCurrent(0.5, 1.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_AC,
            status.external_power);
  EXPECT_EQ(PowerSupplyProperties_BatteryState_DISCHARGING,
            status.battery_state);
}

TEST_F(PowerSupplyTest, ConnectedToUsb) {
  WriteDefaultValues(POWER_AC);
  UpdateChargeAndCurrent(0.5, 1.0);
  Init();

  // Check that the "connected to USB" status is reported for all
  // USB-related strings used by the kernel.
  PowerStatus status;
  const char* kUsbTypes[] = { "USB", "USB_DCP", "USB_CDP", "USB_ACA" };
  for (size_t i = 0; i < arraysize(kUsbTypes); ++i) {
    const char* kType = kUsbTypes[i];
    SCOPED_TRACE(kType);
    UpdatePowerSourceAndBatteryStatus(POWER_AC, kType, kCharging);
    ASSERT_TRUE(UpdateStatus(&status));
    EXPECT_EQ(PowerSupplyProperties_BatteryState_CHARGING,
              status.battery_state);
    EXPECT_EQ(PowerSupplyProperties_ExternalPower_USB, status.external_power);
  }

  // The USB type should be reported even when the current is 0.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kUsbType, kCharging);
  UpdateChargeAndCurrent(0.5, 0.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(PowerSupplyProperties_BatteryState_DISCHARGING,
            status.battery_state);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_USB,
            status.external_power);
}

TEST_F(PowerSupplyTest, OriginalSpringCharger) {
  const char kModelNameFile[] = "model_name";
  WriteDefaultValues(POWER_AC);
  Init();

  PowerStatus status;
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ("", status.line_power_model_name);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_AC, status.external_power);

  WriteValue(ac_dir_, kModelNameFile, PowerSupply::kOldFirmwareModelName);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(PowerSupply::kOldFirmwareModelName, status.line_power_model_name);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_ORIGINAL_SPRING_CHARGER,
            status.external_power);

  WriteValue(ac_dir_, kModelNameFile,
             PowerSupply::kOriginalSpringChargerModelName);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(PowerSupply::kOriginalSpringChargerModelName,
            status.line_power_model_name);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_ORIGINAL_SPRING_CHARGER,
            status.external_power);

  const char kBogusModelName[] = "0x1b";
  WriteValue(ac_dir_, kModelNameFile, kBogusModelName);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(kBogusModelName, status.line_power_model_name);
  EXPECT_EQ(PowerSupplyProperties_ExternalPower_AC, status.external_power);
}

TEST_F(PowerSupplyTest, ShutdownPercentAffectsBatteryTime) {
  const double kShutdownPercent = 10.0;
  prefs_.SetDouble(kLowBatteryShutdownPercentPref, kShutdownPercent);
  const double kShutdownSec = 3200;
  prefs_.SetDouble(kLowBatteryShutdownTimePref, kShutdownSec);
  const double kCurrent = -1.0;

  WriteDefaultValues(POWER_BATTERY);
  UpdateChargeAndCurrent(0.5, kCurrent);
  prefs_.SetDouble(kPowerSupplyFullFactorPref, 1.0);
  Init();
  SetStabilizedTime();

  // The reported time until shutdown should be based only on the charge that's
  // available before shutdown. Note also that the time-based shutdown threshold
  // is ignored since a percent-based threshold is set.
  const double kShutdownCharge = kShutdownPercent / 100.0 * 1.0;
  PowerStatus status;
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(1800, status.battery_time_to_empty.InSeconds());
  EXPECT_EQ(roundl((0.5 - kShutdownCharge) * 3600),
            status.battery_time_to_shutdown.InSeconds());
  EXPECT_FALSE(status.battery_below_shutdown_threshold);

  // The reported time should be zero once the threshold is reached.
  UpdateChargeAndCurrent(kShutdownCharge, kCurrent);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(roundl(kShutdownCharge / 1.0 * 3600),
            status.battery_time_to_empty.InSeconds());
  EXPECT_EQ(0, status.battery_time_to_shutdown.InSeconds());
  EXPECT_TRUE(status.battery_below_shutdown_threshold);

  // It should remain zero if the threshold is passed.
  static const double kLowerCharge = kShutdownCharge / 2.0;
  UpdateChargeAndCurrent(kLowerCharge, kCurrent);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(roundl(kLowerCharge / 1.0 * 3600),
            status.battery_time_to_empty.InSeconds());
  EXPECT_EQ(0, status.battery_time_to_shutdown.InSeconds());
  EXPECT_TRUE(status.battery_below_shutdown_threshold);
}

TEST_F(PowerSupplyTest, ObservedBatteryChargeRate) {
  const int kMaxSamples = 5;
  prefs_.SetInt64(kMaxCurrentSamplesPref, kMaxSamples);
  prefs_.SetInt64(kMaxChargeSamplesPref, kMaxSamples);

  WriteDefaultValues(POWER_BATTERY);
  WriteDoubleValue(battery_dir_, "charge_full", 10.0);
  UpdateChargeAndCurrent(10.0, -1.0);
  prefs_.SetDouble(kPowerSupplyFullFactorPref, 1.0);
  Init();
  SetStabilizedTime();

  PowerStatus status;
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(0.0, status.observed_battery_charge_rate);

  // Advance the time, but not by enough to estimate the rate.
  const base::TimeDelta kObservationTime = base::TimeDelta::FromMilliseconds(
      PowerSupply::kObservedBatteryChargeRateMinMs);
  test_api_->AdvanceTime(kObservationTime / 2);
  UpdateChargeAndCurrent(9.0, -1.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(0.0, status.observed_battery_charge_rate);

  // Advance the time by enough so the next reading will be a full hour from the
  // first one, indicating that the charge is dropping by 1 Ah per hour.
  test_api_->AdvanceTime(base::TimeDelta::FromHours(1) - kObservationTime / 2);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(-1.0, status.observed_battery_charge_rate);

  // Decrease the charge by 3 Ah over the next hour.
  test_api_->AdvanceTime(base::TimeDelta::FromHours(1));
  UpdateChargeAndCurrent(6.0, -1.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(-2.0, status.observed_battery_charge_rate);

  // Switch to AC power and report a different charge. The rate should be
  // reported as 0 initially.
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kAcType, kCharging);
  UpdateChargeAndCurrent(7.0, 1.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(0.0, status.observed_battery_charge_rate);

  // Let enough time pass for the battery readings to stabilize.
  SetStabilizedTime();
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(0.0, status.observed_battery_charge_rate);

  // Advance the time just enough for the rate to be calculated and increase the
  // charge by 1 Ah.
  test_api_->AdvanceTime(kObservationTime);
  UpdateChargeAndCurrent(8.0, 1.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(1.0 / (kObservationTime.InSecondsF() / 3600),
                   status.observed_battery_charge_rate);

  // Now advance the time to get a reading one hour from the first one and
  // decrease the charge by 2 Ah from the first reading while on AC power.
  test_api_->AdvanceTime(base::TimeDelta::FromHours(1) - kObservationTime);
  UpdateChargeAndCurrent(5.0, 1.0);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_DOUBLE_EQ(-2.0, status.observed_battery_charge_rate);

  // Send enough identical samples to fill the window and check that the rate is
  // reported as 0.
  for (int i = 0; i < kMaxSamples; ++i) {
    test_api_->AdvanceTime(base::TimeDelta::FromHours(1));
    ASSERT_TRUE(UpdateStatus(&status));
  }
  EXPECT_DOUBLE_EQ(0.0, status.observed_battery_charge_rate);
}

TEST_F(PowerSupplyTest, LowBatteryShutdownSafetyPercent) {
  // Start out discharging on AC with a ludicrously-high current where all of
  // the charge will be drained in a minute.
  const double kCurrent = -60.0;
  WriteDefaultValues(POWER_AC);
  UpdatePowerSourceAndBatteryStatus(POWER_AC, kAcType, kDischarging);
  UpdateChargeAndCurrent(0.5, kCurrent);
  prefs_.SetInt64(kLowBatteryShutdownTimePref, 180);
  prefs_.SetDouble(kPowerSupplyFullFactorPref, 1.0);
  Init();

  // The system shouldn't shut down initially since it's on AC power and a
  // negative charge rate hasn't yet been observed.
  SetStabilizedTime();
  PowerStatus status;
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(30, status.battery_time_to_empty.InSeconds());
  EXPECT_EQ(0, status.battery_time_to_shutdown.InSeconds());
  EXPECT_DOUBLE_EQ(0.0, status.observed_battery_charge_rate);
  EXPECT_FALSE(status.battery_below_shutdown_threshold);

  // Even after a negative charge rate is observed, the system still shouldn't
  // shut down, since the battery percent is greater than the safety percent.
  test_api_->AdvanceTime(base::TimeDelta::FromMilliseconds(
      PowerSupply::kObservedBatteryChargeRateMinMs));
  UpdateChargeAndCurrent(0.25, kCurrent);
  ASSERT_GT(25.0, PowerSupply::kLowBatteryShutdownSafetyPercent);
  ASSERT_TRUE(UpdateStatus(&status));
  EXPECT_EQ(15, status.battery_time_to_empty.InSeconds());
  EXPECT_EQ(0, status.battery_time_to_shutdown.InSeconds());
  EXPECT_LT(status.observed_battery_charge_rate, 0.0);
  EXPECT_FALSE(status.battery_below_shutdown_threshold);
}

TEST_F(PowerSupplyTest, NotifyObserver) {
  // Set a long polling delay to ensure that PowerSupply doesn't poll in the
  // background during the test.
  const base::TimeDelta kDelay = base::TimeDelta::FromSeconds(60);
  prefs_.SetInt64(kBatteryPollIntervalPref, kDelay.InMilliseconds());
  prefs_.SetInt64(kBatteryStabilizedAfterStartupMsPref,
                  kDelay.InMilliseconds());

  // Check that observers are notified about updates asynchronously.
  TestObserver observer;
  power_supply_->AddObserver(&observer);
  Init();
  ASSERT_TRUE(power_supply_->RefreshImmediately());
  EXPECT_TRUE(observer.WaitForNotification());
  power_supply_->RemoveObserver(&observer);
}

TEST_F(PowerSupplyTest, RegisterForUdevEvents) {
  Init();
  EXPECT_TRUE(udev_.HasSubsystemObserver(PowerSupply::kUdevSubsystem,
                                         power_supply_.get()));

  PowerSupply* dead_ptr = power_supply_.get();
  power_supply_.reset();
  EXPECT_FALSE(udev_.HasSubsystemObserver(PowerSupply::kUdevSubsystem,
                                          dead_ptr));
}

}  // namespace system
}  // namespace power_manager
