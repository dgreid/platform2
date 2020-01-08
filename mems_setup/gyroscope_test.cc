// Copyright 2019 The Chromium OS Authors. All rights reserved.
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

using libmems::fakes::FakeIioChannel;
using libmems::fakes::FakeIioContext;
using libmems::fakes::FakeIioDevice;
using mems_setup::fakes::FakeDelegate;
using mems_setup::testing::SensorTestBase;

namespace mems_setup {

namespace {

static gid_t kIioserviceGroupId = 777;

class GyroscopeTest : public SensorTestBase {
 public:
  GyroscopeTest() : SensorTestBase("cros-ec-gyro", 2, SensorKind::GYROSCOPE) {
    mock_delegate_->AddGroup(Configuration::GetGroupNameForSysfs(),
                             kIioserviceGroupId);
  }
};

TEST_F(GyroscopeTest, MissingVpd) {
  SetSingleSensor(kBaseSensorLocation);
  ConfigureVpd({{"in_anglvel_x_base_calibbias", "100"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_TRUE(mock_device_->GetChannel("anglvel_x")
                  ->ReadNumberAttribute("calibbias")
                  .has_value());
  EXPECT_EQ(100, mock_device_->GetChannel("anglvel_x")
                     ->ReadNumberAttribute("calibbias")
                     .value());
  EXPECT_FALSE(mock_device_->GetChannel("anglvel_y")
                   ->ReadNumberAttribute("calibbias")
                   .has_value());
  EXPECT_FALSE(mock_device_->GetChannel("anglvel_z")
                   ->ReadNumberAttribute("calibbias")
                   .has_value());
}

TEST_F(GyroscopeTest, NotNumericVpd) {
  SetSingleSensor(kBaseSensorLocation);
  ConfigureVpd({{"in_anglvel_x_base_calibbias", "blah"},
                {"in_anglvel_y_base_calibbias", "104"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_FALSE(mock_device_->GetChannel("anglvel_x")
                   ->ReadNumberAttribute("calibbias")
                   .has_value());
  EXPECT_TRUE(mock_device_->GetChannel("anglvel_y")
                  ->ReadNumberAttribute("calibbias")
                  .has_value());
  EXPECT_EQ(104, mock_device_->GetChannel("anglvel_y")
                     ->ReadNumberAttribute("calibbias")
                     .value());
  EXPECT_FALSE(mock_device_->GetChannel("anglvel_z")
                   ->ReadNumberAttribute("calibbias")
                   .has_value());
}

TEST_F(GyroscopeTest, VpdOutOfRange) {
  SetSingleSensor(kBaseSensorLocation);
  ConfigureVpd({{"in_anglvel_x_base_calibbias", "104"},
                {"in_anglvel_y_base_calibbias", "123456789"},
                {"in_anglvel_z_base_calibbias", "85"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_FALSE(mock_device_->GetChannel("anglvel_x")
                   ->ReadNumberAttribute("calibbias")
                   .has_value());
  EXPECT_FALSE(mock_device_->GetChannel("anglvel_y")
                   ->ReadNumberAttribute("calibbias")
                   .has_value());
  EXPECT_FALSE(mock_device_->GetChannel("anglvel_z")
                   ->ReadNumberAttribute("calibbias")
                   .has_value());
}

TEST_F(GyroscopeTest, NotLoadingTriggerModule) {
  SetSingleSensor(kBaseSensorLocation);
  ConfigureVpd({{"in_anglvel_x_base_calibbias", "50"},
                {"in_anglvel_y_base_calibbias", "104"},
                {"in_anglvel_z_base_calibbias", "85"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_EQ(0, mock_delegate_->GetNumModulesProbed());
}

TEST_F(GyroscopeTest, MultipleSensorDevice) {
  SetSharedSensor();
  ConfigureVpd({{"in_anglvel_x_base_calibbias", "50"},
                {"in_anglvel_y_base_calibbias", "104"},
                {"in_anglvel_z_base_calibbias", "85"},
                {"in_anglvel_y_lid_calibbias", "27"}});

  EXPECT_TRUE(GetConfiguration()->Configure());

  EXPECT_TRUE(mock_device_->GetChannel("anglvel_x_base")
                  ->ReadNumberAttribute("calibbias")
                  .has_value());
  EXPECT_TRUE(mock_device_->GetChannel("anglvel_y_base")
                  ->ReadNumberAttribute("calibbias")
                  .has_value());
  EXPECT_TRUE(mock_device_->GetChannel("anglvel_z_base")
                  ->ReadNumberAttribute("calibbias")
                  .has_value());

  EXPECT_EQ(50, mock_device_->GetChannel("anglvel_x_base")
                    ->ReadNumberAttribute("calibbias")
                    .value());
  EXPECT_EQ(104, mock_device_->GetChannel("anglvel_y_base")
                     ->ReadNumberAttribute("calibbias")
                     .value());
  EXPECT_EQ(85, mock_device_->GetChannel("anglvel_z_base")
                    ->ReadNumberAttribute("calibbias")
                    .value());

  EXPECT_FALSE(mock_device_->GetChannel("anglvel_x_lid")
                   ->ReadNumberAttribute("calibbias")
                   .has_value());
  EXPECT_TRUE(mock_device_->GetChannel("anglvel_y_lid")
                  ->ReadNumberAttribute("calibbias")
                  .has_value());
  EXPECT_EQ(27, mock_device_->GetChannel("anglvel_y_lid")
                    ->ReadNumberAttribute("calibbias")
                    .value());
  EXPECT_FALSE(mock_device_->GetChannel("anglvel_z_lid")
                   ->ReadNumberAttribute("calibbias")
                   .has_value());
}

}  // namespace

}  // namespace mems_setup
