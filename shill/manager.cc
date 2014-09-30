// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/manager.h"

#include <stdio.h>
#include <time.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/memory/ref_counted.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/callbacks.h"
#include "shill/connection.h"
#include "shill/control_interface.h"
#include "shill/dbus_adaptor.h"
#include "shill/dbus_manager.h"
#include "shill/default_profile.h"
#include "shill/device.h"
#include "shill/device_info.h"
#include "shill/ephemeral_profile.h"
#include "shill/error.h"
#include "shill/ethernet_eap_provider.h"
#include "shill/ethernet_eap_service.h"
#include "shill/event_dispatcher.h"
#include "shill/geolocation_info.h"
#include "shill/hook_table.h"
#include "shill/ip_address_store.h"
#include "shill/key_file_store.h"
#include "shill/logging.h"
#include "shill/profile.h"
#include "shill/property_accessor.h"
#include "shill/proxy_factory.h"
#include "shill/resolver.h"
#include "shill/result_aggregator.h"
#include "shill/service.h"
#include "shill/service_sorter.h"
#include "shill/vpn_provider.h"
#include "shill/vpn_service.h"
#include "shill/wifi.h"
#include "shill/wifi_provider.h"
#include "shill/wifi_service.h"
#include "shill/wimax_service.h"

using base::Bind;
using base::FilePath;
using base::StringPrintf;
using base::Unretained;
using std::map;
using std::set;
using std::string;
using std::vector;

namespace shill {

// statics
const char Manager::kErrorNoDevice[] = "no wifi devices available";
const char Manager::kErrorTypeRequired[] = "must specify service type";
const char Manager::kErrorUnsupportedServiceType[] =
    "service type is unsupported";
// This timeout should be less than the upstart job timeout, otherwise
// stats for termination actions might be lost.
const int Manager::kTerminationActionsTimeoutMilliseconds = 9500;

// Device status check interval (every 3 minutes).
const int Manager::kDeviceStatusCheckIntervalMilliseconds = 180000;

// static
const char *Manager::kProbeTechnologies[] = {
    kTypeEthernet,
    kTypeWifi,
    kTypeWimax,
    kTypeCellular
};

Manager::Manager(ControlInterface *control_interface,
                 EventDispatcher *dispatcher,
                 Metrics *metrics,
                 GLib *glib,
                 const string &run_directory,
                 const string &storage_directory,
                 const string &user_storage_directory)
    : dispatcher_(dispatcher),
      run_path_(FilePath(run_directory)),
      storage_path_(FilePath(storage_directory)),
      user_storage_path_(user_storage_directory),
      user_profile_list_path_(FilePath(Profile::kUserProfileListPathname)),
      adaptor_(control_interface->CreateManagerAdaptor(this)),
      device_info_(control_interface, dispatcher, metrics, this),
#if !defined(DISABLE_CELLULAR)
      modem_info_(control_interface, dispatcher, metrics, this, glib),
#endif  // DISABLE_CELLULAR
      ethernet_eap_provider_(
          new EthernetEapProvider(
              control_interface, dispatcher, metrics, this)),
      vpn_provider_(
          new VPNProvider(control_interface, dispatcher, metrics, this)),
      wifi_provider_(
          new WiFiProvider(control_interface, dispatcher, metrics, this)),
#if !defined(DISABLE_WIMAX)
      wimax_provider_(
          new WiMaxProvider(control_interface, dispatcher, metrics, this)),
#endif  // DISABLE_WIMAX
      resolver_(Resolver::GetInstance()),
      running_(false),
      connect_profiles_to_rpc_(true),
      ephemeral_profile_(
          new EphemeralProfile(control_interface, metrics, this)),
      control_interface_(control_interface),
      metrics_(metrics),
      glib_(glib),
      use_startup_portal_list_(false),
      device_status_check_task_(Bind(&Manager::DeviceStatusCheckTask,
                                     base::Unretained(this))),
      termination_actions_(dispatcher),
      suspend_delay_registered_(false),
      is_wake_on_lan_enabled_(true),
      default_service_callback_tag_(0),
      crypto_util_proxy_(new CryptoUtilProxy(dispatcher, glib)),
      health_checker_remote_ips_(new IPAddressStore()) {
  HelpRegisterDerivedString(kActiveProfileProperty,
                            &Manager::GetActiveProfileRpcIdentifier,
                            NULL);
  store_.RegisterBool(kArpGatewayProperty, &props_.arp_gateway);
  HelpRegisterConstDerivedStrings(kAvailableTechnologiesProperty,
                                  &Manager::AvailableTechnologies);
  HelpRegisterDerivedString(kCheckPortalListProperty,
                            &Manager::GetCheckPortalList,
                            &Manager::SetCheckPortalList);
  HelpRegisterConstDerivedStrings(kConnectedTechnologiesProperty,
                                  &Manager::ConnectedTechnologies);
  store_.RegisterConstString(kConnectionStateProperty, &connection_state_);
  store_.RegisterString(kCountryProperty, &props_.country);
  HelpRegisterDerivedString(kDefaultTechnologyProperty,
                            &Manager::DefaultTechnology,
                            NULL);
  HelpRegisterConstDerivedRpcIdentifier(
      kDefaultServiceProperty, &Manager::GetDefaultServiceRpcIdentifier);
  HelpRegisterConstDerivedRpcIdentifiers(kDevicesProperty,
                                         &Manager::EnumerateDevices);
  HelpRegisterDerivedBool(kDisableWiFiVHTProperty,
                          &Manager::GetDisableWiFiVHT,
                          &Manager::SetDisableWiFiVHT);
  HelpRegisterConstDerivedStrings(kEnabledTechnologiesProperty,
                                  &Manager::EnabledTechnologies);
  HelpRegisterDerivedString(kIgnoredDNSSearchPathsProperty,
                            &Manager::GetIgnoredDNSSearchPaths,
                            &Manager::SetIgnoredDNSSearchPaths);
  store_.RegisterString(kLinkMonitorTechnologiesProperty,
                        &props_.link_monitor_technologies);
  store_.RegisterString(kNoAutoConnectTechnologiesProperty,
                        &props_.no_auto_connect_technologies);
  store_.RegisterBool(kOfflineModeProperty, &props_.offline_mode);
  store_.RegisterString(kPortalURLProperty, &props_.portal_url);
  store_.RegisterInt32(kPortalCheckIntervalProperty,
                       &props_.portal_check_interval_seconds);
  HelpRegisterConstDerivedRpcIdentifiers(kProfilesProperty,
                                         &Manager::EnumerateProfiles);
  store_.RegisterString(kHostNameProperty, &props_.host_name);
  HelpRegisterDerivedString(kStateProperty,
                            &Manager::CalculateState,
                            NULL);
  HelpRegisterConstDerivedRpcIdentifiers(kServicesProperty,
                                         &Manager::EnumerateAvailableServices);
  HelpRegisterConstDerivedRpcIdentifiers(kServiceCompleteListProperty,
                                         &Manager::EnumerateCompleteServices);
  HelpRegisterConstDerivedRpcIdentifiers(kServiceWatchListProperty,
                                         &Manager::EnumerateWatchedServices);
  HelpRegisterConstDerivedStrings(kUninitializedTechnologiesProperty,
                                  &Manager::UninitializedTechnologies);
  store_.RegisterBool(kWakeOnLanEnabledProperty, &is_wake_on_lan_enabled_);

  // Set default technology order "by hand", to avoid invoking side
  // effects of SetTechnologyOrder.
  technology_order_.push_back(Technology::IdentifierFromName(kTypeVPN));
  technology_order_.push_back(Technology::IdentifierFromName(kTypeEthernet));
  technology_order_.push_back(Technology::IdentifierFromName(kTypeWifi));
  technology_order_.push_back(Technology::IdentifierFromName(kTypeWimax));
  technology_order_.push_back(Technology::IdentifierFromName(kTypeCellular));

  UpdateProviderMapping();

  SLOG(Manager, 2) << "Manager initialized.";
}

Manager::~Manager() {}

void Manager::AddDeviceToBlackList(const string &device_name) {
  device_info_.AddDeviceToBlackList(device_name);
}

void Manager::Start() {
  LOG(INFO) << "Manager started.";

  dbus_manager_.reset(new DBusManager());
  dbus_manager_->Start();

  power_manager_.reset(
      new PowerManager(dispatcher_, ProxyFactory::GetInstance()));
  power_manager_->Start(dbus_manager(),
                        base::TimeDelta::FromMilliseconds(
                            kTerminationActionsTimeoutMilliseconds),
                        Bind(&Manager::OnSuspendImminent, AsWeakPtr()),
                        Bind(&Manager::OnSuspendDone, AsWeakPtr()),
                        Bind(&Manager::OnDarkSuspendImminent, AsWeakPtr()));

  CHECK(base::CreateDirectory(run_path_)) << run_path_.value();
  resolver_->set_path(run_path_.Append("resolv.conf"));

  InitializeProfiles();
  running_ = true;
  adaptor_->UpdateRunning();
  device_info_.Start();
#if !defined(DISABLE_CELLULAR)
  modem_info_.Start();
#endif  // DISABLE_CELLULAR
  for (const auto &provider_mapping : providers_) {
    provider_mapping.second->Start();
  }

  // Start task for checking connection status.
  dispatcher_->PostDelayedTask(device_status_check_task_.callback(),
                               kDeviceStatusCheckIntervalMilliseconds);
}

void Manager::Stop() {
  running_ = false;
  // Persist device information to disk;
  for (const auto &device : devices_) {
    UpdateDevice(device);
  }

  UpdateWiFiProvider();

  // Persist profile, service information to disk.
  for (const auto &profile : profiles_) {
    // Since this happens in a loop, the current manager state is stored to
    // all default profiles in the stack.  This is acceptable because the
    // only time multiple default profiles are loaded are during autotests.
    profile->Save();
  }

  Error e;
  for (const auto &service : services_) {
    service->Disconnect(&e, __func__);
  }

  adaptor_->UpdateRunning();
  for (const auto &provider_mapping : providers_) {
    provider_mapping.second->Stop();
  }
#if !defined(DISABLE_CELLULAR)
  modem_info_.Stop();
#endif  // DISABLE_CELLULAR
  device_info_.Stop();
  device_status_check_task_.Cancel();
  sort_services_task_.Cancel();
  power_manager_->Stop();
  power_manager_.reset();
  dbus_manager_.reset();
}

void Manager::InitializeProfiles() {
  DCHECK(profiles_.empty());  // The default profile must go first on stack.
  CHECK(base::CreateDirectory(storage_path_)) << storage_path_.value();

  // Ensure that we have storage for the default profile, and that
  // the persistent copy of the default profile is not corrupt.
  scoped_refptr<DefaultProfile>
      default_profile(new DefaultProfile(control_interface_,
                                         metrics_,
                                         this,
                                         storage_path_,
                                         DefaultProfile::kDefaultId,
                                         props_));
  // The default profile may fail to initialize if it's corrupted.
  // If so, recreate the default profile.
  if (!default_profile->InitStorage(
      glib_, Profile::kCreateOrOpenExisting, NULL))
    CHECK(default_profile->InitStorage(glib_, Profile::kCreateNew,
                                       NULL));
  // In case we created a new profile, initialize its default values,
  // and then save. This is required for properties such as
  // PortalDetector::kDefaultCheckPortalList to be initialized correctly.
  LoadProperties(default_profile);
  default_profile->Save();
  default_profile = NULL;  // PushProfileInternal will re-create.

  // Read list of user profiles. This must be done before pushing the
  // default profile, because modifying the profile stack updates the
  // user profile list.
  vector<Profile::Identifier> identifiers =
      Profile::LoadUserProfileList(user_profile_list_path_);

  // Push the default profile onto the stack.
  Error error;
  string path;
  Profile::Identifier default_profile_id;
  CHECK(Profile::ParseIdentifier(
      DefaultProfile::kDefaultId, &default_profile_id));
  PushProfileInternal(default_profile_id, &path, &error);
  CHECK(!profiles_.empty());  // Must have a default profile.

  // Push user profiles onto the stack.
  for (const auto &profile_id : identifiers)  {
    PushProfileInternal(profile_id, &path, &error);
  }
}

void Manager::CreateProfile(const string &name, string *path, Error *error) {
  SLOG(Manager, 2) << __func__ << " " << name;
  Profile::Identifier ident;
  if (!Profile::ParseIdentifier(name, &ident)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Invalid profile name " + name);
    return;
  }

  if (HasProfile(ident)) {
    Error::PopulateAndLog(error, Error::kAlreadyExists,
                          "Profile name " + name + " is already on stack");
    return;
  }

  ProfileRefPtr profile;
  if (ident.user.empty()) {
    profile = new DefaultProfile(control_interface_,
                                 metrics_,
                                 this,
                                 storage_path_,
                                 ident.identifier,
                                 props_);
  } else {
    profile = new Profile(control_interface_,
                          metrics_,
                          this,
                          ident,
                          user_storage_path_,
                          true);
  }

  if (!profile->InitStorage(glib_, Profile::kCreateNew, error)) {
    // |error| will have been populated by InitStorage().
    return;
  }

  // Save profile data out, and then let the scoped pointer fall out of scope.
  if (!profile->Save()) {
    Error::PopulateAndLog(error, Error::kInternalError,
                          "Profile name " + name + " could not be saved");
    return;
  }

  *path = profile->GetRpcIdentifier();
}

bool Manager::HasProfile(const Profile::Identifier &ident) {
  for (const auto &profile : profiles_) {
    if (profile->MatchesIdentifier(ident)) {
      return true;
    }
  }
  return false;
}

void Manager::PushProfileInternal(
    const Profile::Identifier &ident, string *path, Error *error) {
  if (HasProfile(ident)) {
    Error::PopulateAndLog(error, Error::kAlreadyExists,
                          "Profile name " + Profile::IdentifierToString(ident) +
                          " is already on stack");
    return;
  }

  ProfileRefPtr profile;
  if (ident.user.empty()) {
    // Allow a machine-wide-profile to be pushed on the stack only if the
    // profile stack is empty, or if the topmost profile on the stack is
    // also a machine-wide (non-user) profile.
    if (!profiles_.empty() && !profiles_.back()->GetUser().empty()) {
      Error::PopulateAndLog(error, Error::kInvalidArguments,
                            "Cannot load non-default global profile " +
                            Profile::IdentifierToString(ident) +
                            " on top of a user profile");
      return;
    }

    scoped_refptr<DefaultProfile>
        default_profile(new DefaultProfile(control_interface_,
                                           metrics_,
                                           this,
                                           storage_path_,
                                           ident.identifier,
                                           props_));
    if (!default_profile->InitStorage(glib_, Profile::kOpenExisting, NULL)) {
      LOG(ERROR) << "Failed to open default profile.";
      // Try to continue anyway, so that we can be useful in cases
      // where the disk is full.
      default_profile->InitStubStorage();
    }

    LoadProperties(default_profile);
    profile = default_profile;
  } else {
    profile = new Profile(control_interface_,
                          metrics_,
                          this,
                          ident,
                          user_storage_path_,
                          connect_profiles_to_rpc_);
    if (!profile->InitStorage(glib_, Profile::kOpenExisting, error)) {
      // |error| will have been populated by InitStorage().
      return;
    }
  }

  profiles_.push_back(profile);

  for (ServiceRefPtr &service : services_) {
    service->ClearExplicitlyDisconnected();

    // Offer each registered Service the opportunity to join this new Profile.
    if (profile->ConfigureService(service)) {
      LOG(INFO) << "(Re-)configured service " << service->unique_name()
                << " from new profile.";
    }
  }

  // Shop the Profile contents around to Devices which may have configuration
  // stored in these profiles.
  for (DeviceRefPtr &device : devices_) {
    profile->ConfigureDevice(device);
  }

  // Offer the Profile contents to the service providers which will
  // create new services if necessary.
  for (const auto &provider_mapping : providers_) {
    provider_mapping.second->CreateServicesFromProfile(profile);
  }

  *path = profile->GetRpcIdentifier();
  SortServices();
  OnProfilesChanged();
  LOG(INFO) << __func__ << " finished; " << profiles_.size()
            << " profile(s) now present.";
}

void Manager::PushProfile(const string &name, string *path, Error *error) {
  SLOG(Manager, 2) << __func__ << " " << name;
  Profile::Identifier ident;
  if (!Profile::ParseIdentifier(name, &ident)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Invalid profile name " + name);
    return;
  }
  PushProfileInternal(ident, path, error);
}

void Manager::InsertUserProfile(const string &name,
                                const string &user_hash,
                                string *path,
                                Error *error) {
  SLOG(Manager, 2) << __func__ << " " << name;
  Profile::Identifier ident;
  if (!Profile::ParseIdentifier(name, &ident) ||
      ident.user.empty()) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Invalid user profile name " + name);
    return;
  }
  ident.user_hash = user_hash;
  PushProfileInternal(ident, path, error);
}

void Manager::PopProfileInternal() {
  CHECK(!profiles_.empty());
  ProfileRefPtr active_profile = profiles_.back();
  profiles_.pop_back();
  for (auto it = services_.begin(); it != services_.end();) {
    (*it)->ClearExplicitlyDisconnected();
    if (IsServiceEphemeral(*it)) {
      // Not affected, since the EphemeralProfile isn't on the stack.
      // Not logged, since ephemeral services aren't that interesting.
      ++it;
      continue;
    }

    if ((*it)->profile().get() != active_profile.get()) {
      LOG(INFO) << "Skipping unload of service " << (*it)->unique_name()
                << ": wasn't using this profile.";
      ++it;
      continue;
    }

    if (MatchProfileWithService(*it)) {
      LOG(INFO) << "Skipping unload of service " << (*it)->unique_name()
                << ": re-configured from another profile.";
      ++it;
      continue;
    }

    if (!UnloadService(&it)) {
      LOG(INFO) << "Service " << (*it)->unique_name()
                << "not completely unloaded.";
      ++it;
      continue;
    }

    // Service was totally unloaded. No advance of iterator in this
    // case, as UnloadService has updated the iterator for us.
  }
  SortServices();
  OnProfilesChanged();
  LOG(INFO) << __func__ << " finished; " << profiles_.size()
            << " profile(s) still present.";
}

void Manager::OnProfilesChanged() {
  Error unused_error;

  adaptor_->EmitStringsChanged(kProfilesProperty,
                               EnumerateProfiles(&unused_error));
  Profile::SaveUserProfileList(user_profile_list_path_, profiles_);
}

void Manager::PopProfile(const string &name, Error *error) {
  SLOG(Manager, 2) << __func__ << " " << name;
  Profile::Identifier ident;
  if (profiles_.empty()) {
    Error::PopulateAndLog(error, Error::kNotFound, "Profile stack is empty");
    return;
  }
  ProfileRefPtr active_profile = profiles_.back();
  if (!Profile::ParseIdentifier(name, &ident)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Invalid profile name " + name);
    return;
  }
  if (!active_profile->MatchesIdentifier(ident)) {
    Error::PopulateAndLog(error, Error::kNotSupported,
                          name + " is not the active profile");
    return;
  }
  PopProfileInternal();
}

void Manager::PopAnyProfile(Error *error) {
  SLOG(Manager, 2) << __func__;
  Profile::Identifier ident;
  if (profiles_.empty()) {
    Error::PopulateAndLog(error, Error::kNotFound, "Profile stack is empty");
    return;
  }
  PopProfileInternal();
}

void Manager::PopAllUserProfiles(Error */*error*/) {
  SLOG(Manager, 2) << __func__;
  while (!profiles_.empty() && !profiles_.back()->GetUser().empty()) {
    PopProfileInternal();
  }
}

void Manager::RemoveProfile(const string &name, Error *error) {
  Profile::Identifier ident;
  if (!Profile::ParseIdentifier(name, &ident)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Invalid profile name " + name);
    return;
  }

  if (HasProfile(ident)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Cannot remove profile name " + name +
                          " since it is on stack");
    return;
  }

  ProfileRefPtr profile;
  if (ident.user.empty()) {
    profile = new DefaultProfile(control_interface_,
                                 metrics_,
                                 this,
                                 storage_path_,
                                 ident.identifier,
                                 props_);
  } else {
    profile = new Profile(control_interface_,
                          metrics_,
                          this,
                          ident,
                          user_storage_path_,
                          false);
  }


  // |error| will have been populated if RemoveStorage fails.
  profile->RemoveStorage(glib_, error);

  return;
}

void Manager::RemoveService(const ServiceRefPtr &service) {
  LOG(INFO) << __func__ << " for service " << service->unique_name();
  if (!IsServiceEphemeral(service)) {
    service->profile()->AbandonService(service);
    if (MatchProfileWithService(service)) {
      // We found another profile to adopt the service; no need to unload.
      UpdateService(service);
      return;
    }
  }
  auto service_it = std::find(services_.begin(), services_.end(), service);
  CHECK(service_it != services_.end());
  if (!UnloadService(&service_it)) {
    UpdateService(service);
  }
  SortServices();
}

bool Manager::HandleProfileEntryDeletion(const ProfileRefPtr &profile,
                                         const std::string &entry_name) {
  bool moved_services = false;
  for (auto it = services_.begin(); it != services_.end();) {
    if ((*it)->profile().get() == profile.get() &&
        (*it)->GetStorageIdentifier() == entry_name) {
      profile->AbandonService(*it);
      if (MatchProfileWithService(*it) ||
          !UnloadService(&it)) {
        ++it;
      }
      moved_services = true;
    } else {
      ++it;
    }
  }
  return moved_services;
}

map<string, string> Manager::GetLoadableProfileEntriesForService(
    const ServiceConstRefPtr &service) {
  map<string, string> profile_entries;
  for (const auto &profile : profiles_) {
    string entry_name = service->GetLoadableStorageIdentifier(
        *profile->GetConstStorage());
    if (!entry_name.empty()) {
      profile_entries[profile->GetRpcIdentifier()] = entry_name;
    }
  }
  return profile_entries;
}

ServiceRefPtr Manager::GetServiceWithStorageIdentifier(
    const ProfileRefPtr &profile, const std::string &entry_name, Error *error) {
  for (const auto &service : services_) {
    if (service->profile().get() == profile.get() &&
        service->GetStorageIdentifier() == entry_name) {
      return service;
    }
  }

  string error_string(
      StringPrintf("Entry %s is not registered in the manager",
                   entry_name.c_str()));
  if (error) {
    error->Populate(Error::kNotFound, error_string);
  }
  SLOG(Manager, 2) << error_string;
  return NULL;
}

ServiceRefPtr Manager::GetServiceWithGUID(
    const std::string &guid, Error *error) {
  for (const auto &service : services_) {
    if (service->guid() == guid) {
      return service;
    }
  }

  string error_string(
      StringPrintf("Service wth GUID %s is not registered in the manager",
                   guid.c_str()));
  if (error) {
    error->Populate(Error::kNotFound, error_string);
  }
  SLOG(Manager, 2) << error_string;
  return NULL;
}

ServiceRefPtr Manager::GetDefaultService() const {
  SLOG(Manager, 2) << __func__;
  if (services_.empty() || !services_[0]->connection().get()) {
    SLOG(Manager, 2) << "In " << __func__ << ": No default connection exists.";
    return NULL;
  }
  return services_[0];
}

RpcIdentifier Manager::GetDefaultServiceRpcIdentifier(Error */*error*/) {
  ServiceRefPtr default_service = GetDefaultService();
  return default_service ? default_service->GetRpcIdentifier() : "/";
}

bool Manager::IsTechnologyInList(const string &technology_list,
                                 Technology::Identifier tech) const {
  if (technology_list.empty())
    return false;

  Error error;
  vector<Technology::Identifier> technologies;
  return Technology::GetTechnologyVectorFromString(technology_list,
                                                   &technologies,
                                                   &error) &&
      std::find(technologies.begin(), technologies.end(), tech) !=
          technologies.end();
}

bool Manager::IsPortalDetectionEnabled(Technology::Identifier tech) {
  return IsTechnologyInList(GetCheckPortalList(NULL), tech);
}

void Manager::SetStartupPortalList(const string &portal_list) {
  startup_portal_list_ = portal_list;
  use_startup_portal_list_ = true;
}

bool Manager::IsProfileBefore(const ProfileRefPtr &a,
                              const ProfileRefPtr &b) const {
  DCHECK(a != b);
  for (const auto &profile : profiles_) {
    if (profile == a) {
      return true;
    }
    if (profile == b) {
      return false;
    }
  }
  NOTREACHED() << "We should have found both profiles in the profiles_ list!";
  return false;
}

bool Manager::IsServiceEphemeral(const ServiceConstRefPtr &service) const {
  return service->profile() == ephemeral_profile_;
}

bool Manager::IsTechnologyLinkMonitorEnabled(
    Technology::Identifier technology) const {
  return IsTechnologyInList(props_.link_monitor_technologies, technology);
}

bool Manager::IsTechnologyAutoConnectDisabled(
    Technology::Identifier technology) const {
  return IsTechnologyInList(props_.no_auto_connect_technologies, technology);
}

void Manager::OnProfileStorageInitialized(Profile *profile) {
  wifi_provider_->LoadAndFixupServiceEntries(profile);
}

DeviceRefPtr Manager::GetEnabledDeviceWithTechnology(
    Technology::Identifier technology) const {
  vector<DeviceRefPtr> devices;
  FilterByTechnology(technology, &devices);
  for (const auto &device : devices_) {
    if (device->enabled()) {
      return device;
    }
  }
  return NULL;
}

const ProfileRefPtr &Manager::ActiveProfile() const {
  DCHECK_NE(profiles_.size(), 0U);
  return profiles_.back();
}

bool Manager::IsActiveProfile(const ProfileRefPtr &profile) const {
  return (profiles_.size() > 0 &&
          ActiveProfile().get() == profile.get());
}

bool Manager::MoveServiceToProfile(const ServiceRefPtr &to_move,
                                   const ProfileRefPtr &destination) {
  const ProfileRefPtr from = to_move->profile();
  SLOG(Manager, 2) << "Moving service "
                   << to_move->unique_name()
                   << " to profile "
                   << destination->GetFriendlyName()
                   << " from "
                   << from->GetFriendlyName();
  return destination->AdoptService(to_move) && from->AbandonService(to_move);
}

ProfileRefPtr Manager::LookupProfileByRpcIdentifier(
    const string &profile_rpcid) {
  for (const auto &profile : profiles_) {
    if (profile_rpcid == profile->GetRpcIdentifier()) {
      return profile;
    }
  }
  return NULL;
}

void Manager::SetProfileForService(const ServiceRefPtr &to_set,
                                   const string &profile_rpcid,
                                   Error *error) {
  ProfileRefPtr profile = LookupProfileByRpcIdentifier(profile_rpcid);
  if (!profile) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          StringPrintf("Unknown Profile %s requested for "
                                       "Service", profile_rpcid.c_str()));
    return;
  }

  if (!to_set->profile()) {
    // We are being asked to set the profile property of a service that
    // has never been registered.  Now is a good time to register it.
    RegisterService(to_set);
  }

  if (to_set->profile().get() == profile.get()) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Service is already connected to this profile");
  } else if (!MoveServiceToProfile(to_set, profile)) {
    Error::PopulateAndLog(error, Error::kInternalError,
                          "Unable to move service to profile");
  }
}

void Manager::SetEnabledStateForTechnology(const std::string &technology_name,
                                           bool enabled_state,
                                           Error *error,
                                           const ResultCallback &callback) {
  CHECK(error != NULL);
  DCHECK(error->IsOngoing());
  Technology::Identifier id = Technology::IdentifierFromName(technology_name);
  if (id == Technology::kUnknown) {
    error->Populate(Error::kInvalidArguments, "Unknown technology");
    return;
  }
  bool deferred = false;
  auto result_aggregator(make_scoped_refptr(new ResultAggregator(callback)));
  for (auto &device : devices_) {
    if (device->technology() != id)
      continue;

    Error device_error(Error::kOperationInitiated);
    ResultCallback aggregator_callback(
        Bind(&ResultAggregator::ReportResult, result_aggregator));
    device->SetEnabledPersistent(
        enabled_state, &device_error, aggregator_callback);
    if (device_error.IsOngoing()) {
      deferred = true;
    } else if (!error->IsFailure()) {  // Report first failure.
      error->CopyFrom(device_error);
    }
  }
  if (deferred) {
    // Some device is handling this change asynchronously. Clobber any error
    // from another device, so that we can indicate the operation is still in
    // progress.
    error->Populate(Error::kOperationInitiated);
  } else if (error->IsOngoing()) {
    // |error| IsOngoing at entry to this method, but no device
    // |deferred|. Reset |error|, to indicate we're done.
    error->Reset();
  }
}

void Manager::UpdateEnabledTechnologies() {
  Error error;
  adaptor_->EmitStringsChanged(kEnabledTechnologiesProperty,
                               EnabledTechnologies(&error));
}

void Manager::UpdateUninitializedTechnologies() {
  Error error;
  adaptor_->EmitStringsChanged(kUninitializedTechnologiesProperty,
                               UninitializedTechnologies(&error));
}

void Manager::RegisterDevice(const DeviceRefPtr &to_manage) {
  LOG(INFO) << "Device " << to_manage->FriendlyName() << " registered.";
  for (const auto &device : devices_) {
    if (to_manage == device)
      return;
  }
  devices_.push_back(to_manage);

  LoadDeviceFromProfiles(to_manage);

  // If |to_manage| is new, it needs to be persisted.
  UpdateDevice(to_manage);

  // In normal usage, running_ will always be true when we are here, however
  // unit tests sometimes do things in otherwise invalid states.
  if (running_ && (to_manage->enabled_persistent() ||
                   to_manage->IsUnderlyingDeviceEnabled()))
    to_manage->SetEnabled(true);

  EmitDeviceProperties();
}

void Manager::DeregisterDevice(const DeviceRefPtr &to_forget) {
  SLOG(Manager, 2) << __func__ << "(" << to_forget->FriendlyName() << ")";
  for (auto it = devices_.begin(); it != devices_.end(); ++it) {
    if (to_forget.get() == it->get()) {
      SLOG(Manager, 2) << "Deregistered device: " << to_forget->UniqueName();
      UpdateDevice(to_forget);
      to_forget->SetEnabled(false);
      devices_.erase(it);
      EmitDeviceProperties();
      return;
    }
  }
  SLOG(Manager, 2) << __func__ << " unknown device: "
                   << to_forget->UniqueName();
}

void Manager::LoadDeviceFromProfiles(const DeviceRefPtr &device) {
  // We are applying device properties from the DefaultProfile, and adding the
  // union of hidden services in all loaded profiles to the device.
  for (const auto &profile : profiles_) {
    // Load device configuration, if any exists, as well as hidden services.
    profile->ConfigureDevice(device);
  }
}

void Manager::EmitDeviceProperties() {
  vector<string> device_paths;
  for (const auto &device : devices_) {
    device_paths.push_back(device->GetRpcIdentifier());
  }
  adaptor_->EmitRpcIdentifierArrayChanged(kDevicesProperty,
                                          device_paths);
  Error error;
  adaptor_->EmitStringsChanged(kAvailableTechnologiesProperty,
                               AvailableTechnologies(&error));
  adaptor_->EmitStringsChanged(kEnabledTechnologiesProperty,
                               EnabledTechnologies(&error));
  adaptor_->EmitStringsChanged(kUninitializedTechnologiesProperty,
                               UninitializedTechnologies(&error));
}

bool Manager::SetDisableWiFiVHT(const bool &disable_wifi_vht, Error *error) {
  if (disable_wifi_vht == wifi_provider_->disable_vht()) {
    return false;
  }
  wifi_provider_->set_disable_vht(disable_wifi_vht);
  return true;
}

bool Manager::GetDisableWiFiVHT(Error *error) {
  return wifi_provider_->disable_vht();
}

bool Manager::HasService(const ServiceRefPtr &service) {
  for (const auto &manager_service : services_) {
    if (manager_service->unique_name() == service->unique_name())
      return true;
  }
  return false;
}

void Manager::RegisterService(const ServiceRefPtr &to_manage) {
  SLOG(Manager, 2) << "Registering service " << to_manage->unique_name();

  MatchProfileWithService(to_manage);

  // Now add to OUR list.
  for (const auto &service : services_) {
    CHECK(to_manage->unique_name() != service->unique_name());
  }
  services_.push_back(to_manage);
  SortServices();
}

void Manager::DeregisterService(const ServiceRefPtr &to_forget) {
  for (auto it = services_.begin(); it != services_.end(); ++it) {
    if (to_forget->unique_name() == (*it)->unique_name()) {
      DLOG_IF(FATAL, (*it)->connection())
          << "Service " << (*it)->unique_name()
          << " still has a connection (in call to " << __func__ << ")";
      (*it)->Unload();
      (*it)->SetProfile(NULL);
      services_.erase(it);
      SortServices();
      return;
    }
  }
}

bool Manager::UnloadService(vector<ServiceRefPtr>::iterator *service_iterator) {
  if (!(**service_iterator)->Unload()) {
    return false;
  }

  DCHECK(!(**service_iterator)->connection());
  (**service_iterator)->SetProfile(NULL);
  *service_iterator = services_.erase(*service_iterator);

  return true;
}

void Manager::UpdateService(const ServiceRefPtr &to_update) {
  CHECK(to_update);
  LOG(INFO) << "Service " << to_update->unique_name() << " updated;"
            << " state: " << Service::ConnectStateToString(to_update->state())
            << " failure: "
            << Service::ConnectFailureToString(to_update->failure());
  SLOG(Manager, 2) << "IsConnected(): " << to_update->IsConnected();
  SLOG(Manager, 2) << "IsConnecting(): " << to_update->IsConnecting();
  if (to_update->IsConnected()) {
    to_update->EnableAndRetainAutoConnect();
    // Persists the updated auto_connect setting in the profile.
    SaveServiceToProfile(to_update);
  }
  SortServices();
}

void Manager::UpdateDevice(const DeviceRefPtr &to_update) {
  LOG(INFO) << "Device " << to_update->link_name() << " updated: "
            << (to_update->enabled_persistent() ? "enabled" : "disabled");
  // Saves the device to the topmost profile that accepts it (ordinary
  // profiles don't update but default profiles do). Normally, the topmost
  // updating profile would be the DefaultProfile at the bottom of the stack.
  // Autotests, differ from the normal scenario, however, in that they push a
  // second test-only DefaultProfile.
  for (auto rit = profiles_.rbegin(); rit != profiles_.rend(); ++rit) {
    if ((*rit)->UpdateDevice(to_update)) {
      return;
    }
  }
}

void Manager::UpdateWiFiProvider() {
  // Saves |wifi_provider_| to the topmost profile that accepts it (ordinary
  // profiles don't update but default profiles do). Normally, the topmost
  // updating profile would be the DefaultProfile at the bottom of the stack.
  // Autotests, differ from the normal scenario, however, in that they push a
  // second test-only DefaultProfile.
  for (auto rit = profiles_.rbegin(); rit != profiles_.rend(); ++rit) {
    if ((*rit)->UpdateWiFiProvider(*wifi_provider_)) {
      return;
    }
  }
}

void Manager::SaveServiceToProfile(const ServiceRefPtr &to_update) {
  if (IsServiceEphemeral(to_update)) {
    if (profiles_.empty()) {
      LOG(ERROR) << "Cannot assign profile to service: no profiles exist!";
    } else {
      MoveServiceToProfile(to_update, profiles_.back());
    }
  } else {
    to_update->profile()->UpdateService(to_update);
  }
}

void Manager::LoadProperties(const scoped_refptr<DefaultProfile> &profile) {
  profile->LoadManagerProperties(&props_);
  SetIgnoredDNSSearchPaths(props_.ignored_dns_search_paths, NULL);
}

void Manager::AddTerminationAction(const string &name,
                                   const base::Closure &start) {
  termination_actions_.Add(name, start);
}

void Manager::TerminationActionComplete(const string &name) {
  SLOG(Manager, 2) << __func__;
  termination_actions_.ActionComplete(name);
}

void Manager::RemoveTerminationAction(const string &name) {
  SLOG(Manager, 2) << __func__;
  termination_actions_.Remove(name);
}

void Manager::RunTerminationActions(const ResultCallback &done_callback) {
  LOG(INFO) << "Running termination actions.";
  termination_actions_.Run(kTerminationActionsTimeoutMilliseconds,
                           done_callback);
}

bool Manager::RunTerminationActionsAndNotifyMetrics(
    const ResultCallback &done_callback,
    Metrics::TerminationActionReason reason) {
  if (termination_actions_.IsEmpty())
    return false;

  metrics_->NotifyTerminationActionsStarted(reason);
  RunTerminationActions(done_callback);
  return true;
}

int Manager::RegisterDefaultServiceCallback(const ServiceCallback &callback) {
  default_service_callbacks_[++default_service_callback_tag_] = callback;
  return default_service_callback_tag_;
}

void Manager::DeregisterDefaultServiceCallback(int tag) {
  default_service_callbacks_.erase(tag);
}

void Manager::VerifyDestination(const string &certificate,
                                const string &public_key,
                                const string &nonce,
                                const string &signed_data,
                                const string &destination_udn,
                                const string &hotspot_ssid,
                                const string &hotspot_bssid,
                                const ResultBoolCallback &cb,
                                Error *error) {
  if (hotspot_bssid.length() > 32) {
    error->Populate(Error::kOperationFailed,
                    "Invalid SSID given for verification.");
    return;
  }
  vector<uint8_t> ssid;
  string bssid;
  if (hotspot_ssid.length() || hotspot_bssid.length()) {
    // If Chrome thinks this destination is already configured, service
    // will be an AP that both we and the destination are connected
    // to, and not the thing we should verify against.
    ssid.assign(hotspot_ssid.begin(), hotspot_ssid.end());
    bssid = hotspot_bssid;
  } else {
    // For now, we only support a single connected WiFi service.  If we change
    // that, we'll need to revisit this.
    bool found_one = false;
    for (const auto &service : services_) {
      if (service->technology() == Technology::kWifi &&
          service->IsConnected()) {
        WiFiService *wifi = reinterpret_cast<WiFiService *>(&(*service));
        bssid = wifi->bssid();
        ssid = wifi->ssid();
        found_one = true;
        break;
      }
    }
    if (!found_one) {
      error->Populate(Error::kOperationFailed,
                      "Unable to find connected WiFi service.");
      return;
    }
  }
  crypto_util_proxy_->VerifyDestination(certificate, public_key, nonce,
                                        signed_data, destination_udn,
                                        ssid, bssid, cb, error);
}

void Manager::VerifyToEncryptLink(string public_key,
                                  string data,
                                  ResultStringCallback cb,
                                  const Error &error,
                                  bool success) {
  if (!success || !error.IsSuccess()) {
    CHECK(error.IsFailure()) << "Return code from CryptoUtilProxy "
                             << "inconsistent with error code.";
    cb.Run(error, "");
    return;
  }
  Error encrypt_error;
  if (!crypto_util_proxy_->EncryptData(public_key, data, cb, &encrypt_error)) {
    CHECK(encrypt_error.IsFailure()) << "CryptoUtilProxy::EncryptData returned "
                                     << "inconsistently.";
    cb.Run(encrypt_error, "");
  }
}

void Manager::VerifyAndEncryptData(const string &certificate,
                                   const string &public_key,
                                   const string &nonce,
                                   const string &signed_data,
                                   const string &destination_udn,
                                   const string &hotspot_ssid,
                                   const string &hotspot_bssid,
                                   const string &data,
                                   const ResultStringCallback &cb,
                                   Error *error) {
  ResultBoolCallback on_verification_success = Bind(
      &Manager::VerifyToEncryptLink, AsWeakPtr(), public_key, data, cb);
  VerifyDestination(certificate, public_key, nonce, signed_data,
                    destination_udn, hotspot_ssid, hotspot_bssid,
                    on_verification_success, error);
}

void Manager::VerifyAndEncryptCredentials(const string &certificate,
                                          const string &public_key,
                                          const string &nonce,
                                          const string &signed_data,
                                          const string &destination_udn,
                                          const string &hotspot_ssid,
                                          const string &hotspot_bssid,
                                          const string &network_path,
                                          const ResultStringCallback &cb,
                                          Error *error) {
  // This is intentionally left unimplemented until we have a security review.
  error->Populate(Error::kNotImplemented, "Not implemented");
}

int Manager::CalcConnectionId(std::string gateway_ip,
                              std::string gateway_mac) {
  return static_cast<int>(std::hash<std::string>()(gateway_ip + gateway_mac +
      std::to_string(props_.connection_id_salt)));
}

void Manager::ReportServicesOnSameNetwork(int connection_id) {
  int num_services = 0;
  for (const auto &service : services_) {
    if (service->connection_id() == connection_id) {
      num_services++;
    }
  }
  metrics_->NotifyServicesOnSameNetwork(num_services);
}

void Manager::NotifyDefaultServiceChanged(const ServiceRefPtr &service) {
  for (const auto &callback : default_service_callbacks_) {
    callback.second.Run(service);
  }
  metrics_->NotifyDefaultServiceChanged(service);
  EmitDefaultService();
}

void Manager::EmitDefaultService() {
  RpcIdentifier rpc_identifier = GetDefaultServiceRpcIdentifier(NULL);
  if (rpc_identifier != default_service_rpc_identifier_) {
    adaptor_->EmitRpcIdentifierChanged(kDefaultServiceProperty, rpc_identifier);
    default_service_rpc_identifier_ = rpc_identifier;
  }
}

void Manager::OnSuspendImminent() {
  for (const auto &device : devices_) {
    device->OnBeforeSuspend();
  }
  if (!RunTerminationActionsAndNotifyMetrics(
           Bind(&Manager::OnSuspendActionsComplete, AsWeakPtr()),
           Metrics::kTerminationActionReasonSuspend)) {
    LOG(INFO) << "No asynchronous suspend actions were run.";
    power_manager_->ReportSuspendReadiness();
  }
}

void Manager::OnSuspendDone() {
  metrics_->NotifySuspendDone();
  for (const auto &service : services_) {
    service->OnAfterResume();
  }
  SortServices();
  for (const auto &device : devices_) {
    device->OnAfterResume();
  }
}

void Manager::OnDarkSuspendImminent() {
  for (const auto &device : devices_) {
    device->OnDarkResume();
  }
  for (const auto &service : services_) {
    service->OnDarkResume();
  }
  // TODO(pprabhu): This should probably become asynchronous when devices
  // implement dark suspend functionality.
  power_manager_->ReportDarkSuspendReadiness();
}

void Manager::OnSuspendActionsComplete(const Error &error) {
  LOG(INFO) << "Finished suspend actions. Result: " << error;
  metrics_->NotifyTerminationActionsCompleted(
      Metrics::kTerminationActionReasonSuspend, error.IsSuccess());
  power_manager_->ReportSuspendReadiness();
}

void Manager::FilterByTechnology(Technology::Identifier tech,
                                 vector<DeviceRefPtr> *found) const {
  CHECK(found);
  for (const auto &device : devices_) {
    if (device->technology() == tech)
      found->push_back(device);
  }
}

ServiceRefPtr Manager::FindService(const string &name) {
  for (const auto &service : services_) {
    if (name == service->unique_name())
      return service;
  }
  return NULL;
}

void Manager::HelpRegisterConstDerivedRpcIdentifier(
    const string &name,
    RpcIdentifier(Manager::*get)(Error *error)) {
  store_.RegisterDerivedRpcIdentifier(
      name,
      RpcIdentifierAccessor(
          new CustomAccessor<Manager, RpcIdentifier>(this, get, NULL)));
}

void Manager::HelpRegisterConstDerivedRpcIdentifiers(
    const string &name,
    RpcIdentifiers(Manager::*get)(Error *error)) {
  store_.RegisterDerivedRpcIdentifiers(
      name,
      RpcIdentifiersAccessor(
          new CustomAccessor<Manager, RpcIdentifiers>(this, get, NULL)));
}

void Manager::HelpRegisterDerivedString(
    const string &name,
    string(Manager::*get)(Error *error),
    bool(Manager::*set)(const string&, Error *)) {
  store_.RegisterDerivedString(
      name,
      StringAccessor(new CustomAccessor<Manager, string>(this, get, set)));
}

void Manager::HelpRegisterConstDerivedStrings(
    const string &name,
    Strings(Manager::*get)(Error *)) {
  store_.RegisterDerivedStrings(
      name,
      StringsAccessor(new CustomAccessor<Manager, Strings>(this, get, NULL)));
}

void Manager::HelpRegisterDerivedBool(
    const string &name,
    bool(Manager::*get)(Error *error),
    bool(Manager::*set)(const bool&, Error *)) {
  store_.RegisterDerivedBool(
      name,
      BoolAccessor(new CustomAccessor<Manager, bool>(this, get, set, NULL)));
}

void Manager::SortServices() {
  // We might be called in the middle of a series of events that
  // may result in multiple calls to Manager::SortServices, or within
  // an outer loop that may also be traversing the services_ list.
  // Defer this work to the event loop.
  if (sort_services_task_.IsCancelled()) {
    sort_services_task_.Reset(Bind(&Manager::SortServicesTask, AsWeakPtr()));
    dispatcher_->PostTask(sort_services_task_.callback());
  }
}

void Manager::SortServicesTask() {
  SLOG(Manager, 4) << "In " << __func__;
  sort_services_task_.Cancel();
  ServiceRefPtr default_service;

  if (!services_.empty()) {
    // Keep track of the service that is the candidate for the default
    // service.  We have not yet tested to see if this service has a
    // connection.
    default_service = services_[0];
  }
  const bool kCompareConnectivityState = true;
  sort(services_.begin(), services_.end(),
       ServiceSorter(this, kCompareConnectivityState, technology_order_));

  if (!services_.empty()) {
    ConnectionRefPtr default_connection = default_service->connection();
    if (default_connection &&
        services_[0]->connection() != default_connection) {
      default_connection->SetIsDefault(false);
    }
    if (services_[0]->connection()) {
      services_[0]->connection()->SetIsDefault(true);
      if (default_service != services_[0]) {
        // TODO(samueltan): never seems to get called when switching
        // between ethernet and wifi; find out why.
        TransferWakeOnPacketConnections(default_service, services_[0]);
        default_service = services_[0];
        LOG(INFO) << "Default service is now "
                  << default_service->unique_name();
      }
    } else {
      default_service = NULL;
    }
  }

  Error error;
  adaptor_->EmitRpcIdentifierArrayChanged(kServiceCompleteListProperty,
                                          EnumerateCompleteServices(NULL));
  adaptor_->EmitRpcIdentifierArrayChanged(kServicesProperty,
                                          EnumerateAvailableServices(NULL));
  adaptor_->EmitRpcIdentifierArrayChanged(kServiceWatchListProperty,
                                          EnumerateWatchedServices(NULL));
  adaptor_->EmitStringsChanged(kConnectedTechnologiesProperty,
                               ConnectedTechnologies(&error));
  adaptor_->EmitStringChanged(kDefaultTechnologyProperty,
                              DefaultTechnology(&error));
  NotifyDefaultServiceChanged(default_service);
  RefreshConnectionState();

  AutoConnect();
}

void Manager::DeviceStatusCheckTask() {
  SLOG(Manager, 4) << "In " << __func__;

  ConnectionStatusCheck();
  DevicePresenceStatusCheck();

  dispatcher_->PostDelayedTask(device_status_check_task_.callback(),
                               kDeviceStatusCheckIntervalMilliseconds);
}

void Manager::ConnectionStatusCheck() {
  SLOG(Manager, 4) << "In " << __func__;
  // Report current connection status.
  Metrics::ConnectionStatus status = Metrics::kConnectionStatusOffline;
  if (IsConnected()) {
    status = Metrics::kConnectionStatusConnected;
    // Check if device is online as well.
    if (IsOnline()) {
      metrics_->NotifyDeviceConnectionStatus(Metrics::kConnectionStatusOnline);
    }
  }
  metrics_->NotifyDeviceConnectionStatus(status);
}

void Manager::DevicePresenceStatusCheck() {
  Error error;
  vector<string> available_technologies = AvailableTechnologies(&error);

  for (const auto &technology : kProbeTechnologies) {
    bool presence = std::find(available_technologies.begin(),
                              available_technologies.end(),
                              technology) != available_technologies.end();
    metrics_->NotifyDevicePresenceStatus(
        Technology::IdentifierFromName(technology), presence);
  }
}

bool Manager::MatchProfileWithService(const ServiceRefPtr &service) {
  vector<ProfileRefPtr>::reverse_iterator it;
  for (it = profiles_.rbegin(); it != profiles_.rend(); ++it) {
    if ((*it)->ConfigureService(service)) {
      break;
    }
  }
  if (it == profiles_.rend()) {
    ephemeral_profile_->AdoptService(service);
    return false;
  }
  return true;
}

void Manager::AutoConnect() {
  if (!running_) {
    LOG(INFO) << "Auto-connect suppressed -- not running.";
    return;
  }
  if (power_manager_ && power_manager_->suspending()) {
    LOG(INFO) << "Auto-connect suppressed -- system is suspending.";
    return;
  }
  if (services_.empty()) {
    LOG(INFO) << "Auto-connect suppressed -- no services.";
    return;
  }

  if (SLOG_IS_ON(Manager, 4)) {
    SLOG(Manager, 4) << "Sorted service list for AutoConnect: ";
    for (size_t i = 0; i < services_.size(); ++i) {
      ServiceRefPtr service = services_[i];
      const char *compare_reason = NULL;
      if (i + 1 < services_.size()) {
        const bool kCompareConnectivityState = true;
        Service::Compare(
            this, service, services_[i+1], kCompareConnectivityState,
            technology_order_, &compare_reason);
      } else {
        compare_reason = "last";
      }
      SLOG(Manager, 4) << "Service " << service->unique_name()
                       << " Profile: " << service->profile()->GetFriendlyName()
                       << " IsConnected: " << service->IsConnected()
                       << " IsConnecting: " << service->IsConnecting()
                       << " HasEverConnected: " << service->has_ever_connected()
                       << " IsFailed: " << service->IsFailed()
                       << " connectable: " << service->connectable()
                       << " auto_connect: " << service->auto_connect()
                       << " retain_auto_connect: "
                       << service->retain_auto_connect()
                       << " priority: " << service->priority()
                       << " crypto_algorithm: " << service->crypto_algorithm()
                       << " key_rotation: " << service->key_rotation()
                       << " endpoint_auth: " << service->endpoint_auth()
                       << " strength: " << service->strength()
                       << " sorted: " << compare_reason;
    }
  }

  // Report the number of auto-connectable wifi services available when wifi is
  // idle (no active or pending connection), which will trigger auto connect
  // for wifi services.
  if (IsWifiIdle()) {
    wifi_provider_->ReportAutoConnectableServices();
  }

  // Perform auto-connect.
  for (const auto &service : services_) {
    if (service->auto_connect()) {
      service->AutoConnect();
    }
  }
}

void Manager::ConnectToBestServices(Error */*error*/) {
  dispatcher_->PostTask(Bind(&Manager::ConnectToBestServicesTask, AsWeakPtr()));
}

void Manager::ConnectToBestServicesTask() {
  vector<ServiceRefPtr> services_copy = services_;
  const bool kCompareConnectivityState = false;
  sort(services_copy.begin(), services_copy.end(),
       ServiceSorter(this, kCompareConnectivityState, technology_order_));
  set<Technology::Identifier> connecting_technologies;
  for (const auto &service : services_copy) {
    if (!service->connectable()) {
      // Due to service sort order, it is guaranteed that no services beyond
      // this one will be connectable either.
      break;
    }
    if (!service->auto_connect() || !service->IsVisible()) {
      continue;
    }
    Technology::Identifier technology = service->technology();
    if (!Technology::IsPrimaryConnectivityTechnology(technology) &&
        !IsConnected()) {
      // Non-primary services need some other service connected first.
      continue;
    }
    if (ContainsKey(connecting_technologies, technology)) {
      // We have already started a connection for this technology.
      continue;
    }
    if (service->explicitly_disconnected())
      continue;
    connecting_technologies.insert(technology);
    if (!service->IsConnected() && !service->IsConnecting()) {
      // At first blush, it may seem that using Service::AutoConnect might
      // be the right choice, however Service::IsAutoConnectable and its
      // overridden implementations consider a host of conditions which
      // prevent it from attempting a connection which we'd like to ignore
      // for the purposes of this user-initiated action.
      Error error;
      service->Connect(&error, __func__);
      if (error.IsFailure()) {
        LOG(ERROR) << "Connection failed: " << error.message();
      }
    }
  }

  if (SLOG_IS_ON(Manager, 4)) {
    SLOG(Manager, 4) << "Sorted service list for ConnectToBestServicesTask: ";
    for (size_t i = 0; i < services_copy.size(); ++i) {
      ServiceRefPtr service = services_copy[i];
      const char *compare_reason = NULL;
      if (i + 1 < services_copy.size()) {
        if (!service->connectable()) {
          // Due to service sort order, it is guaranteed that no services beyond
          // this one are connectable either.
          break;
        }
        Service::Compare(
            this, service, services_copy[i+1],
            kCompareConnectivityState, technology_order_,
            &compare_reason);
      } else {
        compare_reason = "last";
      }
      SLOG(Manager, 4) << "Service " << service->unique_name()
                       << " Profile: " << service->profile()->GetFriendlyName()
                       << " IsConnected: " << service->IsConnected()
                       << " IsConnecting: " << service->IsConnecting()
                       << " HasEverConnected: " << service->has_ever_connected()
                       << " IsFailed: " << service->IsFailed()
                       << " connectable: " << service->connectable()
                       << " auto_connect: " << service->auto_connect()
                       << " retain_auto_connect: "
                       << service->retain_auto_connect()
                       << " priority: " << service->priority()
                       << " crypto_algorithm: " << service->crypto_algorithm()
                       << " key_rotation: " << service->key_rotation()
                       << " endpoint_auth: " << service->endpoint_auth()
                       << " strength: " << service->strength()
                       << " sorted: " << compare_reason;
    }
  }
}

void Manager::CreateConnectivityReport(Error */*error*/) {
  // For each of the connected services, perform a single portal detection
  // test to assess connectivity.  The results should be written to the log.
  for (const auto &service : services_) {
    if (!service->IsConnected()) {
      // Service sort order guarantees that no service beyond this one will be
      // connected either.
      break;
    }

    // Get the underlying device for this service and perform connectivity test.
    for (const auto &device : devices_) {
      if (device->IsConnectedToService(service)) {
        if (device->StartConnectivityTest()) {
          SLOG(Manager, 3) << "Started connectivity test for service "
                           << service->unique_name();
        } else {
          SLOG(Manager, 3) << "Failed to start connectivity test for service "
                           << service->unique_name()
                           << " device not reporting IsConnected.";
        }
        break;
      }
    }
  }
}

bool Manager::IsConnected() const {
  // |services_| is sorted such that connected services are first.
  return !services_.empty() && services_.front()->IsConnected();
}

bool Manager::IsOnline() const {
  // |services_| is sorted such that online services are first.
  return !services_.empty() && services_.front()->IsOnline();
}

string Manager::CalculateState(Error */*error*/) {
  return IsConnected() ? kStateOnline : kStateOffline;
}

void Manager::RefreshConnectionState() {
  const ServiceRefPtr &service = GetDefaultService();
  string connection_state = service ? service->GetStateString() : kStateIdle;
  if (connection_state_ == connection_state) {
    return;
  }
  connection_state_ = connection_state;
  adaptor_->EmitStringChanged(kConnectionStateProperty, connection_state_);
}

vector<string> Manager::AvailableTechnologies(Error */*error*/) {
  set<string> unique_technologies;
  for (const auto &device : devices_) {
    unique_technologies.insert(
        Technology::NameFromIdentifier(device->technology()));
  }
  return vector<string>(unique_technologies.begin(), unique_technologies.end());
}

vector<string> Manager::ConnectedTechnologies(Error */*error*/) {
  set<string> unique_technologies;
  for (const auto &device : devices_) {
    if (device->IsConnected())
      unique_technologies.insert(
          Technology::NameFromIdentifier(device->technology()));
  }
  return vector<string>(unique_technologies.begin(), unique_technologies.end());
}

bool Manager::IsTechnologyConnected(Technology::Identifier technology) const {
  for (const auto &device : devices_) {
    if (device->technology() == technology && device->IsConnected())
      return true;
  }
  return false;
}

string Manager::DefaultTechnology(Error */*error*/) {
  return (!services_.empty() && services_[0]->IsConnected()) ?
      services_[0]->GetTechnologyString() : "";
}

vector<string> Manager::EnabledTechnologies(Error */*error*/) {
  set<string> unique_technologies;
  for (const auto &device : devices_) {
    if (device->enabled())
      unique_technologies.insert(
          Technology::NameFromIdentifier(device->technology()));
  }
  return vector<string>(unique_technologies.begin(), unique_technologies.end());
}

vector<string> Manager::UninitializedTechnologies(Error */*error*/) {
  return device_info_.GetUninitializedTechnologies();
}

RpcIdentifiers Manager::EnumerateDevices(Error */*error*/) {
  RpcIdentifiers device_rpc_ids;
  for (const auto &device : devices_) {
    device_rpc_ids.push_back(device->GetRpcIdentifier());
  }
  return device_rpc_ids;
}

RpcIdentifiers Manager::EnumerateProfiles(Error */*error*/) {
  RpcIdentifiers profile_rpc_ids;
  for (const auto &profile : profiles_) {
    profile_rpc_ids.push_back(profile->GetRpcIdentifier());
  }
  return profile_rpc_ids;
}

RpcIdentifiers Manager::EnumerateAvailableServices(Error */*error*/) {
  RpcIdentifiers service_rpc_ids;
  for (const auto &service : services_) {
    if (service->IsVisible()) {
      service_rpc_ids.push_back(service->GetRpcIdentifier());
    }
  }
  return service_rpc_ids;
}

RpcIdentifiers Manager::EnumerateCompleteServices(Error */*error*/) {
  RpcIdentifiers service_rpc_ids;
  for (const auto &service : services_) {
    service_rpc_ids.push_back(service->GetRpcIdentifier());
  }
  return service_rpc_ids;
}

RpcIdentifiers Manager::EnumerateWatchedServices(Error */*error*/) {
  RpcIdentifiers service_rpc_ids;
  for (const auto &service : services_) {
    if (service->IsVisible() && service->IsActive(NULL)) {
      service_rpc_ids.push_back(service->GetRpcIdentifier());
    }
  }
  return service_rpc_ids;
}

string Manager::GetActiveProfileRpcIdentifier(Error */*error*/) {
  return ActiveProfile()->GetRpcIdentifier();
}

string Manager::GetCheckPortalList(Error */*error*/) {
  return use_startup_portal_list_ ? startup_portal_list_ :
      props_.check_portal_list;
}

bool Manager::SetCheckPortalList(const string &portal_list, Error *error) {
  use_startup_portal_list_ = false;
  if (props_.check_portal_list == portal_list) {
    return false;
  }
  props_.check_portal_list = portal_list;
  return true;
}

string Manager::GetIgnoredDNSSearchPaths(Error */*error*/) {
  return props_.ignored_dns_search_paths;
}

bool Manager::SetIgnoredDNSSearchPaths(const string &ignored_paths,
                                       Error */*error*/) {
  if (props_.ignored_dns_search_paths == ignored_paths) {
    return false;
  }
  vector<string> ignored_path_list;
  if (!ignored_paths.empty()) {
    base::SplitString(ignored_paths, ',', &ignored_path_list);
  }
  props_.ignored_dns_search_paths = ignored_paths;
  resolver_->set_ignored_search_list(ignored_path_list);
  return true;
}

// called via RPC (e.g., from ManagerDBusAdaptor)
ServiceRefPtr Manager::GetService(const KeyValueStore &args, Error *error) {
  if (args.ContainsString(kTypeProperty) &&
      args.GetString(kTypeProperty) == kTypeVPN) {
     // GetService on a VPN service should actually perform ConfigureService.
     // TODO(pstew): Remove this hack and change Chrome to use ConfigureService
     // instead, when we no longer need to support flimflam.  crbug.com/213802
     return ConfigureService(args, error);
  }

  ServiceRefPtr service = GetServiceInner(args, error);
  if (service) {
    // Configures the service using the rest of the passed-in arguments.
    service->Configure(args, error);
  }

  return service;
}

ServiceRefPtr Manager::GetServiceInner(const KeyValueStore &args,
                                       Error *error) {
  if (args.ContainsString(kGuidProperty)) {
    SLOG(Manager, 2) << __func__ << ": searching by GUID";
    ServiceRefPtr service =
        GetServiceWithGUID(args.GetString(kGuidProperty), NULL);
    if (service) {
      return service;
    }
  }

  if (!args.ContainsString(kTypeProperty)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments, kErrorTypeRequired);
    return NULL;
  }

  string type = args.GetString(kTypeProperty);
  Technology::Identifier technology = Technology::IdentifierFromName(type);
  if (!ContainsKey(providers_, technology)) {
    Error::PopulateAndLog(error, Error::kNotSupported,
                          kErrorUnsupportedServiceType);
    return NULL;
  }

  SLOG(Manager, 2) << __func__ << ": getting " << type << " Service";
  return providers_[technology]->GetService(args, error);
}

// called via RPC (e.g., from ManagerDBusAdaptor)
ServiceRefPtr Manager::ConfigureService(const KeyValueStore &args,
                                        Error *error) {
  ProfileRefPtr profile = ActiveProfile();
  bool profile_specified = args.ContainsString(kProfileProperty);
  if (profile_specified) {
    string profile_rpcid = args.GetString(kProfileProperty);
    profile = LookupProfileByRpcIdentifier(profile_rpcid);
    if (!profile) {
      Error::PopulateAndLog(error, Error::kInvalidArguments,
                            "Invalid profile name " + profile_rpcid);
      return NULL;
    }
  }

  ServiceRefPtr service = GetServiceInner(args, error);
  if (error->IsFailure() || !service) {
    LOG(ERROR) << "GetService failed; returning upstream error.";
    return NULL;
  }

  // First pull in any stored configuration associated with the service.
  if (service->profile() == profile) {
    SLOG(Manager, 2) << __func__ << ": service " << service->unique_name()
                     << " is already a member of profile "
                     << profile->GetFriendlyName()
                     << " so a load is not necessary.";
  } else if (profile->LoadService(service)) {
    SLOG(Manager, 2) << __func__ << ": applied stored information from profile "
                     << profile->GetFriendlyName()
                     << " into service "
                     << service->unique_name();
  } else {
    SLOG(Manager, 2) << __func__ << ": no previous information in profile "
                     << profile->GetFriendlyName()
                     << " exists for service "
                     << service->unique_name();
  }

  // Overlay this with the passed-in configuration parameters.
  service->Configure(args, error);

  // Overwrite the profile data with the resulting configured service.
  if (!profile->UpdateService(service)) {
    Error::PopulateAndLog(error, Error::kInternalError,
                          "Unable to save service to profile");
    return NULL;
  }

  if (HasService(service)) {
    // If the service has been registered (it may not be -- as is the case
    // with invisible WiFi networks), we can now transfer the service between
    // profiles.
    if (IsServiceEphemeral(service) ||
        (profile_specified && service->profile() != profile)) {
      SLOG(Manager, 2) << "Moving service to profile "
                       << profile->GetFriendlyName();
      if (!MoveServiceToProfile(service, profile)) {
        Error::PopulateAndLog(error, Error::kInternalError,
                              "Unable to move service to profile");
      }
    }
  }

  // Notify the service that a profile has been configured for it.
  service->OnProfileConfigured();

  return service;
}

// called via RPC (e.g., from ManagerDBusAdaptor)
ServiceRefPtr Manager::ConfigureServiceForProfile(
    const string &profile_rpcid, const KeyValueStore &args, Error *error) {
  if (!args.ContainsString(kTypeProperty)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments, kErrorTypeRequired);
    return NULL;
  }

  string type = args.GetString(kTypeProperty);
  Technology::Identifier technology = Technology::IdentifierFromName(type);

  if (!ContainsKey(providers_, technology)) {
    Error::PopulateAndLog(error, Error::kNotSupported,
                          kErrorUnsupportedServiceType);
    return NULL;
  }

  ProviderInterface *provider = providers_[technology];

  ProfileRefPtr profile = LookupProfileByRpcIdentifier(profile_rpcid);
  if (!profile) {
    Error::PopulateAndLog(error, Error::kNotFound,
                          "Profile specified was not found");
    return NULL;
  }
  if (args.LookupString(kProfileProperty, profile_rpcid) != profile_rpcid) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Profile argument does not match that in "
                          "the configuration arguments");
    return NULL;
  }

  ServiceRefPtr service;
  if (args.ContainsString(kGuidProperty)) {
    SLOG(Manager, 2) << __func__ << ": searching by GUID";
    service = GetServiceWithGUID(args.GetString(kGuidProperty), NULL);
    if (service && service->technology() != technology) {
      Error::PopulateAndLog(error, Error::kNotSupported,
                            StringPrintf("This GUID matches a non-%s service",
                                         type.c_str()));
      return NULL;
    }
  }

  if (!service) {
    Error find_error;
    service = provider->FindSimilarService(args, &find_error);
  }

  // If no matching service exists, create a new service in the specified
  // profile using ConfigureService().
  if (!service) {
    KeyValueStore configure_args;
    configure_args.CopyFrom(args);
    configure_args.SetString(kProfileProperty, profile_rpcid);
    return ConfigureService(configure_args, error);
  }

  // The service already exists and is set to the desired profile,
  // the service is in the ephemeral profile, or the current profile
  // for the service appears before the desired profile, we need to
  // reassign the service to the new profile if necessary, leaving
  // the old profile intact (i.e, not calling Profile::AbandonService()).
  // Then, configure the properties on the service as well as its newly
  // associated profile.
  if (service->profile() == profile ||
      IsServiceEphemeral(service) ||
      IsProfileBefore(service->profile(), profile)) {
    SetupServiceInProfile(service, profile, args, error);
    return service;
  }

  // The current profile for the service appears after the desired
  // profile.  We must create a temporary service specifically for
  // the task of creating configuration data.  This service will
  // neither inherit properties from the visible service, nor will
  // it exist after this function returns.
  service = provider->CreateTemporaryService(args, error);
  if (!service || !error->IsSuccess()) {
    // Service::CreateTemporaryService() failed, and has set the error
    // appropriately.
    return NULL;
  }

  // The profile may already have configuration for this service.
  profile->ConfigureService(service);

  SetupServiceInProfile(service, profile, args, error);

  // Although we have succeeded, this service will not exist, so its
  // path is of no use to the caller.
  DCHECK(service->HasOneRef());
  return NULL;
}

void Manager::SetupServiceInProfile(ServiceRefPtr service,
                                    ProfileRefPtr profile,
                                    const KeyValueStore &args,
                                    Error *error) {
  service->SetProfile(profile);
  service->Configure(args, error);
  profile->UpdateService(service);
}

ServiceRefPtr Manager::FindMatchingService(const KeyValueStore &args,
                                           Error *error) {
  for (const auto &service : services_) {
    if (service->DoPropertiesMatch(args)) {
      return service;
    }
  }
  error->Populate(Error::kNotFound, "Matching service was not found");
  return NULL;
}

void Manager::AddWakeOnPacketConnection(const string &ip_endpoint,
                                        Error *error) {
  IPAddress ip_addr(ip_endpoint);
  if (!ip_addr.IsValid()) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Invalid ip_address " + ip_endpoint);
    return;
  }
  ServiceRefPtr default_service = services_.front();
  if (default_service) {
    DeviceRefPtr device = GetDeviceConnectedToService(default_service);
    if (!device) {
      Error::PopulateAndLog(error, Error::kOperationFailed,
                            "No matching device found");
    } else {
      device->AddWakeOnPacketConnection(ip_addr, error);
    }
  } else {
    Error::PopulateAndLog(error, Error::kOperationFailed, "No services found");
  }
}

void Manager::RemoveWakeOnPacketConnection(const string &ip_endpoint,
                                           Error *error) {
  IPAddress ip_addr(ip_endpoint);
  if (!ip_addr.IsValid()) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Invalid ip_address " + ip_endpoint);
    return;
  }
  ServiceRefPtr default_service = services_.front();
  if (default_service) {
    DeviceRefPtr device = GetDeviceConnectedToService(default_service);
    if (!device) {
      Error::PopulateAndLog(error, Error::kOperationFailed,
                            "No matching device found");
    } else {
      device->RemoveWakeOnPacketConnection(ip_addr, error);
    }
  } else {
    Error::PopulateAndLog(error, Error::kOperationFailed, "No services found");
  }
}

void Manager::RemoveAllWakeOnPacketConnections(Error *error) {
  ServiceRefPtr default_service = services_.front();
  if (default_service) {
    DeviceRefPtr device = GetDeviceConnectedToService(default_service);
    if (!device) {
      Error::PopulateAndLog(error, Error::kOperationFailed,
                            "No matching device found");
    } else {
      device->RemoveAllWakeOnPacketConnections(error);
    }
  } else {
    Error::PopulateAndLog(error, Error::kOperationFailed, "No services found");
  }
}

const map<string, GeolocationInfos>
    &Manager::GetNetworksForGeolocation() const {
  return networks_for_geolocation_;
}

void Manager::OnDeviceGeolocationInfoUpdated(const DeviceRefPtr &device) {
  SLOG(Manager, 2) << __func__ << " for technology "
                   << Technology::NameFromIdentifier(device->technology());
  switch (device->technology()) {
    // TODO(gauravsh): crbug.com/217833 Need a strategy for combining
    // geolocation objects from multiple devices of the same technolgy.
    // Currently, we just override the any previously acquired
    // geolocation objects for the retrieved technology type.
    case Technology::kWifi:
      networks_for_geolocation_[kGeoWifiAccessPointsProperty] =
          device->GetGeolocationObjects();
      break;
    case Technology::kCellular:
      networks_for_geolocation_[kGeoCellTowersProperty] =
          device->GetGeolocationObjects();
      break;
    default:
      // Ignore other technologies.
      break;
  }
}

void Manager::RecheckPortal(Error */*error*/) {
  for (const auto &device : devices_) {
    if (device->RequestPortalDetection()) {
      // Only start Portal Detection on the device with the default connection.
      // We will get a "true" return value when we've found that device, and
      // can end our loop early as a result.
      break;
    }
  }
}

void Manager::RecheckPortalOnService(const ServiceRefPtr &service) {
  for (const auto &device : devices_) {
    if (device->IsConnectedToService(service)) {
      // As opposed to RecheckPortal() above, we explicitly stop and then
      // restart portal detection, since the service to recheck was explicitly
      // specified.
      device->RestartPortalDetection();
      break;
    }
  }
}

void Manager::RequestScan(Device::ScanType scan_type,
                          const string &technology, Error *error) {
  if (technology == kTypeWifi || technology == "") {
    vector<DeviceRefPtr> wifi_devices;
    FilterByTechnology(Technology::kWifi, &wifi_devices);
    for (const auto &wifi_device : wifi_devices) {
      metrics_->NotifyUserInitiatedEvent(Metrics::kUserInitiatedEventWifiScan);
      wifi_device->Scan(scan_type, error, __func__);
    }
  } else {
    // TODO(quiche): support scanning for other technologies?
    Error::PopulateAndLog(error, Error::kInvalidArguments,
                          "Unrecognized technology " + technology);
  }
}

string Manager::GetTechnologyOrder() {
  vector<string> technology_names;
  for (const auto &technology : technology_order_) {
    technology_names.push_back(Technology::NameFromIdentifier(technology));
  }

  return JoinString(technology_names, ',');
}

void Manager::SetTechnologyOrder(const string &order, Error *error) {
  vector<Technology::Identifier> new_order;
  SLOG(Manager, 2) << "Setting technology order to " << order;
  if (!Technology::GetTechnologyVectorFromString(order, &new_order, error)) {
    return;
  }

  technology_order_ = new_order;
  SortServices();
}

bool Manager::IsWifiIdle() {
  bool ret = false;

  // Since services are sorted by connection state, status of the wifi device
  // can be determine by examing the connection state of the first wifi service.
  for (const auto &service : services_) {
    if (service->technology() == Technology::kWifi) {
      if (!service->IsConnecting() && !service->IsConnected()) {
        ret = true;
      }
      break;
    }
  }
  return ret;
}

void Manager::UpdateProviderMapping() {
  providers_[Technology::kEthernetEap] = ethernet_eap_provider_.get();
  providers_[Technology::kVPN] = vpn_provider_.get();
  providers_[Technology::kWifi] = wifi_provider_.get();
#if !defined(DISABLE_WIMAX)
  providers_[Technology::kWiMax] = wimax_provider_.get();
#endif  // DISABLE_WIMAX
}

void Manager::TransferWakeOnPacketConnections(
    const ServiceRefPtr &old_service, const ServiceRefPtr &new_service) {
  string ip_string;
  DeviceRefPtr old_device = GetDeviceConnectedToService(old_service);
  DeviceRefPtr new_device = GetDeviceConnectedToService(new_service);
  if (!old_device || !new_device) {
    LOG(ERROR) << "Cannot find both old a new devices";
    return;
  }
  if (old_device == new_device) {
    return;  // No transfer required
  }
  new_device->AddWakeOnPacketConnections(
      old_device->GetWakeOnPacketConnections());
  Error e;
  old_device->RemoveAllWakeOnPacketConnections(&e);
  if (e.IsFailure()) {
    LOG(ERROR) << "Failed to remove all connection from old device: "
               << e.message();
  }
}

DeviceRefPtr Manager::GetDeviceConnectedToService(ServiceRefPtr service) {
  for (DeviceRefPtr device : devices_) {
    if (device->IsConnectedToService(service)) {
      return device;
    }
  }
  return nullptr;
}

}  // namespace shill
