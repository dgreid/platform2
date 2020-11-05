// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/ambient_light_sensor.h"

#include <memory>
#include <utility>
#include <gtest/gtest.h>

#include "power_manager/powerd/system/ambient_light_observer.h"

namespace power_manager {
namespace system {

namespace {

class TestObserver : public AmbientLightObserver {
 public:
  TestObserver() {}
  TestObserver(const TestObserver&) = delete;
  TestObserver& operator=(const TestObserver&) = delete;
  ~TestObserver() override {}

  bool Updated() {
    bool updated = updated_;
    updated_ = false;
    return updated;
  }

  // AmbientLightObserver implementation:
  void OnAmbientLightUpdated(AmbientLightSensorInterface* sensor) override {
    updated_ = true;
  }

 private:
  bool updated_ = false;
};

class TestDelegate : public AmbientLightSensorDelegate {
 public:
  // AmbientLightSensorDelegate implementation:
  bool IsColorSensor() const override { return is_color_sensor_; }
  base::FilePath GetIlluminancePath() const override {
    return base::FilePath();
  }

  void SetLuxAndColorTemperature(base::Optional<int> lux,
                                 base::Optional<int> color_temperature) {
    if (color_temperature.has_value())
      is_color_sensor_ = true;

    if (!set_lux_callback_)
      return;

    set_lux_callback_.Run(lux, color_temperature);
  }

 private:
  bool is_color_sensor_ = false;
};

}  // namespace

class AmbientLightSensorTest : public ::testing::Test {
 public:
  AmbientLightSensorTest() {}
  AmbientLightSensorTest(const AmbientLightSensorTest&) = delete;
  AmbientLightSensorTest& operator=(const AmbientLightSensorTest&) = delete;

  ~AmbientLightSensorTest() override {}

 protected:
  void SetUp() override {
    sensor_ = std::make_unique<system::AmbientLightSensor>();
    auto delegate = std::make_unique<TestDelegate>();
    delegate_ = delegate.get();
    sensor_->SetDelegate(std::move(delegate));
    sensor_->AddObserver(&observer_);
  }

  void TearDown() override { sensor_->RemoveObserver(&observer_); }

  TestObserver observer_;
  TestDelegate* delegate_;
  std::unique_ptr<AmbientLightSensor> sensor_;
};

TEST_F(AmbientLightSensorTest, IsColorSensor) {
  EXPECT_FALSE(sensor_->IsColorSensor());
  EXPECT_FALSE(observer_.Updated());
}

TEST_F(AmbientLightSensorTest, UpdateWithoutData) {
  delegate_->SetLuxAndColorTemperature(base::nullopt, base::nullopt);
  EXPECT_TRUE(observer_.Updated());

  EXPECT_EQ(-1, sensor_->GetAmbientLightLux());
  EXPECT_EQ(-1, sensor_->GetColorTemperature());
}

TEST_F(AmbientLightSensorTest, UpdateWithLux) {
  delegate_->SetLuxAndColorTemperature(100, base::nullopt);
  EXPECT_TRUE(observer_.Updated());

  EXPECT_EQ(100, sensor_->GetAmbientLightLux());
  EXPECT_EQ(-1, sensor_->GetColorTemperature());

  delegate_->SetLuxAndColorTemperature(base::nullopt, base::nullopt);
  EXPECT_TRUE(observer_.Updated());

  // lux doesn't change.
  EXPECT_EQ(100, sensor_->GetAmbientLightLux());
  EXPECT_EQ(-1, sensor_->GetColorTemperature());
}

TEST_F(AmbientLightSensorTest, UpdateWithColorTemperature) {
  EXPECT_FALSE(sensor_->IsColorSensor());
  delegate_->SetLuxAndColorTemperature(base::nullopt, 200);
  EXPECT_TRUE(sensor_->IsColorSensor());
  EXPECT_TRUE(observer_.Updated());

  EXPECT_EQ(-1, sensor_->GetAmbientLightLux());
  EXPECT_EQ(200, sensor_->GetColorTemperature());

  delegate_->SetLuxAndColorTemperature(base::nullopt, base::nullopt);
  EXPECT_TRUE(observer_.Updated());

  // lux doesn't change.
  EXPECT_EQ(-1, sensor_->GetAmbientLightLux());
  EXPECT_EQ(200, sensor_->GetColorTemperature());
}

TEST_F(AmbientLightSensorTest, UpdateWithLuxAndColorTemperature) {
  delegate_->SetLuxAndColorTemperature(100, 200);
  EXPECT_TRUE(observer_.Updated());

  EXPECT_EQ(100, sensor_->GetAmbientLightLux());
  EXPECT_EQ(200, sensor_->GetColorTemperature());

  delegate_->SetLuxAndColorTemperature(base::nullopt, base::nullopt);
  EXPECT_TRUE(observer_.Updated());

  // lux doesn't change.
  EXPECT_EQ(100, sensor_->GetAmbientLightLux());
  EXPECT_EQ(200, sensor_->GetColorTemperature());
}

}  // namespace system
}  // namespace power_manager
