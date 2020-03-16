// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mems_setup/test_helper.h"

using libmems::fakes::FakeIioChannel;
using libmems::fakes::FakeIioContext;
using libmems::fakes::FakeIioDevice;
using mems_setup::fakes::FakeDelegate;

namespace mems_setup {
namespace testing {

bool FakeSysfsTrigger::WriteNumberAttribute(const std::string& name,
                                            int64_t value) {
  bool ok = this->FakeIioDevice::WriteNumberAttribute(name, value);
  if (ok && name == "add_trigger" && value == 0) {
    mock_context_->AddTrigger(mock_trigger_);
  }
  return ok;
}

SensorTestBase::SensorTestBase(const char* name, int id, SensorKind kind)
    : mock_context_(new FakeIioContext),
      mock_delegate_(new FakeDelegate),
      mock_device_(
          std::make_unique<FakeIioDevice>(mock_context_.get(), name, id)),
      mock_calib_channel_(
          std::make_unique<FakeIioChannel>("calibration", false)),
      mock_trigger1_(std::make_unique<FakeIioDevice>(
          mock_context_.get(), "sysfstrig0", 1)),
      mock_sysfs_trigger_(std::make_unique<FakeSysfsTrigger>(
          mock_context_.get(), mock_trigger1_.get())),
      sensor_kind_(kind) {
  mock_context_->AddDevice(mock_device_.get());
  mock_device_->AddChannel(mock_calib_channel_.get());
  mock_context_->AddTrigger(mock_sysfs_trigger_.get());
}

void SensorTestBase::SetSingleSensor(const char* location) {
  mock_device_->WriteStringAttribute("location", location);

  if (sensor_kind_ == SensorKind::ACCELEROMETER) {
    channels_.push_back(std::make_unique<FakeIioChannel>("accel_x", false));
    channels_.push_back(std::make_unique<FakeIioChannel>("accel_y", false));
    channels_.push_back(std::make_unique<FakeIioChannel>("accel_z", false));

    channels_.push_back(std::make_unique<FakeIioChannel>("timestamp", true));
  } else if (sensor_kind_ == SensorKind::GYROSCOPE) {
    channels_.push_back(std::make_unique<FakeIioChannel>("anglvel_x", false));
    channels_.push_back(std::make_unique<FakeIioChannel>("anglvel_y", false));
    channels_.push_back(std::make_unique<FakeIioChannel>("anglvel_z", false));

    channels_.push_back(std::make_unique<FakeIioChannel>("timestamp", true));
  } else if (sensor_kind_ == SensorKind::LIGHT) {
    channels_.push_back(std::make_unique<FakeIioChannel>("illuminance", false));

    channels_.push_back(std::make_unique<FakeIioChannel>("timestamp", true));
  }

  for (const auto& channel : channels_)
    mock_device_->AddChannel(channel.get());
}

void SensorTestBase::SetSharedSensor() {
  if (sensor_kind_ == SensorKind::ACCELEROMETER) {
    channels_.push_back(
        std::make_unique<FakeIioChannel>("accel_x_base", false));
    channels_.push_back(
        std::make_unique<FakeIioChannel>("accel_y_base", false));
    channels_.push_back(
        std::make_unique<FakeIioChannel>("accel_z_base", false));

    channels_.push_back(std::make_unique<FakeIioChannel>("accel_x_lid", false));
    channels_.push_back(std::make_unique<FakeIioChannel>("accel_y_lid", false));
    channels_.push_back(std::make_unique<FakeIioChannel>("accel_z_lid", false));

    channels_.push_back(std::make_unique<FakeIioChannel>("timestamp", true));
  } else if (sensor_kind_ == SensorKind::GYROSCOPE) {
    channels_.push_back(
        std::make_unique<FakeIioChannel>("anglvel_x_base", false));
    channels_.push_back(
        std::make_unique<FakeIioChannel>("anglvel_y_base", false));
    channels_.push_back(
        std::make_unique<FakeIioChannel>("anglvel_z_base", false));

    channels_.push_back(
        std::make_unique<FakeIioChannel>("anglvel_x_lid", false));
    channels_.push_back(
        std::make_unique<FakeIioChannel>("anglvel_y_lid", false));
    channels_.push_back(
        std::make_unique<FakeIioChannel>("anglvel_z_lid", false));

    channels_.push_back(std::make_unique<FakeIioChannel>("timestamp", true));
  }

  for (const auto& channel : channels_)
    mock_device_->AddChannel(channel.get());
}

void SensorTestBase::SetColorLightSensor() {
  if (sensor_kind_ != SensorKind::LIGHT)
    return;

  channels_.push_back(std::make_unique<FakeIioChannel>("illuminance", false));
  channels_.push_back(
      std::make_unique<FakeIioChannel>("illuminance_red", false));
  channels_.push_back(
      std::make_unique<FakeIioChannel>("illuminance_green", false));
  channels_.push_back(
      std::make_unique<FakeIioChannel>("illuminance_blue", false));

  channels_.push_back(std::make_unique<FakeIioChannel>("timestamp", true));

  for (const auto& channel : channels_)
    mock_device_->AddChannel(channel.get());
}

void SensorTestBase::ConfigureVpd(
    std::initializer_list<std::pair<const char*, const char*>> values) {
  for (const auto& value : values) {
    mock_delegate_->SetVpdValue(value.first, value.second);
  }
}

Configuration* SensorTestBase::GetConfiguration() {
  if (config_ == nullptr) {
    config_.reset(new Configuration(mock_context_.get(), mock_device_.get(),
                                    sensor_kind_, mock_delegate_.get()));
  }

  return config_.get();
}

}  // namespace testing
}  // namespace mems_setup
