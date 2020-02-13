// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_BLUETOOTH_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_BLUETOOTH_CONTROLLER_H_

#include <memory>
#include <string>

#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <dbus/property.h>

namespace power_manager {
namespace policy {

class BluetoothControllerInterface {
 public:
  BluetoothControllerInterface() {}
  virtual ~BluetoothControllerInterface() {}

  virtual const bool AllowWakeup() const = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(BluetoothControllerInterface);
};

class BluetoothController : public BluetoothControllerInterface {
 public:
  BluetoothController();
  ~BluetoothController() override;

  void Init();

  // Returns true when UseSuspendNotifier property is set on the Bluetooth
  // adapter. This is controlled by a chrome://flag "BluetoothSuspendNotifier".
  // This will be used while dogfooding changes to Bluetooth that allows
  // Bluetooth devices to wake the system from suspend.
  const bool AllowWakeup() const override;

 private:
  // Handles updates to bluetooth adapter properties
  void OnPropertyChanged(const std::string& property_name);

  // Holds the D-Bus properties that we care about on the Bluetooth adapter.
  struct Properties : public dbus::PropertySet {
    dbus::Property<bool> use_suspend_notifier;

    Properties(dbus::ObjectProxy* object_proxy,
               const std::string& interface_name,
               const PropertyChangedCallback& callback);
    ~Properties() override;
  };

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<Properties> properties_;

  base::WeakPtrFactory<BluetoothController> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(BluetoothController);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_BLUETOOTH_CONTROLLER_H_
