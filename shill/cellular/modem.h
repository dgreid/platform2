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
  virtual ~Modem();

  // Gathers information and passes it to CreateDeviceFromModemProperties.
  void CreateDeviceMM1(const InterfaceToProperties& properties);

  void OnDeviceInfoAvailable(const std::string& link_name);

  const std::string& service() const { return service_; }
  const RpcIdentifier& path() const { return path_; }

  void set_type(Cellular::Type type) { type_ = type; }

 private:
  friend class ModemTest;
  FRIEND_TEST(ModemTest, CreateDeviceEarlyFailures);
  FRIEND_TEST(ModemTest, CreateDevicePPP);
  FRIEND_TEST(ModemTest, EarlyDeviceProperties);
  FRIEND_TEST(ModemTest, GetDeviceParams);
  FRIEND_TEST(ModemTest, PendingDevicePropertiesAndCreate);

  // Constants associated with fake network devices for PPP dongles.
  // See |fake_dev_serial_|, below, for more info.
  static constexpr char kFakeDevNameFormat[] = "no_netdev_%zu";
  static const char kFakeDevAddress[];
  static const int kFakeDevInterfaceIndex;

  // These are virtual for mock implementation. TODO(stevenjb): Use a Fake.
  virtual std::string GetModemInterface() const;
  virtual Cellular* ConstructCellular(const std::string& link_name,
                                      const std::string& device_name,
                                      int interface_index);
  virtual bool GetLinkName(const KeyValueStore& properties,
                           std::string* name) const;

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
  bool pending_device_info_;
  RTNLHandler* rtnl_handler_;

  // Serial number used to uniquify fake device names for Cellular
  // devices that don't have network devices. (Names must be unique
  // for D-Bus, and PPP dongles don't have network devices.)
  static size_t fake_dev_serial_;

  DISALLOW_COPY_AND_ASSIGN(Modem);
};

}  // namespace shill

#endif  // SHILL_CELLULAR_MODEM_H_
