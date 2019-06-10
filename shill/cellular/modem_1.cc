// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem.h"

#include <ModemManager/ModemManager.h>

#include "shill/cellular/cellular.h"
#include "shill/device_info.h"

using std::string;

namespace shill {

Modem1::Modem1(const string& service,
               const RpcIdentifier& path,
               ModemInfo* modem_info)
    : Modem(service, path, modem_info) {}

Modem1::~Modem1() = default;

bool Modem1::GetLinkName(const KeyValueStore& modem_props, string* name) const {
  if (!modem_props.ContainsVariant(MM_MODEM_PROPERTY_PORTS)) {
    LOG(ERROR) << "Device missing property: " << MM_MODEM_PROPERTY_PORTS;
    return false;
  }

  auto ports = modem_props.GetVariant(MM_MODEM_PROPERTY_PORTS)
                   .Get<std::vector<std::tuple<string, uint32_t>>>();
  string net_port;
  for (const auto& port_pair : ports) {
    if (std::get<1>(port_pair) == MM_MODEM_PORT_TYPE_NET) {
      net_port = std::get<0>(port_pair);
      break;
    }
  }

  if (net_port.empty()) {
    LOG(ERROR) << "Could not find net port used by the device.";
    return false;
  }

  *name = net_port;
  return true;
}

void Modem1::CreateDeviceMM1(const InterfaceToProperties& properties) {
  Init();
  uint32_t capabilities = std::numeric_limits<uint32_t>::max();
  InterfaceToProperties::const_iterator it =
      properties.find(MM_DBUS_INTERFACE_MODEM);
  if (it == properties.end()) {
    LOG(ERROR) << "Cellular device with no modem properties";
    return;
  }
  const KeyValueStore& modem_props = it->second;
  if (modem_props.ContainsUint(MM_MODEM_PROPERTY_CURRENTCAPABILITIES)) {
    capabilities = modem_props.GetUint(MM_MODEM_PROPERTY_CURRENTCAPABILITIES);
  }

  if (capabilities & (MM_MODEM_CAPABILITY_GSM_UMTS | MM_MODEM_CAPABILITY_LTE |
                      MM_MODEM_CAPABILITY_LTE_ADVANCED)) {
    set_type(Cellular::kType3gpp);
  } else if (capabilities & MM_MODEM_CAPABILITY_CDMA_EVDO) {
    set_type(Cellular::kTypeCdma);
  } else {
    LOG(ERROR) << "Unsupported capabilities: " << capabilities;
    return;
  }

  // We cannot check the IP method to make sure it's not PPP. The IP
  // method will be checked later when the bearer object is fetched.
  CreateDeviceFromModemProperties(properties);
}

string Modem1::GetModemInterface() const {
  return string(MM_DBUS_INTERFACE_MODEM);
}

}  // namespace shill
