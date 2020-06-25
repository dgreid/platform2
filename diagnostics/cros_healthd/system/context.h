// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_

#include <memory>

#include <base/memory/scoped_refptr.h>
#include <brillo/dbus/dbus_connection.h>
#include <chromeos/chromeos-config/libcros_config/cros_config_interface.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <mojo/public/cpp/platform/platform_channel_endpoint.h>

#include "diagnostics/common/system/bluetooth_client.h"
#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/common/system/powerd_adapter.h"
#include "diagnostics/cros_healthd/executor/executor_adapter.h"
#include "diagnostics/cros_healthd/system/system_config_interface.h"

namespace org {
namespace chromium {
class debugdProxyInterface;
}  // namespace chromium
}  // namespace org

namespace diagnostics {

// A context class for holding the helper objects used in cros_healthd, which
// simplifies the passing of the helper objects to other objects. For instance,
// instead of passing various helper objects to an object via its constructor,
// the context object is passed.
class Context {
 public:
  // The no-arg constructor exists so that MockContext doesn't need to specify a
  // Mojo endpoint.
  Context();
  // All production uses should use this constructor.
  explicit Context(mojo::PlatformChannelEndpoint endpoint);
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  virtual ~Context();

  // Initializes all helper objects in the context. Returns true on success.
  // Must be called before any of the accessors are used.
  virtual bool Initialize();

  // Accessors for the various helper objects:

  // Use the object returned by bluetooth_client() to subscribe to notifications
  // for D-Bus objects representing Bluetooth adapters and devices.
  BluetoothClient* bluetooth_client() const;
  // Use the object returned by debugd_proxy() to make calls to debugd. Example:
  // cros_healthd calls out to debugd when it needs to collect smart battery
  // metrics like manufacture_date_smart and temperature_smart.
  org::chromium::debugdProxyInterface* debugd_proxy() const;
  // Use the object returned by debugd_adapter() to make calls to debugd.
  // Example: cros_healthd calls out to debugd with async callbacks when it
  // needs to trigger nvme self-test or collect data like progress info.
  DebugdAdapter* debugd_adapter() const;
  // Use the object returned by power_manager_proxy() to make calls to
  // power_manager. Example: cros_healthd calls out to power_manager when it
  // needs to collect battery metrics like cycle count.
  dbus::ObjectProxy* power_manager_proxy() const;
  // Use the object returned by powerd_adapter() to subscribe to notifications
  // from powerd.
  PowerdAdapter* powerd_adapter() const;
  // Use the object returned by system_config() to determine which conditional
  // features a device supports.
  SystemConfigInterface* system_config() const;
  // Use the object returned by executor() to make calls to the root-level
  // executor.
  ExecutorAdapter* executor() const;

 private:
  // Allows MockContext to override the default helper objects.
  friend class MockContext;

  // Used to connect to the root-level executor via Mojo.
  mojo::PlatformChannelEndpoint endpoint_;

  // This should be the only connection to D-Bus. Use |connection_| to get the
  // |dbus_bus_|.
  brillo::DBusConnection connection_;
  // Used by this object to initiate D-Bus clients.
  scoped_refptr<dbus::Bus> dbus_bus_;

  // Used by this object to initialize the SystemConfig. Used for reading
  // cros_config properties to determine device feature support.
  std::unique_ptr<brillo::CrosConfigInterface> cros_config_;

  // Members accessed via the accessor functions defined above.
  std::unique_ptr<BluetoothClient> bluetooth_client_;
  std::unique_ptr<org::chromium::debugdProxyInterface> debugd_proxy_;
  std::unique_ptr<DebugdAdapter> debugd_adapter_;
  // Owned by |dbus_bus_|.
  dbus::ObjectProxy* power_manager_proxy_;
  std::unique_ptr<PowerdAdapter> powerd_adapter_;
  std::unique_ptr<SystemConfigInterface> system_config_;
  std::unique_ptr<ExecutorAdapter> executor_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_CONTEXT_H_
