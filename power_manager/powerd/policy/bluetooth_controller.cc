// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/bluetooth_controller.h"

#include <base/logging.h>

#include "chromeos/dbus/service_constants.h"

namespace power_manager {
namespace policy {

namespace {
// Currently, ChromeOS devices only use one Bluetooth adapter per device so hci0
// is constant.
constexpr char kBluetoothAdapterObjectPath[] = "/org/bluez/hci0";
}  // namespace

BluetoothController::Properties::Properties(
    dbus::ObjectProxy* object_proxy,
    const std::string& interface_name,
    const PropertyChangedCallback& callback)
    : dbus::PropertySet(object_proxy, interface_name, callback) {
  RegisterProperty(bluetooth_adapter::kUseSuspendNotifierProperty,
                   &use_suspend_notifier);
}

BluetoothController::Properties::~Properties() = default;

BluetoothController::BluetoothController() {}
BluetoothController::~BluetoothController() = default;

void BluetoothController::Init() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;

  bus_ = base::MakeRefCounted<dbus::Bus>(options);
  CHECK(bus_->Connect());

  auto bt_dbus_proxy_ =
      bus_->GetObjectProxy(bluetooth_adapter::kBluetoothAdapterServiceName,
                           dbus::ObjectPath(kBluetoothAdapterObjectPath));

  properties_.reset(new BluetoothController::Properties(
      bt_dbus_proxy_, bluetooth_adapter::kBluetoothAdapterInterface,
      base::BindRepeating(&BluetoothController::OnPropertyChanged,
                          weak_ptr_factory_.GetWeakPtr())));

  properties_->ConnectSignals();
  properties_->GetAll();
}

const bool BluetoothController::AllowWakeup() const {
  return properties_->use_suspend_notifier.value();
}

void BluetoothController::OnPropertyChanged(const std::string& property_name) {
  if (property_name == properties_->use_suspend_notifier.name()) {
    LOG(INFO) << "Bluetooth wakeup permission changed to "
              << properties_->use_suspend_notifier.value();
  }
}

}  // namespace policy
}  // namespace power_manager
