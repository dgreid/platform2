// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem.h"

#include <base/bind.h>
#include <base/strings/stringprintf.h>

#include "shill/cellular/cellular.h"
#include "shill/control_interface.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/net/rtnl_handler.h"

using base::Bind;
using base::Unretained;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kModem;
static string ObjectID(Modem* m) {
  return m->path().value().c_str();
}
}  // namespace Logging

// statics
constexpr char Modem::kFakeDevNameFormat[];
const char Modem::kFakeDevAddress[] = "000000000000";
const int Modem::kFakeDevInterfaceIndex = -1;
size_t Modem::fake_dev_serial_ = 0;

Modem::Modem(const string& service,
             const RpcIdentifier& path,
             ModemInfo* modem_info)
    : service_(service),
      path_(path),
      modem_info_(modem_info),
      type_(Cellular::kTypeInvalid),
      pending_device_info_(false),
      rtnl_handler_(RTNLHandler::GetInstance()) {
  LOG(INFO) << "Modem created: at " << path.value();
}

Modem::~Modem() {
  LOG(INFO) << "Modem destructed: " << path_.value();
  if (!device_) {
    return;
  }

  device_->DestroyService();
  device_->StopLocationPolling();
  // Under certain conditions, Cellular::StopModem may not be called before
  // the Cellular device is destroyed. This happens if the dbus modem exported
  // by the modem-manager daemon disappears soon after the modem is disabled,
  // not giving shill enough time to complete the disable operation.
  //
  // In that case, the termination action associated with this cellular object
  // may not have been removed.
  modem_info_->manager()->RemoveTerminationAction(device_->link_name());

  // Explicitly removes this object from being an observer to
  // |home_provider_info_| and |serving_operator_info_| to avoid them from
  // calling into this object while this object is being destructed.
  device_->home_provider_info()->RemoveObserver(device_.get());
  device_->serving_operator_info()->RemoveObserver(device_.get());

  // Ensure that the Cellular interface is fully destroyed here. If we wait for
  // an RTNL link delete message to be received by DeviceInfo, there's the
  // possibility that another Modem instance will come up and attempt to create
  // a Cellular instance with the same name as this device.
  //
  // Note that in the case where this destructor is called before the
  // corresponding RTNL link delete message is received
  // (i.e. ModemManager1::OnInterfacesRemovedSignal is called first), this means
  // that DeviceInfo::DelLinkMsgHandler will be called for a DeviceInfo::Info
  // that DeviceInfo no longer knows about, which DeviceInfo can handle.
  modem_info_->manager()->device_info()->DeregisterDevice(
      device_->interface_index());
}

void Modem::Init() {
  dbus_properties_proxy_ =
      modem_info_->control_interface()->CreateDBusPropertiesProxy(path(),
                                                                  service());
  dbus_properties_proxy_->set_modem_manager_properties_changed_callback(
      Bind(&Modem::OnModemManagerPropertiesChanged, Unretained(this)));
  dbus_properties_proxy_->set_properties_changed_callback(
      Bind(&Modem::OnPropertiesChanged, Unretained(this)));
}

void Modem::OnDeviceInfoAvailable(const string& link_name) {
  SLOG(this, 2) << __func__;
  if (pending_device_info_ && link_name_ == link_name) {
    // pending_device_info_ is only set if we've already been through
    // CreateDeviceFromModemProperties() and saved our initial
    // properties already
    pending_device_info_ = false;
    CreateDeviceFromModemProperties(initial_properties_);
  }
}

Cellular* Modem::ConstructCellular(const string& link_name,
                                   const string& address,
                                   int interface_index) {
  LOG(INFO) << "Creating a cellular device on link " << link_name
            << " interface index " << interface_index << ".";
  return new Cellular(modem_info_, link_name, address, interface_index, type_,
                      service_, path_);
}

void Modem::CreateDeviceFromModemProperties(
    const InterfaceToProperties& properties) {
  SLOG(this, 2) << __func__;

  if (device_) {
    return;
  }

  InterfaceToProperties::const_iterator properties_it =
      properties.find(GetModemInterface());
  if (properties_it == properties.end()) {
    LOG(ERROR) << "Unable to find modem interface properties.";
    return;
  }

  string mac_address;
  int interface_index = -1;
  if (GetLinkName(properties_it->second, &link_name_)) {
    GetDeviceParams(&mac_address, &interface_index);
    if (interface_index < 0) {
      LOG(ERROR) << "Unable to create cellular device -- no interface index.";
      return;
    }
    if (mac_address.empty()) {
      // Save our properties, wait for OnDeviceInfoAvailable to be called.
      LOG(WARNING)
          << "No hardware address, device creation pending device info.";
      initial_properties_ = properties;
      pending_device_info_ = true;
      return;
    }
    // Got the interface index and MAC address. Fall-through to actually
    // creating the Cellular object.
  } else {
    // Probably a PPP dongle.
    LOG(INFO) << "Cellular device without link name; assuming PPP dongle.";
    link_name_ = base::StringPrintf(kFakeDevNameFormat, fake_dev_serial_++);
    mac_address = kFakeDevAddress;
    interface_index = kFakeDevInterfaceIndex;
  }

  if (modem_info_->manager()->device_info()->IsDeviceBlackListed(link_name_)) {
    LOG(INFO) << "Not creating cellular device for blacklisted interface "
              << link_name_ << ".";
    return;
  }

  device_ = ConstructCellular(link_name_, mac_address, interface_index);
  // Give the device a chance to extract any capability-specific properties.
  for (properties_it = properties.begin(); properties_it != properties.end();
       ++properties_it) {
    device_->OnPropertiesChanged(properties_it->first, properties_it->second,
                                 vector<string>());
  }

  modem_info_->manager()->device_info()->RegisterDevice(device_);
}

bool Modem::GetDeviceParams(string* mac_address, int* interface_index) {
  // TODO(petkov): Get the interface index from DeviceInfo, similar to the MAC
  // address below.
  *interface_index = rtnl_handler_->GetInterfaceIndex(link_name_);
  if (*interface_index < 0) {
    return false;
  }

  ByteString address_bytes;
  if (!modem_info_->manager()->device_info()->GetMacAddress(*interface_index,
                                                            &address_bytes)) {
    return false;
  }

  *mac_address = address_bytes.HexEncode();
  return true;
}

void Modem::OnPropertiesChanged(const string& interface,
                                const KeyValueStore& changed_properties,
                                const vector<string>& invalidated_properties) {
  SLOG(this, 2) << __func__;
  SLOG(this, 3) << "PropertiesChanged signal received.";
  if (device_) {
    device_->OnPropertiesChanged(interface, changed_properties,
                                 invalidated_properties);
  }
}

void Modem::OnModemManagerPropertiesChanged(const string& interface,
                                            const KeyValueStore& properties) {
  vector<string> invalidated_properties;
  OnPropertiesChanged(interface, properties, invalidated_properties);
}

}  // namespace shill
