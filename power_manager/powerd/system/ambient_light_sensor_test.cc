// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor.h"

#include <memory>

#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/file_utils.h>
#include <gtest/gtest.h>

#include "power_manager/common/test_main_loop_runner.h"
#include "power_manager/powerd/system/ambient_light_observer.h"

namespace power_manager {
namespace system {

namespace {

// Abort if it an expected brightness change hasn't been received after this
// many milliseconds.
const int kUpdateTimeoutMs = 5000;

// Frequency with which the ambient light sensor file is polled.
const int kPollIntervalMs = 100;

// Simple AmbientLightObserver implementation that runs the event loop
// until it receives notification that the ambient light level has changed.
class TestObserver : public AmbientLightObserver {
 public:
  TestObserver() {}
  TestObserver(const TestObserver&) = delete;
  TestObserver& operator=(const TestObserver&) = delete;

  ~TestObserver() override {}

  // Runs |loop_| until OnAmbientLightUpdated() is called.
  bool RunUntilAmbientLightUpdated() {
    return loop_runner_.StartLoop(
        base::TimeDelta::FromMilliseconds(kUpdateTimeoutMs));
  }

  // AmbientLightObserver implementation:
  void OnAmbientLightUpdated(AmbientLightSensorInterface* sensor) override {
    loop_runner_.StopLoop();
  }

 private:
  TestMainLoopRunner loop_runner_;
};

}  // namespace

class AmbientLightSensorTest : public ::testing::Test {
 public:
  AmbientLightSensorTest() {}
  AmbientLightSensorTest(const AmbientLightSensorTest&) = delete;
  AmbientLightSensorTest& operator=(const AmbientLightSensorTest&) = delete;

  ~AmbientLightSensorTest() override {}

  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());
    device_dir_ = temp_dir_.GetPath().Append("device0");
    CHECK(base::CreateDirectory(device_dir_));
    data_file_ = device_dir_.Append("illuminance0_input");
    sensor_.reset(new AmbientLightSensor);
    sensor_->set_device_list_path_for_testing(temp_dir_.GetPath());
    sensor_->set_poll_interval_ms_for_testing(kPollIntervalMs);
    sensor_->AddObserver(&observer_);
    sensor_->Init(false /* read_immediately */);
  }

  void TearDown() override { sensor_->RemoveObserver(&observer_); }

 protected:
  // Writes |lux| to |data_file_| to simulate the ambient light sensor reporting
  // a new light level.
  void WriteLux(int lux) {
    std::string lux_string = base::NumberToString(lux);
    CHECK(brillo::WriteStringToFile(data_file_, lux_string));
  }

  // Temporary directory mimicking a /sys directory containing a set of sensor
  // devices.
  base::ScopedTempDir temp_dir_;

  base::FilePath device_dir_;

  // Illuminance file containing the sensor's current brightness level.
  base::FilePath data_file_;

  TestObserver observer_;

  std::unique_ptr<AmbientLightSensor> sensor_;
};

TEST_F(AmbientLightSensorTest, Basic) {
  WriteLux(100);
  ASSERT_TRUE(observer_.RunUntilAmbientLightUpdated());
  EXPECT_EQ(100, sensor_->GetAmbientLightLux());

  WriteLux(200);
  ASSERT_TRUE(observer_.RunUntilAmbientLightUpdated());
  EXPECT_EQ(200, sensor_->GetAmbientLightLux());

  // When the lux value doesn't change, we should still be called.
  WriteLux(200);
  ASSERT_TRUE(observer_.RunUntilAmbientLightUpdated());
  EXPECT_EQ(200, sensor_->GetAmbientLightLux());
}

TEST_F(AmbientLightSensorTest, GiveUpAfterTooManyFailures) {
  // Test that the timer is eventually stopped after many failures.
  base::DeleteFile(data_file_, false);
  for (int i = 0; i < AmbientLightSensor::kNumInitAttemptsBeforeGivingUp; ++i) {
    EXPECT_TRUE(sensor_->TriggerPollTimerForTesting());
    EXPECT_LT(sensor_->GetAmbientLightLux(), 0);
  }

  EXPECT_FALSE(sensor_->TriggerPollTimerForTesting());
  EXPECT_LT(sensor_->GetAmbientLightLux(), 0);
}

TEST_F(AmbientLightSensorTest, FailToFindSensorAtLid) {
  // Test that the timer is eventually stopped after many failures if |sensor_|
  // is unable to find the sensor at the expected location.
  sensor_.reset(new AmbientLightSensor(SensorLocation::LID));
  sensor_->set_device_list_path_for_testing(temp_dir_.GetPath());
  sensor_->set_poll_interval_ms_for_testing(kPollIntervalMs);
  sensor_->AddObserver(&observer_);
  sensor_->Init(false /* read_immediately */);

  for (int i = 0; i < AmbientLightSensor::kNumInitAttemptsBeforeGivingUp; ++i) {
    EXPECT_TRUE(sensor_->TriggerPollTimerForTesting());
    EXPECT_LT(sensor_->GetAmbientLightLux(), 0);
  }

  EXPECT_FALSE(sensor_->TriggerPollTimerForTesting());
  EXPECT_LT(sensor_->GetAmbientLightLux(), 0);
}

TEST_F(AmbientLightSensorTest, FindSensorAtBase) {
  // Test that |sensor_| is able to find the correct sensor at the expected
  // location.
  base::FilePath loc_file = device_dir_.Append("location");
  CHECK(brillo::WriteStringToFile(loc_file, "base"));

  sensor_.reset(new AmbientLightSensor(SensorLocation::BASE));
  sensor_->set_device_list_path_for_testing(temp_dir_.GetPath());
  sensor_->set_poll_interval_ms_for_testing(kPollIntervalMs);
  sensor_->AddObserver(&observer_);
  sensor_->Init(false /* read_immediately */);

  WriteLux(100);
  ASSERT_TRUE(observer_.RunUntilAmbientLightUpdated());
  EXPECT_EQ(100, sensor_->GetAmbientLightLux());

  EXPECT_EQ(data_file_, sensor_->GetIlluminancePath());
}

TEST_F(AmbientLightSensorTest, IsColorSensor) {
  // Default sensor does not have color support.
  WriteLux(100);
  ASSERT_TRUE(observer_.RunUntilAmbientLightUpdated());
  EXPECT_FALSE(sensor_->IsColorSensor());

  // Add one color channel.
  base::FilePath color_file = device_dir_.Append("in_illuminance_red_raw");
  CHECK(brillo::WriteStringToFile(color_file, "50"));

  sensor_.reset(new AmbientLightSensor);
  sensor_->set_device_list_path_for_testing(temp_dir_.GetPath());
  sensor_->set_poll_interval_ms_for_testing(kPollIntervalMs);
  sensor_->AddObserver(&observer_);
  sensor_->Init(false /* read_immediately */);

  WriteLux(100);
  ASSERT_TRUE(observer_.RunUntilAmbientLightUpdated());
  // The sensor should still not have color support -- it needs all 3.
  EXPECT_FALSE(sensor_->IsColorSensor());

  // Add the other two channels.
  color_file = device_dir_.Append("in_illuminance_green_raw");
  CHECK(brillo::WriteStringToFile(color_file, "50"));
  color_file = device_dir_.Append("in_illuminance_blue_raw");
  CHECK(brillo::WriteStringToFile(color_file, "50"));

  sensor_.reset(new AmbientLightSensor(true));
  sensor_->set_device_list_path_for_testing(temp_dir_.GetPath());
  sensor_->set_poll_interval_ms_for_testing(kPollIntervalMs);
  sensor_->AddObserver(&observer_);
  sensor_->Init(false /* read_immediately */);

  WriteLux(100);
  ASSERT_TRUE(observer_.RunUntilAmbientLightUpdated());
  // Now we have all channels. The sensor should support color.
  EXPECT_TRUE(sensor_->IsColorSensor());
}

}  // namespace system
}  // namespace power_manager
