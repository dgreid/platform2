// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_service.h"

#include <algorithm>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/connection.h"
#include "shill/control_interface.h"
#include "shill/key_value_store.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/ppp_device.h"
#include "shill/profile.h"
#include "shill/property_accessor.h"
#include "shill/store_interface.h"
#include "shill/technology.h"
#include "shill/vpn/vpn_driver.h"
#include "shill/vpn/vpn_provider.h"

using base::StringPrintf;
using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static string ObjectID(const VPNService* s) {
  return s->log_name();
}
}  // namespace Logging

const char VPNService::kAutoConnNeverConnected[] = "never connected";
const char VPNService::kAutoConnVPNAlreadyActive[] = "vpn already active";

VPNService::VPNService(Manager* manager, std::unique_ptr<VPNDriver> driver)
    : Service(manager, Technology::kVPN),
      driver_(std::move(driver)),
      link_down_(false) {
  if (driver_) {
    set_log_name("vpn_" + driver_->GetProviderType() + "_" +
                 base::NumberToString(serial_number()));
  } else {
    // |driver| may be null in tests.
    set_log_name("vpn_" + base::NumberToString(serial_number()));
  }
  SetConnectable(true);
  set_save_credentials(false);
  mutable_store()->RegisterDerivedString(
      kPhysicalTechnologyProperty,
      StringAccessor(new CustomAccessor<VPNService, string>(
          this, &VPNService::GetPhysicalTechnologyProperty, nullptr)));
  this->manager()->AddDefaultServiceObserver(this);
}

VPNService::~VPNService() {
  manager()->RemoveDefaultServiceObserver(this);
}

void VPNService::OnConnect(Error* error) {
  manager()->vpn_provider()->DisconnectAll();
  // Note that this must be called after VPNProvider::DisconnectAll. While most
  // VPNDrivers create their own Devices, ArcVpnDriver shares the same
  // VirtualDevice (VPNProvider::arc_device), so Disconnect()ing an ARC
  // VPNService after completing the connection for a new ARC VPNService will
  // cause the arc_device to be disabled at the end of this call.

  SetState(ConnectState::kStateAssociating);
  switch (driver_->GetIfType()) {
    case VPNDriver::kTunnel:
      if (!manager()->device_info()->CreateTunnelInterface(base::BindOnce(
              &VPNService::OnLinkReady, weak_factory_.GetWeakPtr()))) {
        Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                              "Could not create tunnel interface.");
        SetFailure(Service::kFailureInternal);
        SetErrorDetails(Service::kErrorDetailsNone);
        return;
      }
      // Flow continues in OnLinkReady().
      break;
    case VPNDriver::kArcBridge:
      device_ = manager()->vpn_provider()->arc_device();
      if (!device_) {
        Error::PopulateAndLog(FROM_HERE, error, Error::kNotFound,
                              "arc_device is not found");
        SetFailure(Service::kFailureInternal);
        SetErrorDetails(Service::kErrorDetailsNone);
        return;
      }
      FALLTHROUGH;
    case VPNDriver::kPPP:
      driver_->ConnectAsync(base::BindRepeating(&VPNService::OnDriverEvent,
                                                weak_factory_.GetWeakPtr()));
      // Flow continues in OnDriverEvent(kEventConnectionSuccess).
      break;
    default:
      NOTREACHED();
  }
}

void VPNService::OnDisconnect(Error* error, const char* reason) {
  SetState(ConnectState::kStateDisconnecting);
  driver_->Disconnect();
  CleanupDevice();

  SetState(ConnectState::kStateIdle);
}

void VPNService::OnLinkReady(const string& link_name, int interface_index) {
  switch (driver_->GetIfType()) {
    case VPNDriver::kTunnel:
      CHECK(!device_);
      device_ = new VirtualDevice(manager(), link_name, interface_index,
                                  Technology::kVPN);
      driver_->set_interface_name(link_name);
      driver_->ConnectAsync(base::BindRepeating(&VPNService::OnDriverEvent,
                                                weak_factory_.GetWeakPtr()));
      // Flow continues in OnDriverEvent(kEventConnectionSuccess).
      break;
    case VPNDriver::kPPP:
      // Only get called when driver notification arrives earlier than RTNL
      // notification - continues flow from
      // OnDriverEvent(kEventConnectionSuccess).
      device_ = new PPPDevice(manager(), link_name, interface_index);
      SetState(ConnectState::kStateConfiguring);
      ConfigureDevice();
      SetState(ConnectState::kStateConnected);
      SetState(ConnectState::kStateOnline);
      break;
    default:
      NOTREACHED();
  }
}

void VPNService::OnDriverEvent(DriverEvent event,
                               ConnectFailure failure,
                               const std::string& error_details) {
  switch (event) {
    case kEventConnectionSuccess:
      if (driver_->GetIfType() == VPNDriver::kPPP) {
        string link_name = driver_->interface_name();
        int interface_index = manager()->device_info()->GetIndex(link_name);
        if (interface_index < 0) {
          // To handle the potential race when the RTNL notification about the
          // new PPP device has not been received yet. Register a callback where
          // the remaining steps can be continued.
          manager()->device_info()->AddVirtualInterfaceReadyCallback(
              link_name, base::BindOnce(&VPNService::OnLinkReady,
                                        weak_factory_.GetWeakPtr()));
          return;
        }
        device_ = new PPPDevice(manager(), link_name, interface_index);
      }
      SetState(ConnectState::kStateConfiguring);
      ConfigureDevice();
      SetState(ConnectState::kStateConnected);
      SetState(ConnectState::kStateOnline);
      break;
    case kEventDriverFailure:
      CleanupDevice();
      SetErrorDetails(error_details);
      SetFailure(failure);
      break;
    case kEventDriverReconnecting:
      if (device_) {
        SetState(Service::kStateAssociating);
        device_->ResetConnection();
      }
      // Await further OnDriverEvent(kEventConnectionSuccess).
      break;
  }
}

void VPNService::CleanupDevice() {
  if (!device_)
    return;
  int interface_index = device_->interface_index();
  device_->DropConnection();
  device_->SetEnabled(false);
  device_ = nullptr;
  if (driver_->GetIfType() == VPNDriver::kTunnel) {
    manager()->device_info()->DeleteInterface(interface_index);
  }
}

void VPNService::ConfigureDevice() {
  if (!device_) {
    LOG(DFATAL) << "Device not created yet.";
    return;
  }

  IPConfig::Properties ip_properties = driver_->GetIPProperties();

  // TODO(taoyl): Remove routing policy from IPProperties.
  manager()->vpn_provider()->SetDefaultRoutingPolicy(&ip_properties);
  // Remove vpn virtual device from allow_iifs list to avoid route loop.
  // This is mainly for ARC bridge on ARC VPN (note ARC bridge needs to
  // be in allow_iifs for non-ARC VPNs). PPP and tunnel interface should
  // never be in allow_iifs.
  // TODO(taoyl): Remove this as part of routing policy migration.
  base::Erase(ip_properties.allowed_iifs, device_->link_name());

  device_->SetEnabled(true);
  device_->SelectService(this);
  device_->UpdateIPConfig(ip_properties);
  device_->SetLooseRouting(true);
}

string VPNService::GetStorageIdentifier() const {
  return storage_id_;
}

bool VPNService::IsAlwaysOnVpn(const string& package) const {
  // For ArcVPN connections, the driver host is set to the package name of the
  // Android app that is creating the VPN connection.
  return driver_->GetProviderType() == string(kProviderArcVpn) &&
         driver_->GetHost() == package;
}

// static
string VPNService::CreateStorageIdentifier(const KeyValueStore& args,
                                           Error* error) {
  string host = args.Lookup<string>(kProviderHostProperty, "");
  if (host.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidProperty,
                          "Missing VPN host.");
    return "";
  }
  string name = args.Lookup<string>(kNameProperty, "");
  if (name.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Missing VPN name.");
    return "";
  }
  return SanitizeStorageIdentifier(
      StringPrintf("vpn_%s_%s", host.c_str(), name.c_str()));
}

string VPNService::GetPhysicalTechnologyProperty(Error* error) {
  ConnectionConstRefPtr underlying_connection = GetUnderlyingConnection();
  if (!underlying_connection) {
    error->Populate(Error::kOperationFailed);
    return "";
  }

  return underlying_connection->technology().GetName();
}

RpcIdentifier VPNService::GetDeviceRpcId(Error* error) const {
  if (!device_) {
    error->Populate(Error::kNotFound, "Not associated with a device");
    return control_interface()->NullRpcIdentifier();
  }
  return device_->GetRpcIdentifier();
}

ConnectionConstRefPtr VPNService::GetUnderlyingConnection() const {
  // TODO(crbug.com/941597) Policy routing should be used to enforce that VPN
  // traffic can only exit the interface it is supposed to. The VPN driver
  // should also be informed of changes in underlying connection.
  ServiceRefPtr underlying_service = manager()->GetPrimaryPhysicalService();
  if (!underlying_service) {
    return nullptr;
  }
  return underlying_service->connection();
}

bool VPNService::Load(const StoreInterface* storage) {
  return Service::Load(storage) &&
         driver_->Load(storage, GetStorageIdentifier());
}

void VPNService::MigrateDeprecatedStorage(StoreInterface* storage) {
  Service::MigrateDeprecatedStorage(storage);

  const string id = GetStorageIdentifier();
  CHECK(storage->ContainsGroup(id));
  driver_->MigrateDeprecatedStorage(storage, id);
}

bool VPNService::Save(StoreInterface* storage) {
  return Service::Save(storage) &&
         driver_->Save(storage, GetStorageIdentifier(), save_credentials());
}

bool VPNService::Unload() {
  // The base method also disconnects the service.
  Service::Unload();

  set_save_credentials(false);
  driver_->UnloadCredentials();

  // Ask the VPN provider to remove us from its list.
  manager()->vpn_provider()->RemoveService(this);

  return true;
}

void VPNService::InitDriverPropertyStore() {
  driver_->InitPropertyStore(mutable_store());
}

void VPNService::EnableAndRetainAutoConnect() {
  // The base EnableAndRetainAutoConnect method also sets auto_connect_ to true
  // which is not desirable for VPN services.
  RetainAutoConnect();
}

bool VPNService::IsAutoConnectable(const char** reason) const {
  if (!Service::IsAutoConnectable(reason)) {
    return false;
  }
  // Don't auto-connect VPN services that have never connected. This improves
  // the chances that the VPN service is connectable and avoids dialog popups.
  if (!has_ever_connected()) {
    *reason = kAutoConnNeverConnected;
    return false;
  }
  // Don't auto-connect a VPN service if another VPN service is already active.
  if (manager()->vpn_provider()->HasActiveService()) {
    *reason = kAutoConnVPNAlreadyActive;
    return false;
  }
  return true;
}

string VPNService::GetTethering(Error* error) const {
  ConnectionConstRefPtr underlying_connection = GetUnderlyingConnection();
  string tethering;
  if (underlying_connection) {
    tethering = underlying_connection->tethering();
    if (!tethering.empty()) {
      return tethering;
    }
    // The underlying service may not have a Tethering property.  This is
    // not strictly an error, so we don't print an error message.  Populating
    // an error here just serves to propagate the lack of a property in
    // GetProperties().
    error->Populate(Error::kNotSupported);
  } else {
    error->Populate(Error::kOperationFailed);
  }
  return "";
}

bool VPNService::SetNameProperty(const string& name, Error* error) {
  if (name == friendly_name()) {
    return false;
  }
  LOG(INFO) << "SetNameProperty called for: " << log_name();

  KeyValueStore* args = driver_->args();
  args->Set<string>(kNameProperty, name);
  string new_storage_id = CreateStorageIdentifier(*args, error);
  if (new_storage_id.empty()) {
    return false;
  }
  string old_storage_id = storage_id_;
  DCHECK_NE(old_storage_id, new_storage_id);

  SetFriendlyName(name);

  // Update the storage identifier before invoking DeleteEntry to prevent it
  // from unloading this service.
  storage_id_ = new_storage_id;
  profile()->DeleteEntry(old_storage_id, nullptr);
  profile()->UpdateService(this);
  return true;
}

void VPNService::OnBeforeSuspend(const ResultCallback& callback) {
  driver_->OnBeforeSuspend(callback);
}

void VPNService::OnAfterResume() {
  driver_->OnAfterResume();
  Service::OnAfterResume();
}

void VPNService::OnDefaultServiceStateChanged(const ServiceRefPtr& service) {
  if (!link_down_ || service->state() != Service::kStateOnline)
    return;

  link_down_ = false;
  driver_->OnDefaultPhysicalServiceEvent(VPNDriver::kDefaultPhysicalServiceUp);
}

void VPNService::OnDefaultServiceChanged(
    const ServiceRefPtr& /* logical_service */,
    bool /* logical_service_changed */,
    const ServiceRefPtr& physical_service,
    bool physical_service_changed) {
  if (!physical_service_changed)
    return;

  SLOG(this, 2) << __func__ << "("
                << (physical_service ? physical_service->log_name() : "-")
                << ")";

  if (physical_service && physical_service->state() == Service::kStateOnline) {
    // The original service is no longer the default, but manager was able to
    // find another physical service that is already Online.
    driver_->OnDefaultPhysicalServiceEvent(
        VPNDriver::kDefaultPhysicalServiceChanged);
  } else {
    // The default physical service went away, and nothing else is available
    // right now. All we can do is wait.
    if (link_down_)
      return;
    SLOG(this, 2) << __func__ << " - physical connection lost";
    link_down_ = true;

    driver_->OnDefaultPhysicalServiceEvent(
        VPNDriver::kDefaultPhysicalServiceDown);
  }
}

}  // namespace shill
