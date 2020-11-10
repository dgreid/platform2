// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MODEM_H_
#define SHILL_CELLULAR_MODEM_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/files/file_util.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/cellular/cellular.h"
#include "shill/cellular/dbus_objectmanager_proxy_interface.h"
#include "shill/cellular/modem_info.h"
#include "shill/dbus_properties_proxy_interface.h"
#include "shill/refptr_types.h"

namespace shill {

// Handles an instance of ModemManager.Modem and an instance of a Cellular
// device.
class Modem {
 public:
  // ||path| is the ModemManager.Modem DBus object path (e.g.,
  // "/org/freedesktop/ModemManager1/Modem/0").
  Modem(const std::string& service,
        const RpcIdentifier& path,
        ModemInfo* modem_info);
  Modem(const Modem&) = delete;
  Modem& operator=(const Modem&) = delete;

  virtual ~Modem();

  // Gathers information and passes it to CreateDeviceFromModemProperties.
  void CreateDeviceMM1(const InterfaceToProperties& properties);

  void OnDeviceInfoAvailable(const std::string& link_name);

  const std::string& link_name() const { return link_name_; }
  Cellular::Type type() const { return type_; }
  const std::string& service() const { return service_; }
  const RpcIdentifier& path() const { return path_; }

  Cellular* device_for_testing() { return device_.get(); }
  bool has_pending_device_info_for_testing() {
    return has_pending_device_info_;
  }

  // Constants associated with fake network devices for PPP dongles.
  // See |fake_dev_serial_|, below, for more info.
  static constexpr char kFakeDevNameFormat[] = "no_netdev_%zu";
  static const char kFakeDevAddress[];
  static const int kFakeDevInterfaceIndex;

 protected:
  // Overridden in tests to provide a MockCellular instance instead of
  // creating a real instance. TODO(b:172077101): Use a delegate interface
  // instead once Cellular lifetime is detached from Modem lifetime.
  virtual Cellular* ConstructCellular(const std::string& mac_address,
                                      int interface_index);

  ModemInfo* modem_info_for_testing() { return modem_info_; }
  void set_rtnl_handler_for_testing(RTNLHandler* rtnl_handler) {
    rtnl_handler_ = rtnl_handler;
  }

 private:
  friend class ModemTest;

  std::string GetModemInterface() const;
  bool GetLinkName(const KeyValueStore& properties, std::string* name) const;

  // Asynchronously initializes support for the modem.
  // If the |properties| are valid and the MAC address is present,
  // constructs and registers a Cellular device in |device_| based on
  // |properties|.
  void CreateDeviceFromModemProperties(const InterfaceToProperties& properties);

  // Find the |mac_address| and |interface_index| for the kernel
  // network device with name |link_name|. Returns true iff both
  // |mac_address| and |interface_index| were found. Modifies
  // |interface_index| even on failure.
  bool GetDeviceParams(std::string* mac_address, int* interface_index);

  void OnPropertiesChanged(
      const std::string& interface,
      const KeyValueStore& changed_properties,
      const std::vector<std::string>& invalidated_properties);

  void OnModemManagerPropertiesChanged(const std::string& interface,
                                       const KeyValueStore& properties);

  // A proxy to the org.freedesktop.DBusProperties interface used to obtain
  // ModemManager.Modem properties and watch for property changes
  std::unique_ptr<DBusPropertiesProxyInterface> dbus_properties_proxy_;

  InterfaceToProperties initial_properties_;

  const std::string service_;
  const RpcIdentifier path_;

  CellularRefPtr device_;

  ModemInfo* modem_info_;
  std::string link_name_;
  Cellular::Type type_;
  bool has_pending_device_info_;
  RTNLHandler* rtnl_handler_;

  // Serial number used to uniquify fake device names for Cellular
  // devices that don't have network devices. (Names must be unique
  // for D-Bus, and PPP dongles don't have network devices.)
  static size_t fake_dev_serial_;
};

}  // namespace shill

#endif  // SHILL_CELLULAR_MODEM_H_
