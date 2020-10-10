// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_provider.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/connection.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/process_manager.h"
#include "shill/profile.h"
#include "shill/routing_policy_entry.h"
#include "shill/store_interface.h"
#include "shill/vpn/arc_vpn_driver.h"
#include "shill/vpn/l2tp_ipsec_driver.h"
#include "shill/vpn/openvpn_driver.h"
#include "shill/vpn/third_party_vpn_driver.h"
#include "shill/vpn/vpn_service.h"

using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static string ObjectID(const VPNProvider* v) {
  return "(vpn_provider)";
}
}  // namespace Logging

namespace {

// TODO(b/151879931) Import these from libarcnetwork after platform2/arc/network
// has been moved to platform2/patchpanel
constexpr const uint32_t kFwMarkRouteOnVpnBit = 0x00008000;
constexpr const uint32_t kFwMarkBypassVpnBit = 0x00004000;
constexpr const uint32_t kFwMarkVpnMask =
    kFwMarkBypassVpnBit | kFwMarkRouteOnVpnBit;

constexpr const RoutingPolicyEntry::FwMark kFwMarkRouteOnVpn = {
    .value = kFwMarkRouteOnVpnBit,
    .mask = kFwMarkVpnMask,
};

// Populates |type_ptr|, |name_ptr| and |host_ptr| with the appropriate
// values from |args|.  Returns True on success, otherwise if any of
// these arguments are not available, |error| is populated and False is
// returned.
bool GetServiceParametersFromArgs(const KeyValueStore& args,
                                  string* type_ptr,
                                  string* name_ptr,
                                  string* host_ptr,
                                  Error* error) {
  SLOG(nullptr, 2) << __func__;
  string type = args.Lookup<string>(kProviderTypeProperty, "");
  if (type.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Missing VPN type property.");
    return false;
  }

  string host = args.Lookup<string>(kProviderHostProperty, "");
  if (host.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Missing VPN host property.");
    return false;
  }

  *type_ptr = type;
  *host_ptr = host;
  *name_ptr = args.Lookup<string>(kNameProperty, "");

  return true;
}

// Populates |vpn_type_ptr|, |name_ptr| and |host_ptr| with the appropriate
// values from profile storgae.  Returns True on success, otherwise if any of
// these arguments are not available, |error| is populated and False is
// returned.
bool GetServiceParametersFromStorage(const StoreInterface* storage,
                                     const string& entry_name,
                                     string* vpn_type_ptr,
                                     string* name_ptr,
                                     string* host_ptr,
                                     Error* error) {
  string service_type;
  if (!storage->GetString(entry_name, kTypeProperty, &service_type) ||
      service_type != kTypeVPN) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unspecified or invalid network type");
    return false;
  }

  if (!storage->GetString(entry_name, kProviderTypeProperty, vpn_type_ptr) ||
      vpn_type_ptr->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "VPN type not specified");
    return false;
  }

  if (!storage->GetString(entry_name, kNameProperty, name_ptr) ||
      name_ptr->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Network name not specified");
    return false;
  }

  if (!storage->GetString(entry_name, kProviderHostProperty, host_ptr) ||
      host_ptr->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Host not specified");
    return false;
  }
  return true;
}

}  // namespace

VPNProvider::VPNProvider(Manager* manager) : manager_(manager) {}

VPNProvider::~VPNProvider() = default;

void VPNProvider::Start() {}

void VPNProvider::Stop() {}

void VPNProvider::AddAllowedInterface(const std::string& interface_name) {
  if (base::Contains(allowed_iifs_, interface_name))
    return;

  // Add to the list of allowed nterfaces via |SetDefaultRoutingPolicy|
  // when connecting.
  allowed_iifs_.push_back(interface_name);

  // Update the routing table if there's an active VPN connection.
  for (auto& service : services_) {
    if (service->IsConnected()) {
      service->connection()->AddInputInterfaceToRoutingTable(interface_name);
    }
  }
}

void VPNProvider::RemoveAllowedInterface(const std::string& interface_name) {
  if (!base::Contains(allowed_iifs_, interface_name))
    return;

  base::Erase(allowed_iifs_, interface_name);
  // Update the routing table if there's an active VPN connection.
  for (auto& service : services_) {
    if (service->IsConnected()) {
      service->connection()->RemoveInputInterfaceFromRoutingTable(
          interface_name);
    }
  }
}

ServiceRefPtr VPNProvider::GetService(const KeyValueStore& args, Error* error) {
  SLOG(this, 2) << __func__;
  string type;
  string name;
  string host;

  if (!GetServiceParametersFromArgs(args, &type, &name, &host, error)) {
    return nullptr;
  }

  string storage_id = VPNService::CreateStorageIdentifier(args, error);
  if (storage_id.empty()) {
    return nullptr;
  }

  // Find a service in the provider list which matches these parameters.
  VPNServiceRefPtr service = FindService(type, name, host);
  if (service == nullptr) {
    service = CreateService(type, name, storage_id, error);
  }
  return service;
}

ServiceRefPtr VPNProvider::FindSimilarService(const KeyValueStore& args,
                                              Error* error) const {
  SLOG(this, 2) << __func__;
  string type;
  string name;
  string host;

  if (!GetServiceParametersFromArgs(args, &type, &name, &host, error)) {
    return nullptr;
  }

  // Find a service in the provider list which matches these parameters.
  VPNServiceRefPtr service = FindService(type, name, host);
  if (!service) {
    error->Populate(Error::kNotFound, "Matching service was not found");
  }

  return service;
}

bool VPNProvider::OnDeviceInfoAvailable(const string& link_name,
                                        int interface_index,
                                        Technology technology) {
  if (technology == Technology::kArcBridge) {
    arc_device_ = base::MakeRefCounted<VirtualDevice>(
        manager_, link_name, interface_index, Technology::kArcBridge);
    arc_device_->SetFixedIpParams(true);
    // Forward ARC->internet traffic over third-party VPN services.
    allowed_iifs_.push_back(link_name);
    return true;
  }
  return false;
}

void VPNProvider::RemoveService(VPNServiceRefPtr service) {
  const auto it = std::find(services_.begin(), services_.end(), service);
  if (it != services_.end()) {
    services_.erase(it);
  }
}

void VPNProvider::CreateServicesFromProfile(const ProfileRefPtr& profile) {
  SLOG(this, 2) << __func__;
  const StoreInterface* storage = profile->GetConstStorage();
  KeyValueStore args;
  args.Set<string>(kTypeProperty, kTypeVPN);
  for (const auto& group : storage->GetGroupsWithProperties(args)) {
    string type;
    string name;
    string host;
    if (!GetServiceParametersFromStorage(storage, group, &type, &name, &host,
                                         nullptr)) {
      continue;
    }

    VPNServiceRefPtr service = FindService(type, name, host);
    if (service != nullptr) {
      // If the service already exists, it does not need to be configured,
      // since PushProfile would have already called ConfigureService on it.
      SLOG(this, 2) << "Service already exists " << group;
      continue;
    }

    Error error;
    service = CreateService(type, name, group, &error);

    if (service == nullptr) {
      LOG(ERROR) << "Could not create service for " << group;
      continue;
    }

    if (!profile->ConfigureService(service)) {
      LOG(ERROR) << "Could not configure service for " << group;
      continue;
    }
  }
}

VPNServiceRefPtr VPNProvider::CreateServiceInner(const string& type,
                                                 const string& name,
                                                 const string& storage_id,
                                                 Error* error) {
  SLOG(this, 2) << __func__ << " type " << type << " name " << name
                << " storage id " << storage_id;
#if defined(DISABLE_VPN)

  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                        "VPN is not supported.");
  return nullptr;

#else

  std::unique_ptr<VPNDriver> driver;
  if (type == kProviderOpenVpn) {
    driver.reset(new OpenVPNDriver(manager_, ProcessManager::GetInstance()));
  } else if (type == kProviderL2tpIpsec) {
    driver.reset(new L2TPIPSecDriver(manager_, ProcessManager::GetInstance()));
  } else if (type == kProviderThirdPartyVpn) {
    // For third party VPN host contains extension ID
    driver.reset(
        new ThirdPartyVpnDriver(manager_, ProcessManager::GetInstance()));
  } else if (type == kProviderArcVpn) {
    driver.reset(new ArcVpnDriver(manager_, ProcessManager::GetInstance()));
  } else {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Unsupported VPN type: " + type);
    return nullptr;
  }

  VPNServiceRefPtr service = new VPNService(manager_, std::move(driver));
  service->set_storage_id(storage_id);
  service->InitDriverPropertyStore();
  if (!name.empty()) {
    service->set_friendly_name(name);
  }
  return service;

#endif  // DISABLE_VPN
}

VPNServiceRefPtr VPNProvider::CreateService(const string& type,
                                            const string& name,
                                            const string& storage_id,
                                            Error* error) {
  VPNServiceRefPtr service = CreateServiceInner(type, name, storage_id, error);
  if (service) {
    services_.push_back(service);
    manager_->RegisterService(service);
  }

  return service;
}

VPNServiceRefPtr VPNProvider::FindService(const string& type,
                                          const string& name,
                                          const string& host) const {
  for (const auto& service : services_) {
    if (type == service->driver()->GetProviderType() &&
        name == service->friendly_name() &&
        host == service->driver()->GetHost()) {
      return service;
    }
  }
  return nullptr;
}

ServiceRefPtr VPNProvider::CreateTemporaryService(const KeyValueStore& args,
                                                  Error* error) {
  string type;
  string name;
  string host;

  if (!GetServiceParametersFromArgs(args, &type, &name, &host, error)) {
    return nullptr;
  }

  string storage_id = VPNService::CreateStorageIdentifier(args, error);
  if (storage_id.empty()) {
    return nullptr;
  }

  return CreateServiceInner(type, name, storage_id, error);
}

ServiceRefPtr VPNProvider::CreateTemporaryServiceFromProfile(
    const ProfileRefPtr& profile, const std::string& entry_name, Error* error) {
  string type;
  string name;
  string host;
  if (!GetServiceParametersFromStorage(profile->GetConstStorage(), entry_name,
                                       &type, &name, &host, error)) {
    return nullptr;
  }

  return CreateServiceInner(type, name, entry_name, error);
}

bool VPNProvider::HasActiveService() const {
  for (const auto& service : services_) {
    if (service->IsConnecting() || service->IsConnected()) {
      return true;
    }
  }
  return false;
}

void VPNProvider::DisconnectAll() {
  for (const auto& service : services_) {
    if (service->IsConnecting() || service->IsConnected()) {
      service->Disconnect(nullptr, "user selected new config");
    }
  }
}

// TODO(crbug.com/1022028, b/154183305) Do not reuse IP properties object to
// carry routing policies related to VPN services. IP properties should be
// strictly layer 3 data only ideally.
void VPNProvider::SetDefaultRoutingPolicy(IPConfig::Properties* properties) {
  CHECK(!manager_->user_traffic_uids().empty());
  properties->allowed_uids = manager_->user_traffic_uids();
  properties->allowed_iifs = allowed_iifs_;
  properties->included_fwmarks = {kFwMarkRouteOnVpn};
}

}  // namespace shill
