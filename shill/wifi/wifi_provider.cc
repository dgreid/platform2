// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_provider.h"

#include <stdlib.h>

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/format_macros.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/key_value_store.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/net/byte_string.h"
#include "shill/net/ieee80211.h"
#include "shill/profile.h"
#include "shill/store_interface.h"
#include "shill/technology.h"
#include "shill/wifi/wifi_endpoint.h"
#include "shill/wifi/wifi_service.h"

using base::StringPrintf;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
static string ObjectID(WiFiProvider* w) {
  return "(wifi_provider)";
}
}  // namespace Logging

namespace {

// We used to store a few properties under this group entry, but they've been
// deprecated. Remove after M-88.
const char kWiFiProviderStorageId[] = "provider_of_wifi";

// Note that WiFiProvider generates some manager-level errors, because it
// implements the WiFi portion of the Manager.GetService flimflam API. The
// API is implemented here, rather than in manager, to keep WiFi-specific
// logic in the right place.
const char kManagerErrorSSIDRequired[] = "must specify SSID";
const char kManagerErrorSSIDTooLong[] = "SSID is too long";
const char kManagerErrorSSIDTooShort[] = "SSID is too short";
const char kManagerErrorUnsupportedSecurityClass[] =
    "security class is unsupported";
const char kManagerErrorUnsupportedServiceMode[] =
    "service mode is unsupported";

// Retrieve a WiFi service's identifying properties from passed-in |args|.
// Returns true if |args| are valid and populates |ssid|, |mode|,
// |security_class| and |hidden_ssid|, if successful.  Otherwise, this function
// returns false and populates |error| with the reason for failure.  It
// is a fatal error if the "Type" parameter passed in |args| is not kWiFi.
bool GetServiceParametersFromArgs(const KeyValueStore& args,
                                  vector<uint8_t>* ssid_bytes,
                                  string* mode,
                                  string* security_class,
                                  bool* hidden_ssid,
                                  Error* error) {
  CHECK_EQ(args.Lookup<string>(kTypeProperty, ""), kTypeWifi);

  string mode_test = args.Lookup<string>(kModeProperty, kModeManaged);
  if (!WiFiService::IsValidMode(mode_test)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          kManagerErrorUnsupportedServiceMode);
    return false;
  }

  vector<uint8_t> ssid;
  if (args.Contains<string>(kWifiHexSsid)) {
    string ssid_hex_string = args.Get<string>(kWifiHexSsid);
    if (!base::HexStringToBytes(ssid_hex_string, &ssid)) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                            "Hex SSID parameter is not valid");
      return false;
    }
  } else if (args.Contains<string>(kSSIDProperty)) {
    string ssid_string = args.Get<string>(kSSIDProperty);
    ssid = vector<uint8_t>(ssid_string.begin(), ssid_string.end());
  } else {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          kManagerErrorSSIDRequired);
    return false;
  }

  if (ssid.size() < 1) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidNetworkName,
                          kManagerErrorSSIDTooShort);
    return false;
  }

  if (ssid.size() > IEEE_80211::kMaxSSIDLen) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidNetworkName,
                          kManagerErrorSSIDTooLong);
    return false;
  }

  if (args.Contains<string>(kSecurityProperty)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unexpected Security property");
    return false;
  }

  const string kDefaultSecurity = kSecurityNone;
  if (args.Contains<string>(kSecurityClassProperty)) {
    string security_class_test =
        args.Lookup<string>(kSecurityClassProperty, kDefaultSecurity);
    if (!WiFiService::IsValidSecurityClass(security_class_test)) {
      Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                            kManagerErrorUnsupportedSecurityClass);
      return false;
    }
    *security_class = security_class_test;
  } else {
    *security_class = kDefaultSecurity;
  }

  *ssid_bytes = ssid;
  *mode = mode_test;

  // If the caller hasn't specified otherwise, we assume it is a hidden service.
  *hidden_ssid = args.Lookup<bool>(kWifiHiddenSsid, true);

  return true;
}

// Retrieve a WiFi service's identifying properties from passed-in |storage|.
// Return true if storage contain valid parameter values and populates |ssid|,
// |mode|, |security_class| and |hidden_ssid|. Otherwise, this function returns
// false and populates |error| with the reason for failure.
bool GetServiceParametersFromStorage(const StoreInterface* storage,
                                     const std::string& entry_name,
                                     std::vector<uint8_t>* ssid_bytes,
                                     std::string* mode,
                                     std::string* security_class,
                                     bool* hidden_ssid,
                                     Error* error) {
  // Verify service type.
  string type;
  if (!storage->GetString(entry_name, WiFiService::kStorageType, &type) ||
      type != kTypeWifi) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unspecified or invalid network type");
    return false;
  }

  string ssid_hex;
  if (!storage->GetString(entry_name, WiFiService::kStorageSSID, &ssid_hex) ||
      !base::HexStringToBytes(ssid_hex, ssid_bytes)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unspecified or invalid SSID");
    return false;
  }

  if (!storage->GetString(entry_name, WiFiService::kStorageMode, mode) ||
      mode->empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Network mode not specified");
    return false;
  }

  if (!storage->GetString(entry_name, WiFiService::kStorageSecurityClass,
                          security_class) ||
      !WiFiService::IsValidSecurityClass(*security_class)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unspecified or invalid security class");
    return false;
  }

  if (!storage->GetBool(entry_name, WiFiService::kStorageHiddenSSID,
                        hidden_ssid)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Hidden SSID not specified");
    return false;
  }
  return true;
}

}  // namespace

WiFiProvider::WiFiProvider(Manager* manager)
    : manager_(manager),
      running_(false),
      disable_vht_(false) {}

WiFiProvider::~WiFiProvider() = default;

void WiFiProvider::Start() {
  running_ = true;
}

void WiFiProvider::Stop() {
  SLOG(this, 2) << __func__;
  while (!services_.empty()) {
    WiFiServiceRefPtr service = services_.back();
    ForgetService(service);
    SLOG(this, 3) << "WiFiProvider deregistering service "
                  << service->log_name();
    manager_->DeregisterService(service);
  }
  service_by_endpoint_.clear();
  running_ = false;
}

void WiFiProvider::CreateServicesFromProfile(const ProfileRefPtr& profile) {
  const StoreInterface* storage = profile->GetConstStorage();
  KeyValueStore args;
  args.Set<string>(kTypeProperty, kTypeWifi);
  bool created_hidden_service = false;
  for (const auto& group : storage->GetGroupsWithProperties(args)) {
    vector<uint8_t> ssid_bytes;
    string network_mode;
    string security_class;
    bool is_hidden = false;
    if (!GetServiceParametersFromStorage(storage, group, &ssid_bytes,
                                         &network_mode, &security_class,
                                         &is_hidden, nullptr)) {
      continue;
    }

    if (FindService(ssid_bytes, network_mode, security_class)) {
      // If service already exists, we have nothing to do, since the
      // service has already loaded its configuration from storage.
      // This is guaranteed to happen in the single case where
      // CreateServicesFromProfile() is called on a WiFiProvider from
      // Manager::PushProfile():
      continue;
    }

    AddService(ssid_bytes, network_mode, security_class, is_hidden);

    // By registering the service in AddService, the rest of the configuration
    // will be loaded from the profile into the service via ConfigureService().

    if (is_hidden) {
      created_hidden_service = true;
    }
  }

  // If WiFi is unconnected and we created a hidden service as a result
  // of opening the profile, we should initiate a WiFi scan, which will
  // allow us to find any hidden services that we may have created.
  if (created_hidden_service &&
      !manager_->IsTechnologyConnected(Technology::kWifi)) {
    Error unused_error;
    manager_->RequestScan(kTypeWifi, &unused_error);
  }

  ReportRememberedNetworkCount();

  // Only report service source metrics when a user profile is pushed.
  // This ensures that we have an equal number of samples for the
  // default profile and user profiles.
  if (!profile->IsDefault()) {
    ReportServiceSourceMetrics();
  }
}

ServiceRefPtr WiFiProvider::FindSimilarService(const KeyValueStore& args,
                                               Error* error) const {
  vector<uint8_t> ssid;
  string mode;
  string security_class;
  bool hidden_ssid;

  if (!GetServiceParametersFromArgs(args, &ssid, &mode, &security_class,
                                    &hidden_ssid, error)) {
    return nullptr;
  }

  WiFiServiceRefPtr service(FindService(ssid, mode, security_class));
  if (!service) {
    error->Populate(Error::kNotFound, "Matching service was not found");
  }

  return service;
}

ServiceRefPtr WiFiProvider::CreateTemporaryService(const KeyValueStore& args,
                                                   Error* error) {
  vector<uint8_t> ssid;
  string mode;
  string security_class;
  bool hidden_ssid;

  if (!GetServiceParametersFromArgs(args, &ssid, &mode, &security_class,
                                    &hidden_ssid, error)) {
    return nullptr;
  }

  return new WiFiService(manager_, this, ssid, mode, security_class,
                         hidden_ssid);
}

ServiceRefPtr WiFiProvider::CreateTemporaryServiceFromProfile(
    const ProfileRefPtr& profile, const std::string& entry_name, Error* error) {
  vector<uint8_t> ssid;
  string mode;
  string security_class;
  bool hidden_ssid;
  if (!GetServiceParametersFromStorage(profile->GetConstStorage(), entry_name,
                                       &ssid, &mode, &security_class,
                                       &hidden_ssid, error)) {
    return nullptr;
  }
  return new WiFiService(manager_, this, ssid, mode, security_class,
                         hidden_ssid);
}

ServiceRefPtr WiFiProvider::GetService(const KeyValueStore& args,
                                       Error* error) {
  return GetWiFiService(args, error);
}

WiFiServiceRefPtr WiFiProvider::GetWiFiService(const KeyValueStore& args,
                                               Error* error) {
  vector<uint8_t> ssid_bytes;
  string mode;
  string security_class;
  bool hidden_ssid;

  if (!GetServiceParametersFromArgs(args, &ssid_bytes, &mode, &security_class,
                                    &hidden_ssid, error)) {
    return nullptr;
  }

  WiFiServiceRefPtr service(FindService(ssid_bytes, mode, security_class));
  if (!service) {
    service = AddService(ssid_bytes, mode, security_class, hidden_ssid);
  }

  return service;
}

WiFiServiceRefPtr WiFiProvider::FindServiceForEndpoint(
    const WiFiEndpointConstRefPtr& endpoint) {
  EndpointServiceMap::iterator service_it =
      service_by_endpoint_.find(endpoint.get());
  if (service_it == service_by_endpoint_.end())
    return nullptr;
  return service_it->second;
}

void WiFiProvider::OnEndpointAdded(const WiFiEndpointConstRefPtr& endpoint) {
  if (!running_) {
    return;
  }

  WiFiServiceRefPtr service = FindService(
      endpoint->ssid(), endpoint->network_mode(), endpoint->security_mode());
  if (!service) {
    const bool hidden_ssid = false;
    service =
        AddService(endpoint->ssid(), endpoint->network_mode(),
                   WiFiService::ComputeSecurityClass(endpoint->security_mode()),
                   hidden_ssid);
  }

  service->AddEndpoint(endpoint);
  service_by_endpoint_[endpoint.get()] = service;

  SLOG(this, 1) << "Assigned endpoint " << endpoint->bssid_string()
                << " to service " << service->log_name() << ".";

  manager_->UpdateService(service);
}

WiFiServiceRefPtr WiFiProvider::OnEndpointRemoved(
    const WiFiEndpointConstRefPtr& endpoint) {
  if (!running_) {
    return nullptr;
  }

  WiFiServiceRefPtr service = FindServiceForEndpoint(endpoint);

  CHECK(service) << "Can't find Service for Endpoint "
                 << "(with BSSID " << endpoint->bssid_string() << ").";
  SLOG(this, 1) << "Removing endpoint " << endpoint->bssid_string()
                << " from Service " << service->log_name();
  service->RemoveEndpoint(endpoint);
  service_by_endpoint_.erase(endpoint.get());

  if (service->HasEndpoints() || service->IsRemembered()) {
    // Keep services around if they are in a profile or have remaining
    // endpoints.
    manager_->UpdateService(service);
    return nullptr;
  }

  ForgetService(service);
  manager_->DeregisterService(service);

  return service;
}

void WiFiProvider::OnEndpointUpdated(const WiFiEndpointConstRefPtr& endpoint) {
  if (!running_) {
    return;
  }

  WiFiService* service = FindServiceForEndpoint(endpoint).get();
  CHECK(service);

  // If the service still matches the endpoint in its new configuration,
  // we need only to update the service.
  if (service->ssid() == endpoint->ssid() &&
      service->mode() == endpoint->network_mode() &&
      service->IsSecurityMatch(endpoint->security_mode())) {
    service->NotifyEndpointUpdated(endpoint);
    return;
  }

  // The endpoint no longer matches the associated service.  Remove the
  // endpoint, so current references to the endpoint are reset, then add
  // it again so it can be associated with a new service.
  OnEndpointRemoved(endpoint);
  OnEndpointAdded(endpoint);
}

bool WiFiProvider::OnServiceUnloaded(const WiFiServiceRefPtr& service) {
  // If the service still has endpoints, it should remain in the service list.
  if (service->HasEndpoints()) {
    return false;
  }

  // This is the one place where we forget the service but do not also
  // deregister the service with the manager.  However, by returning
  // true below, the manager will do so itself.
  ForgetService(service);
  return true;
}

void WiFiProvider::UpdateStorage(Profile* profile) {
  CHECK(profile);
  StoreInterface* storage = profile->GetStorage();
  // We stored this only to the default profile, but no reason not to delete it
  // from any profile it exists in.
  // Remove after M-88.
  storage->DeleteGroup(kWiFiProviderStorageId);
}

WiFiServiceRefPtr WiFiProvider::AddService(const vector<uint8_t>& ssid,
                                           const string& mode,
                                           const string& security_class,
                                           bool is_hidden) {
  WiFiServiceRefPtr service =
      new WiFiService(manager_, this, ssid, mode, security_class, is_hidden);

  services_.push_back(service);
  manager_->RegisterService(service);
  return service;
}

WiFiServiceRefPtr WiFiProvider::FindService(const vector<uint8_t>& ssid,
                                            const string& mode,
                                            const string& security) const {
  for (const auto& service : services_) {
    if (service->ssid() == ssid && service->mode() == mode &&
        service->IsSecurityMatch(security)) {
      return service;
    }
  }
  return nullptr;
}

ByteArrays WiFiProvider::GetHiddenSSIDList() {
  // Create a unique set of hidden SSIDs.
  std::set<ByteArray> hidden_ssids_set;
  for (const auto& service : services_) {
    if (service->hidden_ssid() && service->IsRemembered()) {
      hidden_ssids_set.insert(service->ssid());
    }
  }
  SLOG(this, 2) << "Found " << hidden_ssids_set.size() << " hidden services";
  return ByteArrays(hidden_ssids_set.begin(), hidden_ssids_set.end());
}

void WiFiProvider::ForgetService(const WiFiServiceRefPtr& service) {
  vector<WiFiServiceRefPtr>::iterator it;
  it = std::find(services_.begin(), services_.end(), service);
  if (it == services_.end()) {
    return;
  }
  (*it)->ResetWiFi();
  services_.erase(it);
}

void WiFiProvider::ReportRememberedNetworkCount() {
  metrics()->SendToUMA(
      Metrics::kMetricRememberedWiFiNetworkCount,
      std::count_if(services_.begin(), services_.end(),
                    [](ServiceRefPtr s) { return s->IsRemembered(); }),
      Metrics::kMetricRememberedWiFiNetworkCountMin,
      Metrics::kMetricRememberedWiFiNetworkCountMax,
      Metrics::kMetricRememberedWiFiNetworkCountNumBuckets);
}

void WiFiProvider::ReportServiceSourceMetrics() {
  for (const auto& security_mode :
       {kSecurityNone, kSecurityWep, kSecurityPsk, kSecurity8021x}) {
    metrics()->SendToUMA(
        base::StringPrintf(
            Metrics::
                kMetricRememberedSystemWiFiNetworkCountBySecurityModeFormat,
            security_mode),
        std::count_if(services_.begin(), services_.end(),
                      [security_mode](WiFiServiceRefPtr s) {
                        return s->IsRemembered() &&
                               s->IsSecurityMatch(security_mode) &&
                               s->profile()->IsDefault();
                      }),
        Metrics::kMetricRememberedWiFiNetworkCountMin,
        Metrics::kMetricRememberedWiFiNetworkCountMax,
        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets);
    metrics()->SendToUMA(
        base::StringPrintf(
            Metrics::kMetricRememberedUserWiFiNetworkCountBySecurityModeFormat,
            security_mode),
        std::count_if(services_.begin(), services_.end(),
                      [security_mode](WiFiServiceRefPtr s) {
                        return s->IsRemembered() &&
                               s->IsSecurityMatch(security_mode) &&
                               !s->profile()->IsDefault();
                      }),
        Metrics::kMetricRememberedWiFiNetworkCountMin,
        Metrics::kMetricRememberedWiFiNetworkCountMax,
        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets);
  }
}

void WiFiProvider::ReportAutoConnectableServices() {
  int num_services = NumAutoConnectableServices();
  // Only report stats when there are wifi services available.
  if (num_services) {
    metrics()->NotifyWifiAutoConnectableServices(num_services);
  }
}

int WiFiProvider::NumAutoConnectableServices() {
  const char* reason = nullptr;
  int num_services = 0;
  // Determine the number of services available for auto-connect.
  for (const auto& service : services_) {
    // Service is available for auto connect if it is configured for auto
    // connect, and is auto-connectable.
    if (service->auto_connect() && service->IsAutoConnectable(&reason)) {
      num_services++;
    }
  }
  return num_services;
}

vector<ByteString> WiFiProvider::GetSsidsConfiguredForAutoConnect() {
  vector<ByteString> results;
  for (const auto& service : services_) {
    if (service->auto_connect()) {
      // Service configured for auto-connect.
      ByteString ssid_bytes(service->ssid());
      results.push_back(ssid_bytes);
    }
  }
  return results;
}

Metrics* WiFiProvider::metrics() const {
  return manager_->metrics();
}

}  // namespace shill
