// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MANAGER_H_
#define SHILL_MANAGER_H_

#include <map>
#include <string>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_ptr.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/crypto_util_proxy.h"
#include "shill/device.h"
#include "shill/device_info.h"
#include "shill/event_dispatcher.h"
#include "shill/geolocation_info.h"
#include "shill/hook_table.h"
#include "shill/metrics.h"
#include "shill/modem_info.h"
#include "shill/power_manager.h"
#include "shill/profile.h"
#include "shill/property_store.h"
#include "shill/service.h"
#include "shill/wifi.h"
#include "shill/wimax_provider.h"

namespace shill {

class ControlInterface;
class DBusManager;
class DefaultProfile;
class Error;
class EthernetEapProvider;
class EventDispatcher;
class IPAddressStore;
class ManagerAdaptorInterface;
class Resolver;
class StoreInterface;
class VPNProvider;
class WiFiProvider;

class Manager : public base::SupportsWeakPtr<Manager> {
 public:
  typedef base::Callback<void(const ServiceRefPtr &service)> ServiceCallback;

  struct Properties {
   public:
    Properties()
        : offline_mode(false),
          portal_check_interval_seconds(0),
          arp_gateway(true),
          connection_id_salt(0) {}
    bool offline_mode;
    std::string check_portal_list;
    std::string country;
    int32_t portal_check_interval_seconds;
    std::string portal_url;
    std::string host_name;
    // Whether to ARP for the default gateway in the DHCP client after
    // acquiring a lease.
    bool arp_gateway;
    // Comma-separated list of technologies for which link-monitoring is
    // enabled.
    std::string link_monitor_technologies;
    // Comma-separated list of technologies for which auto-connect is disabled.
    std::string no_auto_connect_technologies;
    // Comma-separated list of DNS search paths to be ignored.
    std::string ignored_dns_search_paths;
    // Salt value use for calculating network connection ID.
    int connection_id_salt;
  };

  Manager(ControlInterface *control_interface,
          EventDispatcher *dispatcher,
          Metrics *metrics,
          GLib *glib,
          const std::string &run_directory,
          const std::string &storage_directory,
          const std::string &user_storage_directory);
  virtual ~Manager();

  void AddDeviceToBlackList(const std::string &device_name);

  virtual void Start();
  virtual void Stop();
  bool running() const { return running_; }

  const ProfileRefPtr &ActiveProfile() const;
  bool IsActiveProfile(const ProfileRefPtr &profile) const;
  bool MoveServiceToProfile(const ServiceRefPtr &to_move,
                            const ProfileRefPtr &destination);
  ProfileRefPtr LookupProfileByRpcIdentifier(const std::string &profile_rpcid);

  // Called via RPC call on Service (|to_set|) to set the "Profile" property.
  virtual void SetProfileForService(const ServiceRefPtr &to_set,
                                    const std::string &profile,
                                    Error *error);

  virtual void RegisterDevice(const DeviceRefPtr &to_manage);
  virtual void DeregisterDevice(const DeviceRefPtr &to_forget);

  virtual bool HasService(const ServiceRefPtr &service);
  // Register a Service with the Manager. Manager may choose to
  // connect to it immediately.
  virtual void RegisterService(const ServiceRefPtr &to_manage);
  // Deregister a Service from the Manager. Caller is responsible
  // for disconnecting the Service before-hand.
  virtual void DeregisterService(const ServiceRefPtr &to_forget);
  virtual void UpdateService(const ServiceRefPtr &to_update);

  // Persists |to_update| into an appropriate profile.
  virtual void UpdateDevice(const DeviceRefPtr &to_update);

  virtual void UpdateWiFiProvider();

  void FilterByTechnology(Technology::Identifier tech,
                          std::vector<DeviceRefPtr> *found) const;

  ServiceRefPtr FindService(const std::string& name);
  RpcIdentifiers EnumerateAvailableServices(Error *error);

  // Return the complete list of services, including those that are not visible.
  RpcIdentifiers EnumerateCompleteServices(Error *error);

  // called via RPC (e.g., from ManagerDBusAdaptor)
  std::map<std::string, std::string> GetLoadableProfileEntriesForService(
      const ServiceConstRefPtr &service);
  ServiceRefPtr GetService(const KeyValueStore &args, Error *error);
  ServiceRefPtr ConfigureService(const KeyValueStore &args, Error *error);
  ServiceRefPtr ConfigureServiceForProfile(
      const std::string &profile_rpcid,
      const KeyValueStore &args,
      Error *error);
  ServiceRefPtr FindMatchingService(const KeyValueStore &args, Error *error);

  // Requests that NICs be programmed to wake up from
  // suspend on the arrival of packets on any TCP connection
  // with a source IP address matching the IP address
  // specified in the string argument.
  void AddWakeOnPacketConnection(const std::string &ip_endpoint,
                                 Error *error);
  // Removes a NIC programming request established by AddWakeOnPacketConnection.
  void RemoveWakeOnPacketConnection(const std::string &ip_endpoint,
                                    Error *error);
  // Removes all NIC programming requests.
  void RemoveAllWakeOnPacketConnections(Error *error);

  // Retrieve geolocation data from the Manager.
  const std::map<std::string, GeolocationInfos>
      &GetNetworksForGeolocation() const;

  // Called by Device when its geolocation data has been updated.
  virtual void OnDeviceGeolocationInfoUpdated(const DeviceRefPtr &device);

  void ConnectToBestServices(Error *error);

  // Method to create connectivity report for connected services.
  void CreateConnectivityReport(Error *error);

  // Request portal detection checks on each registered device until a portal
  // detection attempt starts on one of them.
  void RecheckPortal(Error *error);
  // Request portal detection be restarted on the device connected to
  // |service|.
  virtual void RecheckPortalOnService(const ServiceRefPtr &service);

  virtual void RequestScan(Device::ScanType scan_type,
                           const std::string &technology, Error *error);
  std::string GetTechnologyOrder();
  void SetTechnologyOrder(const std::string &order, Error *error);
  // Set up the profile list starting with a default profile along with
  // an (optional) list of startup profiles.
  void InitializeProfiles();
  // Create a profile.  This does not affect the profile stack.  Returns
  // the RPC path of the created profile in |path|.
  void CreateProfile(const std::string &name, std::string *path, Error *error);
  // Pushes existing profile with name |name| onto stack of managed profiles.
  // Returns the RPC path of the pushed profile in |path|.
  void PushProfile(const std::string &name, std::string *path, Error *error);
  // Insert an existing user profile with name |name| into the stack of
  // managed profiles.  Associate |user_hash| with this profile entry.
  // Returns the RPC path of the pushed profile in |path|.
  void InsertUserProfile(const std::string &name,
                         const std::string &user_hash,
                         std::string *path,
                         Error *error);
  // Pops profile named |name| off the top of the stack of managed profiles.
  void PopProfile(const std::string &name, Error *error);
  // Remove the active profile.
  void PopAnyProfile(Error *error);
  // Remove all user profiles from the stack of managed profiles leaving only
  // default profiles.
  void PopAllUserProfiles(Error *error);
  // Remove the underlying persistent storage for a profile.
  void RemoveProfile(const std::string &name, Error *error);
  // Called by a service to remove its associated configuration.  If |service|
  // is associated with a non-ephemeral profile, this configuration entry
  // will be removed and the manager will search for another matching profile.
  // If the service ends up with no matching profile, it is unloaded (which
  // may also remove the service from the manager's list, e.g. WiFi services
  // that are not visible)..
  void RemoveService(const ServiceRefPtr &service);
  // Handle the event where a profile is about to remove a profile entry.
  // Any Services that are dependent on this storage identifier will need
  // to find new profiles.  Return true if any service has been moved to a new
  // profile.  Any such services will have had the profile group removed from
  // the profile.
  virtual bool HandleProfileEntryDeletion(const ProfileRefPtr &profile,
                                          const std::string &entry_name);
  // Find a registered service that contains a GUID property that
  // matches |guid|.
  virtual ServiceRefPtr GetServiceWithGUID(const std::string &guid,
                                           Error *error);
  // Find a service that is both the member of |profile| and has a
  // storage identifier that matches |entry_name|.  This function is
  // called by the Profile in order to return a profile entry's properties.
  virtual ServiceRefPtr GetServiceWithStorageIdentifier(
      const ProfileRefPtr &profile,
      const std::string &entry_name,
      Error *error);
  // Return a reference to the Service associated with the default connection.
  // If there is no such connection, this function returns a reference to NULL.
  virtual ServiceRefPtr GetDefaultService() const;

  // Set enabled state of all |technology_name| devices to |enabled_state|.
  void SetEnabledStateForTechnology(const std::string &technology_name,
                                    bool enabled_state,
                                    Error *error,
                                    const ResultCallback &callback);
  // Return whether a technology is marked as enabled for portal detection.
  virtual bool IsPortalDetectionEnabled(Technology::Identifier tech);
  // Set the start-up value for the portal detection list.  This list will
  // be used until a value set explicitly over the control API.  Until
  // then, we ignore but do not overwrite whatever value is stored in the
  // profile.
  void SetStartupPortalList(const std::string &portal_list);

  // Returns true if profile |a| has been pushed on the Manager's
  // |profiles_| stack before profile |b|.
  bool IsProfileBefore(const ProfileRefPtr &a,
                       const ProfileRefPtr &b) const;

  // Return whether a service belongs to the ephemeral profile.
  virtual bool IsServiceEphemeral(const ServiceConstRefPtr &service) const;

  // Return whether a Technology has any connected Services.
  virtual bool IsTechnologyConnected(Technology::Identifier technology) const;

  // Return whether a technology is enabled for link monitoring.
  virtual bool IsTechnologyLinkMonitorEnabled(
      Technology::Identifier technology) const;

  // Return whether the Wake on LAN feature is enabled.
  virtual bool IsWakeOnLanEnabled() const { return is_wake_on_lan_enabled_; }

  // Return whether a technology is disabled for auto-connect.
  virtual bool IsTechnologyAutoConnectDisabled(
      Technology::Identifier technology) const;

  // Called by Profile when a |storage| completes initialization.
  void OnProfileStorageInitialized(Profile *storage);

  // Return a Device with technology |technology| in the enabled state.
  DeviceRefPtr GetEnabledDeviceWithTechnology(
      Technology::Identifier technology) const;

  // Returns true if at least one connection exists, and false if there's no
  // connected service.
  virtual bool IsConnected() const;
  // Returns true if at least one connection exists that have Internet
  // connectivity, and false if there's no such service.
  virtual bool IsOnline() const;
  std::string CalculateState(Error *error);

  // Recalculate the |connected_state_| string and emit a singal if it has
  // changed.
  void RefreshConnectionState();

  virtual int GetPortalCheckInterval() const {
    return props_.portal_check_interval_seconds;
  }
  virtual const std::string &GetPortalCheckURL() const {
    return props_.portal_url;
  }

  DBusManager *dbus_manager() const { return dbus_manager_.get(); }
  virtual DeviceInfo *device_info() { return &device_info_; }
#if !defined(DISABLE_CELLULAR)
  virtual ModemInfo *modem_info() { return &modem_info_; }
#endif  // DISABLE_CELLULAR
  PowerManager *power_manager() const { return power_manager_.get(); }
  virtual EthernetEapProvider *ethernet_eap_provider() const {
    return ethernet_eap_provider_.get();
  }
  VPNProvider *vpn_provider() const { return vpn_provider_.get(); }
  WiFiProvider *wifi_provider() const { return wifi_provider_.get(); }
#if !defined(DISABLE_WIMAX)
  virtual WiMaxProvider *wimax_provider() { return wimax_provider_.get(); }
#endif  // DISABLE_WIMAX
  PropertyStore *mutable_store() { return &store_; }
  virtual const PropertyStore &store() const { return store_; }
  GLib *glib() const { return glib_; }
  virtual const base::FilePath &run_path() const { return run_path_; }
  const base::FilePath &storage_path() const { return storage_path_; }
  IPAddressStore * health_checker_remote_ips() const {
    return health_checker_remote_ips_.get();
  }

  bool GetArpGateway() const { return props_.arp_gateway; }
  const std::string &GetHostName() const { return props_.host_name; }

  virtual void UpdateEnabledTechnologies();
  virtual void UpdateUninitializedTechnologies();

  // Writes the service |to_update| to persistant storage.  If the service's is
  // ephemeral, it is moved to the current profile.
  void SaveServiceToProfile(const ServiceRefPtr &to_update);

  // Adds a closure to be executed when ChromeOS suspends or shill terminates.
  // |name| should be unique; otherwise, a previous closure by the same name
  // will be replaced.  |start| will be called when RunTerminationActions() is
  // called.  When an action completed, TerminationActionComplete() must be
  // called.
  void AddTerminationAction(const std::string &name,
                            const base::Closure &start);

  // Users call this function to report the completion of an action |name|.
  // This function should be called once for each action.
  void TerminationActionComplete(const std::string &name);

  // Removes the action associtated with |name|.
  void RemoveTerminationAction(const std::string &name);

  // Runs the termination actions and notifies the metrics framework
  // that the termination actions started running, only if any termination
  // actions have been registered. |reason| specifies whether this method was
  // called due to termination or suspend. If all actions complete within
  // |kTerminationActionsTimeoutMilliseconds|, |done_callback| is called with a
  // value of Error::kSuccess. Otherwise, it is called with
  // Error::kOperationTimeout.
  //
  // Returns true, if termination actions were run.
  bool RunTerminationActionsAndNotifyMetrics(
      const ResultCallback &done_callback,
      Metrics::TerminationActionReason reason);

  // Registers a |callback| that's invoked whenever the default service
  // changes. Returns a unique tag that can be used to deregister the
  // callback. A tag equal to 0 is invalid.
  virtual int RegisterDefaultServiceCallback(const ServiceCallback &callback);
  virtual void DeregisterDefaultServiceCallback(int tag);

  // Verifies that the destination described by certificate is valid, and that
  // we're currently connected to that destination.  A full description of the
  // rules being enforced is in doc/manager-api.txt.  Returns true iff all
  // checks pass, false otherwise.  On false, error is filled with a
  // descriptive error code and message.
  //
  // |certificate| is a PEM encoded x509 certificate, |public_key| is a base64
  // encoded public half of an RSA key, |nonce| is a random string, and
  // |signed_data| is a base64 encoded string as described in
  // doc/manager-api.txt.
  void VerifyDestination(const std::string &certificate,
                         const std::string &public_key,
                         const std::string &nonce,
                         const std::string &signed_data,
                         const std::string &destination_udn,
                         const std::string &hotspot_ssid,
                         const std::string &hotspot_bssid,
                         const ResultBoolCallback &cb,
                         Error *error);

  // After verifying the destination, encrypt the string data with
  // |public_key|, the base64 encoded public half of an RSA key pair.  Returns
  // the base64 encoded result if successful, or an empty string on failure.
  // On failure, |error| will be filled with an appropriately descriptive
  // message and error code.
  void VerifyAndEncryptData(const std::string &certificate,
                            const std::string &public_key,
                            const std::string &nonce,
                            const std::string &signed_data,
                            const std::string &destination_udn,
                            const std::string &hotspot_ssid,
                            const std::string &hotspot_bssid,
                            const std::string &data,
                            const ResultStringCallback &cb,
                            Error *error);

  // After verifying the destination, encrypt the password for |network_path|
  // under |public_key|.  Similar to EncryptData above except that the
  // information being encrypted is implicitly the authentication credentials
  // of the given network.
  void VerifyAndEncryptCredentials(const std::string &certificate,
                                   const std::string &public_key,
                                   const std::string &nonce,
                                   const std::string &signed_data,
                                   const std::string &destination_udn,
                                   const std::string &hotspot_ssid,
                                   const std::string &hotspot_bssid,
                                   const std::string &network_path,
                                   const ResultStringCallback &cb,
                                   Error *error);

  // Calculate connection identifier, which is hash of salt value, gateway IP
  // address, and gateway MAC address.
  int CalcConnectionId(std::string gateway_ip, std::string gateway_mac);

  // Report the number of services associated with given connection
  // |connection_id|.
  void ReportServicesOnSameNetwork(int connection_id);

 private:
  friend class CellularTest;
  friend class DeviceInfoTest;
  friend class ManagerAdaptorInterface;
  friend class ManagerTest;
  friend class ModemInfoTest;
  friend class ModemManagerTest;
  friend class ServiceTest;
  friend class VPNServiceTest;
  friend class WiFiObjectTest;
  friend class WiMaxProviderTest;

  FRIEND_TEST(CellularCapabilityUniversalMainTest, TerminationAction);
  FRIEND_TEST(CellularCapabilityUniversalMainTest,
              TerminationActionRemovedByStopModem);
  FRIEND_TEST(CellularTest, LinkEventWontDestroyService);
  FRIEND_TEST(ManagerTest, AvailableTechnologies);
  FRIEND_TEST(ManagerTest, ConnectedTechnologies);
  FRIEND_TEST(ManagerTest, ConnectionStatusCheck);
  FRIEND_TEST(ManagerTest, ConnectToBestServices);
  FRIEND_TEST(ManagerTest, CreateConnectivityReport);
  FRIEND_TEST(ManagerTest, DefaultTechnology);
  FRIEND_TEST(ManagerTest, DevicePresenceStatusCheck);
  FRIEND_TEST(ManagerTest, DeviceRegistrationAndStart);
  FRIEND_TEST(ManagerTest, DisableTechnology);
  FRIEND_TEST(ManagerTest, EnableTechnology);
  FRIEND_TEST(ManagerTest, EnumerateProfiles);
  FRIEND_TEST(ManagerTest, HandleProfileEntryDeletionWithUnload);
  FRIEND_TEST(ManagerTest, InitializeProfilesInformsProviders);
  FRIEND_TEST(ManagerTest, InitializeProfilesHandlesDefaults);
  FRIEND_TEST(ManagerTest, IsDefaultProfile);
  FRIEND_TEST(ManagerTest, IsTechnologyAutoConnectDisabled);
  FRIEND_TEST(ManagerTest, IsWifiIdle);
  FRIEND_TEST(ManagerTest, LinkMonitorEnabled);
  FRIEND_TEST(ManagerTest, MoveService);
  FRIEND_TEST(ManagerTest, NotifyDefaultServiceChanged);
  FRIEND_TEST(ManagerTest, PopProfileWithUnload);
  FRIEND_TEST(ManagerTest, RegisterKnownService);
  FRIEND_TEST(ManagerTest, RegisterUnknownService);
  FRIEND_TEST(ManagerTest, RunTerminationActions);
  FRIEND_TEST(ManagerTest, ServiceRegistration);
  FRIEND_TEST(ManagerTest, SortServices);
  FRIEND_TEST(ManagerTest, SortServicesWithConnection);
  FRIEND_TEST(ManagerTest, StartupPortalList);
  FRIEND_TEST(ServiceTest, IsAutoConnectable);

  static const char kErrorNoDevice[];
  static const char kErrorTypeRequired[];
  static const char kErrorUnsupportedServiceType[];

  // Technologies to probe for.
  static const char *kProbeTechnologies[];

  // Timeout interval for probing various device status, and report them to
  // UMA stats.
  static const int kDeviceStatusCheckIntervalMilliseconds;
  // Time to wait for termination actions to complete.
  static const int kTerminationActionsTimeoutMilliseconds;

  void AutoConnect();
  std::vector<std::string> AvailableTechnologies(Error *error);
  std::vector<std::string> ConnectedTechnologies(Error *error);
  std::string DefaultTechnology(Error *error);
  std::vector<std::string> EnabledTechnologies(Error *error);
  std::vector<std::string> UninitializedTechnologies(Error *error);
  RpcIdentifiers EnumerateDevices(Error *error);
  RpcIdentifiers EnumerateProfiles(Error *error);
  // TODO(cmasone): This should be implemented by filtering |services_|.
  RpcIdentifiers EnumerateWatchedServices(Error *error);
  std::string GetActiveProfileRpcIdentifier(Error *error);
  std::string GetCheckPortalList(Error *error);
  RpcIdentifier GetDefaultServiceRpcIdentifier(Error *error);
  std::string GetIgnoredDNSSearchPaths(Error *error);
  ServiceRefPtr GetServiceInner(const KeyValueStore &args, Error *error);
  bool SetCheckPortalList(const std::string &portal_list, Error *error);
  bool SetIgnoredDNSSearchPaths(const std::string &ignored_paths, Error *error);
  void EmitDefaultService();
  bool IsTechnologyInList(const std::string &technology_list,
                          Technology::Identifier tech) const;
  void EmitDeviceProperties();
  bool SetDisableWiFiVHT(const bool &disable_wifi_vht, Error *error);
  bool GetDisableWiFiVHT(Error *error);

  // Unload a service while iterating through |services_|.  Returns true if
  // service was erased (which means the caller loop should not increment
  // |service_iterator|), false otherwise (meaning the caller should
  // increment |service_iterator|).
  bool UnloadService(std::vector<ServiceRefPtr>::iterator *service_iterator);

  // Load Manager default properties from |profile|.
  void LoadProperties(const scoped_refptr<DefaultProfile> &profile);

  // Configure the device with profile data from all current profiles.
  void LoadDeviceFromProfiles(const DeviceRefPtr &device);

  void HelpRegisterConstDerivedRpcIdentifier(
      const std::string &name,
      RpcIdentifier(Manager::*get)(Error *));
  void HelpRegisterConstDerivedRpcIdentifiers(
      const std::string &name,
      RpcIdentifiers(Manager::*get)(Error *));
  void HelpRegisterDerivedString(
      const std::string &name,
      std::string(Manager::*get)(Error *error),
      bool(Manager::*set)(const std::string&, Error *));
  void HelpRegisterConstDerivedStrings(
      const std::string &name,
      Strings(Manager::*get)(Error *));
  void HelpRegisterDerivedBool(
      const std::string &name,
      bool(Manager::*get)(Error *error),
      bool(Manager::*set)(const bool &value, Error *error));

  bool HasProfile(const Profile::Identifier &ident);
  void PushProfileInternal(const Profile::Identifier &ident,
                           std::string *path,
                           Error *error);
  void PopProfileInternal();
  void OnProfilesChanged();

  void SortServices();
  void SortServicesTask();
  void DeviceStatusCheckTask();
  void ConnectionStatusCheck();
  void DevicePresenceStatusCheck();

  bool MatchProfileWithService(const ServiceRefPtr &service);

  // Sets the profile of |service| to |profile|, without notifying its
  // previous profile.  Configures a |service| with |args|, then saves
  // the resulting configuration to |profile|.  This method is useful
  // when copying a service configuration from one profile to another,
  // or writing a newly created service config to a specific profile.
  static void SetupServiceInProfile(ServiceRefPtr service,
                                    ProfileRefPtr profile,
                                    const KeyValueStore &args,
                                    Error *error);

  // For each technology present, connect to the "best" service available,
  // as determined by sorting all services independent of their current state.
  void ConnectToBestServicesTask();

  void NotifyDefaultServiceChanged(const ServiceRefPtr &service);

  // Runs the termination actions.  If all actions complete within
  // |kTerminationActionsTimeoutMilliseconds|, |done_callback| is called with a
  // value of Error::kSuccess.  Otherwise, it is called with
  // Error::kOperationTimeout.
  void RunTerminationActions(const ResultCallback &done_callback);

  // Called when the system is about to be suspended.  Each call will be
  // followed by a call to OnSuspendDone().
  void OnSuspendImminent();

  // Called when the system has completed a suspend attempt (possibly without
  // actually suspending, in the event of the user canceling the attempt).
  void OnSuspendDone();

  // Called when the system is entering a dark resume phase (and hence a dark
  // suspend is imminent).
  void OnDarkSuspendImminent();

  void OnSuspendActionsComplete(const Error &error);
  void VerifyToEncryptLink(std::string public_key, std::string data,
                           ResultStringCallback cb, const Error &error,
                           bool success);

  // Return true if wifi device is enabled with no existing connection (pending
  // or connected).
  bool IsWifiIdle();

  // For unit testing.
  void set_metrics(Metrics *metrics) { metrics_ = metrics; }
  void UpdateProviderMapping();

  // Used by tests to set a mock PowerManager.  Takes ownership of
  // power_manager.
  void set_power_manager(PowerManager *power_manager) {
    power_manager_.reset(power_manager);
  }

  // Removes all wake on packet connections rules registered on the device
  // that is connected to the old service and adds them to the device connected
  // to the new service
  void TransferWakeOnPacketConnections(const ServiceRefPtr &old_service,
                                       const ServiceRefPtr &new_service);

  DeviceRefPtr GetDeviceConnectedToService(ServiceRefPtr service);

  EventDispatcher *dispatcher_;
  const base::FilePath run_path_;
  const base::FilePath storage_path_;
  const std::string user_storage_path_;
  base::FilePath user_profile_list_path_;  // Changed in tests.
  scoped_ptr<ManagerAdaptorInterface> adaptor_;
  scoped_ptr<DBusManager> dbus_manager_;
  DeviceInfo device_info_;
#if !defined(DISABLE_CELLULAR)
  ModemInfo modem_info_;
#endif  // DISABLE_CELLULAR
  scoped_ptr<EthernetEapProvider> ethernet_eap_provider_;
  scoped_ptr<VPNProvider> vpn_provider_;
  scoped_ptr<WiFiProvider> wifi_provider_;
#if !defined(DISABLE_WIMAX)
  scoped_ptr<WiMaxProvider> wimax_provider_;
#endif  // DISABLE_WIMAX
  // Hold pointer to singleton Resolver instance for testing purposes.
  Resolver *resolver_;
  bool running_;
  // Used to facilitate unit tests which can't use RPC.
  bool connect_profiles_to_rpc_;
  std::vector<DeviceRefPtr> devices_;
  // We store Services in a vector, because we want to keep them sorted.
  // Services that are connected appear first in the vector.  See
  // Service::Compare() for details of the sorting criteria.
  std::vector<ServiceRefPtr> services_;
  // Map of technologies to Provider instances.  These pointers are owned
  // by the respective scoped_reptr objects that are held over the lifetime
  // of the Manager object.
  std::map<Technology::Identifier, ProviderInterface *> providers_;
  // List of startup profile names to push on the profile stack on startup.
  std::vector<ProfileRefPtr> profiles_;
  ProfileRefPtr ephemeral_profile_;
  ControlInterface *control_interface_;
  Metrics *metrics_;
  GLib *glib_;
  scoped_ptr<PowerManager> power_manager_;

  // The priority order of technologies
  std::vector<Technology::Identifier> technology_order_;

  // This is the last Service RPC Identifier for which we emitted a
  // "DefaultService" signal for.
  RpcIdentifier default_service_rpc_identifier_;

  // Manager can be optionally configured with a list of technologies to
  // do portal detection on at startup.  We need to keep track of that list
  // as well as a flag that tells us whether we should continue using it
  // instead of the configured portal list.
  std::string startup_portal_list_;
  bool use_startup_portal_list_;

  // Properties to be get/set via PropertyStore calls.
  Properties props_;
  PropertyStore store_;

  base::CancelableClosure sort_services_task_;

  // Task for periodically checking various device status.
  base::CancelableClosure device_status_check_task_;

  // TODO(petkov): Currently this handles both terminate and suspend
  // actions. Rename all relevant identifiers to capture this.
  HookTable termination_actions_;

  // Is a suspend delay currently registered with the power manager?
  bool suspend_delay_registered_;

  // Whether Wake on LAN should be enabled for all Ethernet devices.
  bool is_wake_on_lan_enabled_;

  // Maps tags to callbacks for monitoring default service changes.
  std::map<int, ServiceCallback> default_service_callbacks_;
  int default_service_callback_tag_;

  // Delegate to handle destination verification operations for the manager.
  scoped_ptr<CryptoUtilProxy> crypto_util_proxy_;

  // Stores IP addresses of some remote hosts that accept port 80 TCP
  // connections. ConnectionHealthChecker uses these IPs.
  // The store resides in Manager so that it persists across Device reset.
  scoped_ptr<IPAddressStore> health_checker_remote_ips_;

  // Stores the most recent copy of geolocation information for each
  // technology type.
  std::map<std::string, GeolocationInfos> networks_for_geolocation_;

  // Stores the state of the highest ranked connected service.
  std::string connection_state_;

  DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace shill

#endif  // SHILL_MANAGER_H_
