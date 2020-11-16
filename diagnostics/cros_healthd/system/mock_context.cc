// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/mock_context.h"

#include <memory>

#include <gmock/gmock.h>

#include "debugd/dbus-proxy-mocks.h"

namespace diagnostics {

MockContext::MockContext() = default;
MockContext::~MockContext() = default;

bool MockContext::Initialize() {
  bluetooth_client_ = std::make_unique<FakeBluetoothClient>();
  cros_config_ = std::make_unique<brillo::FakeCrosConfig>();
  debugd_proxy_ =
      std::make_unique<testing::StrictMock<org::chromium::debugdProxyMock>>();
  debugd_adapter_ = std::make_unique<testing::StrictMock<MockDebugdAdapter>>();
  network_health_adapter_ = std::make_unique<FakeNetworkHealthAdapter>();
  network_diagnostics_adapter_ =
      std::make_unique<MockNetworkDiagnosticsAdapter>();
  powerd_adapter_ = std::make_unique<FakePowerdAdapter>();
  system_config_ = std::make_unique<FakeSystemConfig>();
  system_utils_ = std::make_unique<FakeSystemUtilities>();
  executor_ = std::make_unique<MockExecutorAdapter>();
  tick_clock_ = std::make_unique<base::SimpleTestTickClock>();

  if (!temp_dir_.CreateUniqueTempDir())
    return false;
  root_dir_ = temp_dir_.GetPath();

  return true;
}

FakeBluetoothClient* MockContext::fake_bluetooth_client() const {
  return static_cast<FakeBluetoothClient*>(bluetooth_client_.get());
}

brillo::FakeCrosConfig* MockContext::fake_cros_config() const {
  return static_cast<brillo::FakeCrosConfig*>(cros_config_.get());
}

org::chromium::debugdProxyMock* MockContext::mock_debugd_proxy() const {
  return static_cast<testing::StrictMock<org::chromium::debugdProxyMock>*>(
      debugd_proxy_.get());
}

MockDebugdAdapter* MockContext::mock_debugd_adapter() const {
  return static_cast<testing::StrictMock<MockDebugdAdapter>*>(
      debugd_adapter_.get());
}

FakeNetworkHealthAdapter* MockContext::fake_network_health_adapter() const {
  return static_cast<FakeNetworkHealthAdapter*>(network_health_adapter_.get());
}

MockNetworkDiagnosticsAdapter* MockContext::network_diagnostics_adapter()
    const {
  return static_cast<MockNetworkDiagnosticsAdapter*>(
      network_diagnostics_adapter_.get());
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

base::SimpleTestTickClock* MockContext::mock_tick_clock() const {
  return static_cast<base::SimpleTestTickClock*>(tick_clock_.get());
}

}  // namespace diagnostics
