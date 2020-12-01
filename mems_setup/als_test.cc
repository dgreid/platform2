// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <libmems/iio_context.h>
#include <libmems/iio_device.h>
#include <libmems/test_fakes.h>
#include "mems_setup/configuration.h"
#include "mems_setup/delegate.h"
#include "mems_setup/sensor_location.h"
#include "mems_setup/test_fakes.h"
#include "mems_setup/test_helper.h"

using mems_setup::testing::SensorTestBase;

namespace mems_setup {

namespace {

static gid_t kIioserviceGroupId = 777;

#if USE_IIOSERVICE
constexpr char kAcpiAlsTriggerName[] = "iioservice-acpi-als";
#endif  // USE_IIOSERVICE

class AlsTest : public SensorTestBase {
 public:
  AlsTest() : SensorTestBase("acpi-als", 5, SensorKind::LIGHT) {
    mock_delegate_->AddGroup(Configuration::GetGroupNameForSysfs(),
                             kIioserviceGroupId);
    mock_delegate_->SetMockContext(mock_context_.get());
  }
};

#if USE_IIOSERVICE
TEST_F(AlsTest, TriggerSet) {
  SetSingleSensor(kBaseSensorLocation);
  ConfigureVpd({{"als_cal_intercept", "100"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_TRUE(mock_device_->GetTrigger());
  EXPECT_EQ(strcmp(mock_device_->GetTrigger()->GetName(), kAcpiAlsTriggerName),
            0);
}
#endif  // USE_IIOSERVICE

TEST_F(AlsTest, PartialVpd) {
  SetSingleSensor(kBaseSensorLocation);
  ConfigureVpd({{"als_cal_intercept", "100"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_TRUE(mock_device_->GetChannel("illuminance")
                  ->ReadDoubleAttribute("calibbias")
                  .has_value());
  EXPECT_EQ(100, mock_device_->GetChannel("illuminance")
                     ->ReadDoubleAttribute("calibbias")
                     .value());
  EXPECT_FALSE(mock_device_->GetChannel("illuminance")
                   ->ReadDoubleAttribute("calibscale")
                   .has_value());
}

TEST_F(AlsTest, VpdFormatError) {
  SetSingleSensor(kBaseSensorLocation);
  ConfigureVpd({{"als_cal_slope", "abc"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_FALSE(mock_device_->GetChannel("illuminance")
                   ->ReadDoubleAttribute("calibbias")
                   .has_value());
  EXPECT_FALSE(mock_device_->GetChannel("illuminance")
                   ->ReadDoubleAttribute("calibscale")
                   .has_value());
}

TEST_F(AlsTest, ValidVpd) {
  SetSingleSensor(kBaseSensorLocation);
  ConfigureVpd({{"als_cal_intercept", "1.25"}, {"als_cal_slope", "12.5"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_TRUE(mock_device_->GetChannel("illuminance")
                  ->ReadDoubleAttribute("calibbias")
                  .has_value());
  EXPECT_EQ(1.25, mock_device_->GetChannel("illuminance")
                      ->ReadDoubleAttribute("calibbias")
                      .value());
  EXPECT_TRUE(mock_device_->GetChannel("illuminance")
                  ->ReadDoubleAttribute("calibscale")
                  .has_value());
  EXPECT_EQ(12.5, mock_device_->GetChannel("illuminance")
                      ->ReadDoubleAttribute("calibscale")
                      .value());
}

TEST_F(AlsTest, VpdCalSlopeColorGood) {
  SetColorLightSensor();
  ConfigureVpd({{"als_cal_slope_color", "1.1 1.2 1.3"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_TRUE(mock_device_->GetChannel("illuminance_red")
                  ->ReadDoubleAttribute("calibscale")
                  .has_value());
  EXPECT_EQ(1.1, mock_device_->GetChannel("illuminance_red")
                     ->ReadDoubleAttribute("calibscale")
                     .value());

  EXPECT_TRUE(mock_device_->GetChannel("illuminance_green")
                  ->ReadDoubleAttribute("calibscale")
                  .has_value());
  EXPECT_EQ(1.2, mock_device_->GetChannel("illuminance_green")
                     ->ReadDoubleAttribute("calibscale")
                     .value());

  EXPECT_TRUE(mock_device_->GetChannel("illuminance_blue")
                  ->ReadDoubleAttribute("calibscale")
                  .has_value());
  EXPECT_EQ(1.3, mock_device_->GetChannel("illuminance_blue")
                     ->ReadDoubleAttribute("calibscale")
                     .value());
}

TEST_F(AlsTest, VpdCalSlopeColorCorrupted) {
  SetColorLightSensor();
  ConfigureVpd({{"als_cal_slope_color", "1.1 no 1.3"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_TRUE(mock_device_->GetChannel("illuminance_red")
                  ->ReadDoubleAttribute("calibscale")
                  .has_value());
  EXPECT_EQ(1.1, mock_device_->GetChannel("illuminance_red")
                     ->ReadDoubleAttribute("calibscale")
                     .value());

  EXPECT_FALSE(mock_device_->GetChannel("illuminance_green")
                   ->ReadDoubleAttribute("calibscale")
                   .has_value());

  EXPECT_FALSE(mock_device_->GetChannel("illuminance_blue")
                   ->ReadDoubleAttribute("calibscale")
                   .has_value());
}

TEST_F(AlsTest, VpdCalSlopeColorIncomplete) {
  SetColorLightSensor();
  ConfigureVpd({{"als_cal_slope_color", "1.1"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_FALSE(mock_device_->GetChannel("illuminance_red")
                   ->ReadDoubleAttribute("calibscale")
                   .has_value());

  EXPECT_FALSE(mock_device_->GetChannel("illuminance_green")
                   ->ReadDoubleAttribute("calibscale")
                   .has_value());

  EXPECT_FALSE(mock_device_->GetChannel("illuminance_blue")
                   ->ReadDoubleAttribute("calibscale")
                   .has_value());
}

}  // namespace

}  // namespace mems_setup
