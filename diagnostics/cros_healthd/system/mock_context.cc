// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/mock_context.h"

#include <memory>

#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>

#include "debugd/dbus-proxy-mocks.h"

namespace diagnostics {

MockContext::MockContext() = default;
MockContext::~MockContext() = default;

bool MockContext::Initialize() {
  // Initialize the mock D-Bus connection.
  options_.bus_type = dbus::Bus::SYSTEM;
  mock_bus_ = new dbus::MockBus(options_);
  mock_power_manager_proxy_ = new dbus::MockObjectProxy(
      mock_bus_.get(), power_manager::kPowerManagerServiceName,
      dbus::ObjectPath(power_manager::kPowerManagerServicePath));

  bluetooth_client_ = std::make_unique<FakeBluetoothClient>();
  debugd_proxy_ =
      std::make_unique<testing::StrictMock<org::chromium::debugdProxyMock>>();
  debugd_adapter_ = std::make_unique<testing::StrictMock<MockDebugdAdapter>>();
  power_manager_proxy_ = mock_power_manager_proxy_.get();
  powerd_adapter_ = std::make_unique<FakePowerdAdapter>();
  system_config_ = std::make_unique<FakeSystemConfig>();
  system_utils_ = std::make_unique<FakeSystemUtilities>();
  executor_ = std::make_unique<MockExecutorAdapter>();

  return true;
}

FakeBluetoothClient* MockContext::fake_bluetooth_client() const {
  return static_cast<FakeBluetoothClient*>(bluetooth_client_.get());
}

org::chromium::debugdProxyMock* MockContext::mock_debugd_proxy() const {
  return static_cast<testing::StrictMock<org::chromium::debugdProxyMock>*>(
      debugd_proxy_.get());
}

MockDebugdAdapter* MockContext::mock_debugd_adapter() const {
  return static_cast<testing::StrictMock<MockDebugdAdapter>*>(
      debugd_adapter_.get());
}

dbus::MockObjectProxy* MockContext::mock_power_manager_proxy() const {
  return mock_power_manager_proxy_.get();
}

FakePowerdAdapter* MockContext::fake_powerd_adapter() const {
  return static_cast<FakePowerdAdapter*>(powerd_adapter_.get());
}

FakeSystemConfig* MockContext::fake_system_config() const {
  return static_cast<FakeSystemConfig*>(system_config_.get());
}

FakeSystemUtilities* MockContext::fake_system_utils() const {
  return static_cast<FakeSystemUtilities*>(system_utils_.get());
}

MockExecutorAdapter* MockContext::mock_executor() const {
  return static_cast<MockExecutorAdapter*>(executor_.get());
}

}  // namespace diagnostics
