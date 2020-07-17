// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/default_profile.h"

#include <random>

#include <base/files/file_path.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/link_monitor.h"
#include "shill/manager.h"
#include "shill/portal_detector.h"
#if !defined(DISABLE_WIFI)
#include "shill/property_accessor.h"
#endif  // DISABLE_WIFI
#include "shill/resolver.h"
#include "shill/store_interface.h"

namespace shill {

namespace {
// OfflineMode was removed in crrev.com/c/2202196.
// This was left here to remove OfflineMode entries from profiles.
const char kStorageOfflineMode[] = "OfflineMode";
}  // namespace

// static
const char DefaultProfile::kDefaultId[] = "default";
// static
const char DefaultProfile::kStorageId[] = "global";
// static
const char DefaultProfile::kStorageArpGateway[] = "ArpGateway";
// static
const char DefaultProfile::kStorageCheckPortalList[] = "CheckPortalList";
// static
const char DefaultProfile::kStorageConnectionIdSalt[] = "ConnectionIdSalt";
// static
const char DefaultProfile::kStorageIgnoredDNSSearchPaths[] =
    "IgnoredDNSSearchPaths";
// static
const char DefaultProfile::kStorageLinkMonitorTechnologies[] =
    "LinkMonitorTechnologies";
// static
const char DefaultProfile::kStorageName[] = "Name";
// static
const char DefaultProfile::kStorageNoAutoConnectTechnologies[] =
    "NoAutoConnectTechnologies";
// static
const char DefaultProfile::kStorageProhibitedTechnologies[] =
    "ProhibitedTechnologies";
#if !defined(DISABLE_WIFI)
// static
const char DefaultProfile::kStorageWifiGlobalFTEnabled[] =
    "WiFi.GlobalFTEnabled";
#endif  // DISABLE_WIFI

DefaultProfile::DefaultProfile(Manager* manager,
                               const base::FilePath& storage_directory,
                               const std::string& profile_id,
                               const Manager::Properties& manager_props)
    : Profile(manager, Identifier(profile_id), storage_directory, true),
      profile_id_(profile_id),
      props_(manager_props),
      random_engine_(time(nullptr)) {
  PropertyStore* store = this->mutable_store();
  store->RegisterConstBool(kArpGatewayProperty, &manager_props.arp_gateway);
  store->RegisterConstString(kCheckPortalListProperty,
                             &manager_props.check_portal_list);
  store->RegisterConstString(kIgnoredDNSSearchPathsProperty,
                             &manager_props.ignored_dns_search_paths);
  store->RegisterConstString(kLinkMonitorTechnologiesProperty,
                             &manager_props.link_monitor_technologies);
  store->RegisterConstString(kNoAutoConnectTechnologiesProperty,
                             &manager_props.no_auto_connect_technologies);
  store->RegisterConstString(kProhibitedTechnologiesProperty,
                             &manager_props.prohibited_technologies);
#if !defined(DISABLE_WIFI)
  HelpRegisterConstDerivedBool(kWifiGlobalFTEnabledProperty,
                               &DefaultProfile::GetFTEnabled);
#endif  // DISABLE_WIFI
  set_persistent_profile_path(
      GetFinalStoragePath(storage_directory, Identifier(profile_id)));
}

DefaultProfile::~DefaultProfile() = default;

#if !defined(DISABLE_WIFI)
void DefaultProfile::HelpRegisterConstDerivedBool(
    const std::string& name, bool (DefaultProfile::*get)(Error*)) {
  this->mutable_store()->RegisterDerivedBool(
      name, BoolAccessor(new CustomAccessor<DefaultProfile, bool>(
                this, get, nullptr, nullptr)));
}

bool DefaultProfile::GetFTEnabled(Error* error) {
  return manager()->GetFTEnabled(error);
}
#endif  // DISABLE_WIFI

void DefaultProfile::LoadManagerProperties(Manager::Properties* manager_props,
                                           DhcpProperties* dhcp_properties) {
  storage()->GetBool(kStorageId, kStorageArpGateway,
                     &manager_props->arp_gateway);
  if (!storage()->GetString(kStorageId, kStorageCheckPortalList,
                            &manager_props->check_portal_list)) {
    manager_props->check_portal_list = PortalDetector::kDefaultCheckPortalList;
  }
  if (!storage()->GetInt(kStorageId, kStorageConnectionIdSalt,
                         &manager_props->connection_id_salt)) {
    manager_props->connection_id_salt =
        std::uniform_int_distribution<int>()(random_engine_);
  }
  if (!storage()->GetString(kStorageId, kStorageIgnoredDNSSearchPaths,
                            &manager_props->ignored_dns_search_paths)) {
    manager_props->ignored_dns_search_paths =
        Resolver::kDefaultIgnoredSearchList;
  }
  if (!storage()->GetString(kStorageId, kStorageLinkMonitorTechnologies,
                            &manager_props->link_monitor_technologies)) {
    manager_props->link_monitor_technologies =
        LinkMonitor::kDefaultLinkMonitorTechnologies;
  }
  if (!storage()->GetString(kStorageId, kStorageNoAutoConnectTechnologies,
                            &manager_props->no_auto_connect_technologies)) {
    manager_props->no_auto_connect_technologies = "";
  }

  // This used to be loaded from the default profile, but now it is fixed.
  manager_props->portal_http_url = PortalDetector::kDefaultHttpUrl;
  manager_props->portal_https_url = PortalDetector::kDefaultHttpsUrl;
  manager_props->portal_fallback_http_urls =
      PortalDetector::kDefaultFallbackHttpUrls;

  if (!storage()->GetString(kStorageId, kStorageProhibitedTechnologies,
                            &manager_props->prohibited_technologies)) {
    manager_props->prohibited_technologies = "";
  }
#if !defined(DISABLE_WIFI)
  bool ft_enabled;
  if (storage()->GetBool(kStorageId, kStorageWifiGlobalFTEnabled,
                         &ft_enabled)) {
    manager_props->ft_enabled = ft_enabled;
  }
#endif  // DISABLE_WIFI

  dhcp_properties->Load(storage(), kStorageId);
}

bool DefaultProfile::ConfigureService(const ServiceRefPtr& service) {
  if (Profile::ConfigureService(service)) {
    return true;
  }
  if (service->technology() == Technology::kEthernet) {
    // Ethernet services should have an affinity towards the default profile,
    // so even if a new Ethernet service has no known configuration, accept
    // it anyway.
    UpdateService(service);
    service->SetProfile(this);
    return true;
  }
  return false;
}

bool DefaultProfile::Save() {
  // OfflineMode was removed in crrev.com/c/2202196.
  storage()->DeleteKey(kStorageId, kStorageOfflineMode);

  storage()->SetBool(kStorageId, kStorageArpGateway, props_.arp_gateway);
  storage()->SetString(kStorageId, kStorageName, GetFriendlyName());
  storage()->SetString(kStorageId, kStorageCheckPortalList,
                       props_.check_portal_list);
  storage()->SetInt(kStorageId, kStorageConnectionIdSalt,
                    props_.connection_id_salt);
  storage()->SetString(kStorageId, kStorageIgnoredDNSSearchPaths,
                       props_.ignored_dns_search_paths);
  storage()->SetString(kStorageId, kStorageLinkMonitorTechnologies,
                       props_.link_monitor_technologies);
  storage()->SetString(kStorageId, kStorageNoAutoConnectTechnologies,
                       props_.no_auto_connect_technologies);
  storage()->SetString(kStorageId, kStorageProhibitedTechnologies,
                       props_.prohibited_technologies);
#if !defined(DISABLE_WIFI)
  if (props_.ft_enabled.has_value()) {
    storage()->SetBool(kStorageId, kStorageWifiGlobalFTEnabled,
                       props_.ft_enabled.value());
  }
#endif  // DISABLE_WIFI
  manager()->dhcp_properties().Save(storage(), kStorageId);
  return Profile::Save();
}

bool DefaultProfile::UpdateDevice(const DeviceRefPtr& device) {
  return device->Save(storage()) && storage()->Flush();
}

}  // namespace shill
