// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/context.h"

#include <utility>

#include <base/logging.h>
#include <chromeos/chromeos-config/libcros_config/cros_config.h>
#include <dbus/power_manager/dbus-constants.h>

#include "debugd/dbus-proxies.h"
#include "diagnostics/common/system/bluetooth_client_impl.h"
#include "diagnostics/common/system/debugd_adapter_impl.h"
#include "diagnostics/common/system/powerd_adapter_impl.h"
#include "diagnostics/cros_healthd/system/system_config.h"

namespace diagnostics {

Context::Context() = default;

Context::Context(mojo::PlatformChannelEndpoint endpoint)
    : endpoint_(std::move(endpoint)) {}

Context::~Context() = default;

bool Context::Initialize() {
  // Initialize the D-Bus connection.
  dbus_bus_ = connection_.Connect();
  if (!dbus_bus_) {
    LOG(ERROR) << "Failed to connect to the D-Bus system bus.";
    return false;
  }

  // Initialize D-Bus clients:
  bluetooth_client_ = std::make_unique<BluetoothClientImpl>(dbus_bus_);
  debugd_proxy_ = std::make_unique<org::chromium::debugdProxy>(dbus_bus_);
  debugd_adapter_ = std::make_unique<DebugdAdapterImpl>(
      std::make_unique<org::chromium::debugdProxy>(dbus_bus_));
  // TODO(crbug/1074476): Remove |power_manager_proxy_| once |powerd_adapter_|
  // supports all the methods we call on |power_manager_proxy_|.
  power_manager_proxy_ = dbus_bus_->GetObjectProxy(
      power_manager::kPowerManagerServiceName,
      dbus::ObjectPath(power_manager::kPowerManagerServicePath));
  powerd_adapter_ = std::make_unique<PowerdAdapterImpl>(dbus_bus_);

  cros_config_ = std::make_unique<brillo::CrosConfig>();

  // Init should always succeed on unibuild boards.
  if (!static_cast<brillo::CrosConfig*>(cros_config_.get())->Init()) {
    LOG(ERROR) << "Unable to initialize cros_config";
    return false;
  }

  system_config_ = std::make_unique<SystemConfig>(cros_config_.get());

  // Create and connect the adapter for the root-level executor.
  executor_ = std::make_unique<ExecutorAdapter>();
  executor_->Connect(std::move(endpoint_));

  return true;
}

BluetoothClient* Context::bluetooth_client() const {
  return bluetooth_client_.get();
}

org::chromium::debugdProxyInterface* Context::debugd_proxy() const {
  return debugd_proxy_.get();
}

DebugdAdapter* Context::debugd_adapter() const {
  return debugd_adapter_.get();
}

dbus::ObjectProxy* Context::power_manager_proxy() const {
  return power_manager_proxy_;
}

PowerdAdapter* Context::powerd_adapter() const {
  return powerd_adapter_.get();
}

SystemConfigInterface* Context::system_config() const {
  return system_config_.get();
}

ExecutorAdapter* Context::executor() const {
  return executor_.get();
}

}  // namespace diagnostics
