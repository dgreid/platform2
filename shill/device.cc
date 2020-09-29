// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/device.h"

#include <errno.h>
#include <netinet/in.h>
#include <linux/if.h>  // NOLINT - Needs definitions from netinet/in.h
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/memory/ref_counted.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/connection.h"
#include "shill/control_interface.h"
#include "shill/dhcp/dhcp_config.h"
#include "shill/dhcp/dhcp_properties.h"
#include "shill/dhcp/dhcp_provider.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/icmp.h"
#include "shill/ip_address_store.h"
#include "shill/link_monitor.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/net/ip_address.h"
#include "shill/net/ndisc.h"
#include "shill/net/rtnl_handler.h"
#include "shill/property_accessor.h"
#include "shill/refptr_types.h"
#include "shill/routing_table.h"
#include "shill/routing_table_entry.h"
#include "shill/service.h"
#include "shill/socket_info_reader.h"
#include "shill/store_interface.h"
#include "shill/technology.h"
#include "shill/tethering.h"
#include "shill/traffic_monitor.h"

using base::Bind;
using base::BindOnce;
using base::Callback;
using base::FilePath;
using base::StringPrintf;
using std::set;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDevice;
static string ObjectID(const Device* d) {
  return d->GetRpcIdentifier().value();
}
}  // namespace Logging

namespace {

constexpr char kIPFlagTemplate[] = "/proc/sys/net/%s/conf/%s/%s";
constexpr char kIPFlagVersion4[] = "ipv4";
constexpr char kIPFlagVersion6[] = "ipv6";
constexpr char kIPFlagUseTempAddr[] = "use_tempaddr";
constexpr char kIPFlagUseTempAddrUsedAndDefault[] = "2";
constexpr char kIPFlagAcceptRouterAdvertisementsAlways[] = "2";
constexpr char kIPFlagReversePathFilter[] = "rp_filter";
constexpr char kIPFlagReversePathFilterEnabled[] = "1";
constexpr char kIPFlagReversePathFilterLooseMode[] = "2";
constexpr char kIPFlagArpAnnounce[] = "arp_announce";
constexpr char kIPFlagArpAnnounceDefault[] = "0";
constexpr char kIPFlagArpAnnounceBestLocal[] = "2";
constexpr char kIPFlagArpIgnore[] = "arp_ignore";
constexpr char kIPFlagArpIgnoreDefault[] = "0";
constexpr char kIPFlagArpIgnoreLocalOnly[] = "1";
const char* const kFallbackDnsServers[] = {"8.8.8.8", "8.8.4.4"};
constexpr size_t kHardwareAddressLength = 6;

// Maximum seconds between two link monitor failures to declare this link
// (network) as unreliable.
constexpr int kLinkUnreliableThresholdSeconds = 60 * 60;

Service::ConnectState CalculatePortalStateFromProbeResults(
    const PortalDetector::Result& http_result,
    const PortalDetector::Result& https_result) {
  if (http_result.phase != PortalDetector::Phase::kContent) {
    return Service::kStateNoConnectivity;
  }
  if (http_result.status == PortalDetector::Status::kSuccess &&
      https_result.status == PortalDetector::Status::kSuccess) {
    return Service::kStateOnline;
  }
  if (http_result.status == PortalDetector::Status::kRedirect) {
    return http_result.redirect_url_string.empty()
               ? Service::kStatePortalSuspected
               : Service::kStateRedirectFound;
  }
  if (http_result.status == PortalDetector::Status::kTimeout &&
      https_result.status != PortalDetector::Status::kSuccess) {
    return Service::kStateNoConnectivity;
  }
  return Service::kStatePortalSuspected;
}

}  // namespace

const char Device::kIPFlagDisableIPv6[] = "disable_ipv6";
const char Device::kIPFlagAcceptRouterAdvertisements[] = "accept_ra";
const char Device::kStoragePowered[] = "Powered";
const char Device::kStorageReceiveByteCount[] = "ReceiveByteCount";
const char Device::kStorageTransmitByteCount[] = "TransmitByteCount";

Device::Device(Manager* manager,
               const string& link_name,
               const string& mac_address,
               int interface_index,
               Technology technology)
    : enabled_(false),
      enabled_persistent_(true),
      enabled_pending_(enabled_),
      mac_address_(base::ToLowerASCII(mac_address)),
      interface_index_(interface_index),
      running_(false),
      link_name_(link_name),
      manager_(manager),
      adaptor_(manager->control_interface()->CreateDeviceAdaptor(this)),
      technology_(technology),
      portal_check_interval_seconds_(0),
      receive_byte_offset_(0),
      transmit_byte_offset_(0),
      dhcp_provider_(DHCPProvider::GetInstance()),
      routing_table_(RoutingTable::GetInstance()),
      rtnl_handler_(RTNLHandler::GetInstance()),
      time_(Time::GetInstance()),
      last_link_monitor_failed_time_(0),
      ipv6_disabled_(false),
      is_loose_routing_(false),
      is_multi_homed_(false),
      fixed_ip_params_(false),
      traffic_counter_callback_id_(0),
      weak_ptr_factory_(this) {
  store_.RegisterConstString(kAddressProperty, &mac_address_);

  // kBgscanMethodProperty: Registered in WiFi
  // kBgscanShortIntervalProperty: Registered in WiFi
  // kBgscanSignalThresholdProperty: Registered in WiFi

  // kCellularAllowRoamingProperty: Registered in Cellular
  // kCarrierProperty: Registered in Cellular
  // kEsnProperty: Registered in Cellular
  // kHomeProviderProperty: Registered in Cellular
  // kImeiProperty: Registered in Cellular
  // kIccidProperty: Registered in Cellular
  // kImsiProperty: Registered in Cellular
  // kManufacturerProperty: Registered in Cellular
  // kMdnProperty: Registered in Cellular
  // kMeidProperty: Registered in Cellular
  // kMinProperty: Registered in Cellular
  // kModelIdProperty: Registered in Cellular
  // kFirmwareRevisionProperty: Registered in Cellular
  // kHardwareRevisionProperty: Registered in Cellular
  // kDeviceIdProperty: Registered in Cellular
  // kSIMLockStatusProperty: Registered in Cellular
  // kFoundNetworksProperty: Registered in Cellular
  // kDBusObjectProperty: Register in Cellular

  store_.RegisterConstString(kInterfaceProperty, &link_name_);
  HelpRegisterDerivedBool(kIPv6DisabledProperty, &Device::GetIPv6Disabled,
                          &Device::SetIPv6Disabled, &Device::ClearIPv6Disabled);
  HelpRegisterConstDerivedRpcIdentifier(
      kSelectedServiceProperty, &Device::GetSelectedServiceRpcIdentifier);
  HelpRegisterConstDerivedRpcIdentifiers(kIPConfigsProperty,
                                         &Device::AvailableIPConfigs);
  store_.RegisterConstString(kNameProperty, &link_name_);
  store_.RegisterConstBool(kPoweredProperty, &enabled_);
  HelpRegisterConstDerivedString(kTypeProperty, &Device::GetTechnologyString);
  HelpRegisterConstDerivedUint64(kLinkMonitorResponseTimeProperty,
                                 &Device::GetLinkMonitorResponseTime);

  // kScanningProperty: Registered in WiFi, Cellular
  // kScanIntervalProperty: Registered in WiFi, Cellular
  // kWakeOnWiFiFeaturesEnabledProperty: Registered in WiFi

  if (manager_ && manager_->device_info()) {  // Unit tests may not have these.
    manager_->device_info()->GetByteCounts(
        interface_index_, &receive_byte_offset_, &transmit_byte_offset_);
    HelpRegisterConstDerivedUint64(kReceiveByteCountProperty,
                                   &Device::GetReceiveByteCountProperty);
    HelpRegisterConstDerivedUint64(kTransmitByteCountProperty,
                                   &Device::GetTransmitByteCountProperty);
  }

  SLOG(this, 1) << "Device(): " << link_name_ << " index: " << interface_index_;
}

Device::~Device() {
  SLOG(this, 1) << "~Device(): " << link_name_
                << " index: " << interface_index_;
}

void Device::Initialize() {
  SLOG(this, 2) << "Initialized";
  DisableArpFiltering();
  EnableReversePathFilter();
}

void Device::LinkEvent(unsigned flags, unsigned change) {
  SLOG(this, 2) << base::StringPrintf("Device %s flags 0x%x changed 0x%x",
                                      link_name_.c_str(), flags, change);
}

void Device::Scan(Error* error, const string& reason) {
  SLOG(this, 2) << __func__ << " [Device] on " << link_name() << " from "
                << reason;
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                        "Device doesn't support scan.");
}

void Device::RegisterOnNetwork(const std::string& /*network_id*/,
                               Error* error,
                               const ResultCallback& /*callback*/) {
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                        "Device doesn't support network registration.");
}

void Device::RequirePin(const string& /*pin*/,
                        bool /*require*/,
                        Error* error,
                        const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                        "Device doesn't support RequirePin.");
}

void Device::EnterPin(const string& /*pin*/,
                      Error* error,
                      const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                        "Device doesn't support EnterPin.");
}

void Device::UnblockPin(const string& /*unblock_code*/,
                        const string& /*pin*/,
                        Error* error,
                        const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                        "Device doesn't support UnblockPin.");
}

void Device::ChangePin(const string& /*old_pin*/,
                       const string& /*new_pin*/,
                       Error* error,
                       const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                        "Device doesn't support ChangePin.");
}

void Device::Reset(Error* error, const ResultCallback& /*callback*/) {
  SLOG(this, 2) << __func__;
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                        "Device doesn't support Reset.");
}

void Device::RefreshIPConfig(Error* /*error*/) {
  SLOG(this, 2) << __func__;
  if (ipconfig_) {
    ipconfig_->Refresh();
  }
}

bool Device::GetIPv6Disabled(Error* error) {
  return ipv6_disabled_;
}

bool Device::SetIPv6Disabled(const bool& disabling, Error* /*error*/) {
  if (disabling == ipv6_disabled_)
    return false;
  ipv6_disabled_ = disabling;
  if (ipv6_disabled_) {
    StopIPv6();
  } else {
    StartIPv6();
  }
  return true;
}

void Device::ClearIPv6Disabled(Error* error) {
  SetIPv6Disabled(IsIPv6DisabledByDefault(), error);
}

bool Device::IsIPv6DisabledByDefault() const {
  return false;
}

void Device::StopIPv6() {
  SLOG(this, 2) << __func__;
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagDisableIPv6, "1");
}

void Device::StartIPv6() {
  SLOG(this, 2) << __func__;
  if (ipv6_disabled_) {
    LOG(INFO) << "Skip enabling IPv6 on " << link_name_
              << " as it is disabled.";
    return;
  }
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagDisableIPv6, "0");

  // Force the kernel to accept RAs even when global IPv6 forwarding is
  // enabled.  Unfortunately this needs to be set on a per-interface basis.
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagAcceptRouterAdvertisements,
            kIPFlagAcceptRouterAdvertisementsAlways);
}

void Device::EnableIPv6Privacy() {
  SetIPFlag(IPAddress::kFamilyIPv6, kIPFlagUseTempAddr,
            kIPFlagUseTempAddrUsedAndDefault);
}

void Device::SetLooseRouting(bool is_loose_routing) {
  if (is_loose_routing == is_loose_routing_) {
    return;
  }
  is_loose_routing_ = is_loose_routing;
  if (is_multi_homed_) {
    // Nothing to do: loose routing is already enabled, and should remain so.
    return;
  }
  if (is_loose_routing) {
    DisableReversePathFilter();
  } else {
    EnableReversePathFilter();
  }
}

void Device::DisableReversePathFilter() {
  // TODO(pstew): Current kernel doesn't offer reverse-path filtering flag
  // for IPv6.  crbug.com/207193
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagReversePathFilter,
            kIPFlagReversePathFilterLooseMode);
}

void Device::EnableReversePathFilter() {
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagReversePathFilter,
            kIPFlagReversePathFilterEnabled);
  // Clear any cached routes that might have accumulated while reverse-path
  // filtering was disabled.
  routing_table_->FlushCache();
}

void Device::SetIsMultiHomed(bool is_multi_homed) {
  if (is_multi_homed == is_multi_homed_) {
    return;
  }
  LOG(INFO) << "Device " << link_name() << " multi-home state is now "
            << is_multi_homed;
  is_multi_homed_ = is_multi_homed;
  if (is_multi_homed) {
    EnableArpFiltering();
    if (!is_loose_routing_) {
      DisableReversePathFilter();
    }
  } else {
    DisableArpFiltering();
    if (!is_loose_routing_) {
      EnableReversePathFilter();
    }
  }
}

void Device::SetFixedIpParams(bool fixed_ip_params) {
  fixed_ip_params_ = fixed_ip_params;
}

void Device::DisableArpFiltering() {
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpAnnounce,
            kIPFlagArpAnnounceDefault);
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpIgnore, kIPFlagArpIgnoreDefault);
}

void Device::EnableArpFiltering() {
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpAnnounce,
            kIPFlagArpAnnounceBestLocal);
  SetIPFlag(IPAddress::kFamilyIPv4, kIPFlagArpIgnore,
            kIPFlagArpIgnoreLocalOnly);
}

bool Device::IsConnected() const {
  if (selected_service_)
    return selected_service_->IsConnected();
  return false;
}

bool Device::IsConnectedToService(const ServiceRefPtr& service) const {
  return service == selected_service_ && IsConnected();
}

bool Device::IsConnectedViaTether() const {
  if (!ipconfig_)
    return false;

  ByteArray vendor_encapsulated_options =
      ipconfig_->properties().vendor_encapsulated_options;
  size_t android_vendor_encapsulated_options_len =
      strlen(Tethering::kAndroidVendorEncapsulatedOptions);

  return (vendor_encapsulated_options.size() ==
          android_vendor_encapsulated_options_len) &&
         !memcmp(&vendor_encapsulated_options[0],
                 Tethering::kAndroidVendorEncapsulatedOptions,
                 vendor_encapsulated_options.size());
}

const RpcIdentifier& Device::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

string Device::GetStorageIdentifier() const {
  return "device_" + mac_address_;
}

vector<GeolocationInfo> Device::GetGeolocationObjects() const {
  return vector<GeolocationInfo>();
}

string Device::GetTechnologyString(Error* /*error*/) {
  return technology().GetName();
}

const string& Device::UniqueName() const {
  return link_name_;
}

bool Device::Load(const StoreInterface* storage) {
  const string id = GetStorageIdentifier();
  if (!storage->ContainsGroup(id)) {
    SLOG(this, 2) << "Device is not available in the persistent store: " << id;
    return false;
  }
  enabled_persistent_ = true;
  storage->GetBool(id, kStoragePowered, &enabled_persistent_);
  uint64_t rx_byte_count = 0, tx_byte_count = 0;

  manager_->device_info()->GetByteCounts(interface_index_, &rx_byte_count,
                                         &tx_byte_count);
  // If there is a byte-count present in the profile, the return value
  // of Device::Get*ByteCount() should be the this stored value plus
  // whatever additional bytes we receive since time-of-load.  We
  // accomplish this by the subtractions below, which can validly
  // roll over "negative" in the subtractions below and in Get*ByteCount.
  uint64_t profile_byte_count;
  if (storage->GetUint64(id, kStorageReceiveByteCount, &profile_byte_count)) {
    receive_byte_offset_ = rx_byte_count - profile_byte_count;
  }
  if (storage->GetUint64(id, kStorageTransmitByteCount, &profile_byte_count)) {
    transmit_byte_offset_ = tx_byte_count - profile_byte_count;
  }

  return true;
}

bool Device::Save(StoreInterface* storage) {
  const string id = GetStorageIdentifier();
  storage->SetBool(id, kStoragePowered, enabled_persistent_);
  storage->SetUint64(id, kStorageReceiveByteCount, GetReceiveByteCount());
  storage->SetUint64(id, kStorageTransmitByteCount, GetTransmitByteCount());
  return true;
}

void Device::OnBeforeSuspend(const ResultCallback& callback) {
  // Nothing to be done in the general case, so immediately report success.
  callback.Run(Error(Error::kSuccess));
}

void Device::OnAfterResume() {
  RenewDHCPLease(false, nullptr);
  if (link_monitor_) {
    SLOG(this, 3) << "Informing Link Monitor of resume.";
    link_monitor_->OnAfterResume();
  }
  // Resume from sleep, could be in different location now.
  // Ignore previous link monitor failures.
  if (selected_service_) {
    selected_service_->set_unreliable(false);
    reliable_link_callback_.Cancel();
  }
  last_link_monitor_failed_time_ = 0;
}

void Device::OnDarkResume(const ResultCallback& callback) {
  // Nothing to be done in the general case, so immediately report success.
  callback.Run(Error(Error::kSuccess));
}

void Device::DropConnection() {
  SLOG(this, 2) << __func__;
  DestroyIPConfig();
  SelectService(nullptr);
}

void Device::ResetConnection() {
  SLOG(this, 2) << __func__;
  DestroyIPConfig();
  if (!selected_service_) {
    return;
  }

  // Refresh traffic counters before deselecting the service.
  FetchTrafficCounters(selected_service_, /*new_service=*/nullptr);
  selected_service_->set_unreliable(false);
  reliable_link_callback_.Cancel();
  selected_service_ = nullptr;
  adaptor_->EmitRpcIdentifierChanged(kSelectedServiceProperty,
                                     GetSelectedServiceRpcIdentifier(nullptr));
}

void Device::DestroyIPConfig() {
  StopIPv6();
  bool ipconfig_changed = false;
  if (ipconfig_) {
    ipconfig_->ReleaseIP(IPConfig::kReleaseReasonDisconnect);
    ipconfig_ = nullptr;
    ipconfig_changed = true;
  }
  if (ip6config_) {
    StopIPv6DNSServerTimer();
    ip6config_ = nullptr;
    ipconfig_changed = true;
  }
  if (dhcpv6_config_) {
    dhcpv6_config_->ReleaseIP(IPConfig::kReleaseReasonDisconnect);
    dhcpv6_config_ = nullptr;
    ipconfig_changed = true;
  }
  // Emit updated IP configs if there are any changes.
  if (ipconfig_changed) {
    UpdateIPConfigsProperty();
  }
  DestroyConnection();
}

void Device::OnIPv6AddressChanged(const IPAddress* address) {
  if (!address) {
    if (ip6config_) {
      ip6config_ = nullptr;
      UpdateIPConfigsProperty();
    }
    return;
  }

  CHECK_EQ(address->family(), IPAddress::kFamilyIPv6);
  IPConfig::Properties properties;
  if (!address->IntoString(&properties.address)) {
    LOG(ERROR) << "Unable to convert IPv6 address into a string";
    return;
  }
  properties.subnet_prefix = address->prefix();

  RoutingTableEntry default_route;
  if (routing_table_->GetDefaultRoute(interface_index_, IPAddress::kFamilyIPv6,
                                      &default_route)) {
    if (!default_route.gateway.IntoString(&properties.gateway)) {
      LOG(ERROR) << "Unable to convert IPv6 gateway into a string";
      return;
    }
  } else {
    // The kernel normally populates the default route before it performs
    // a neighbor solicitation for the new address, so it shouldn't be
    // missing at this point.
    LOG(WARNING) << "No default route for global IPv6 address "
                 << properties.address;
  }

  if (!ip6config_) {
    ip6config_ = new IPConfig(control_interface(), link_name_);
  } else if (properties.address == ip6config_->properties().address &&
             properties.subnet_prefix ==
                 ip6config_->properties().subnet_prefix) {
    SLOG(this, 2) << __func__ << " primary address for " << link_name_
                  << " is unchanged";
    return;
  }

  properties.address_family = IPAddress::kFamilyIPv6;
  properties.method = kTypeIPv6;
  // It is possible for device to receive DNS server notification before IP
  // address notification, so preserve the saved DNS server if it exist.
  properties.dns_servers = ip6config_->properties().dns_servers;
  PrependDNSServers(IPAddress::kFamilyIPv6, &properties.dns_servers);
  ip6config_->set_properties(properties);
  UpdateIPConfigsProperty();
  OnIPv6ConfigUpdated();
}

void Device::OnIPv6DnsServerAddressesChanged() {
  vector<IPAddress> server_addresses;
  uint32_t lifetime = 0;

  // Stop any existing timer.
  StopIPv6DNSServerTimer();

  if (!manager_->device_info()->GetIPv6DnsServerAddresses(
          interface_index_, &server_addresses, &lifetime) ||
      lifetime == 0) {
    IPv6DNSServerExpired();
    return;
  }

  vector<string> addresses_str;
  for (const auto& ip : server_addresses) {
    string address_str;
    if (!ip.IntoString(&address_str)) {
      LOG(ERROR) << "Unable to convert IPv6 address into a string!";
      IPv6DNSServerExpired();
      return;
    }
    addresses_str.push_back(address_str);
  }

  if (!ip6config_) {
    ip6config_ = new IPConfig(control_interface(), link_name_);
  }

  if (lifetime != ND_OPT_LIFETIME_INFINITY) {
    // Setup timer to monitor DNS server lifetime if not infinite lifetime.
    StartIPv6DNSServerTimer(lifetime);
  }

  PrependDNSServers(IPAddress::kFamilyIPv6, &addresses_str);

  // Done if no change in server addresses.
  if (ip6config_->properties().dns_servers == addresses_str) {
    SLOG(this, 2) << __func__ << " IPv6 DNS server list for " << link_name_
                  << " is unchanged.";
    return;
  }

  ip6config_->UpdateDNSServers(std::move(addresses_str));
  UpdateIPConfigsProperty();
  OnIPv6ConfigUpdated();
}

void Device::StartIPv6DNSServerTimer(uint32_t lifetime_seconds) {
  int64_t delay = static_cast<int64_t>(lifetime_seconds) * 1000;
  ipv6_dns_server_expired_callback_.Reset(
      base::Bind(&Device::IPv6DNSServerExpired, base::Unretained(this)));
  dispatcher()->PostDelayedTask(
      FROM_HERE, ipv6_dns_server_expired_callback_.callback(), delay);
}

void Device::StopIPv6DNSServerTimer() {
  ipv6_dns_server_expired_callback_.Cancel();
}

void Device::IPv6DNSServerExpired() {
  if (!ip6config_) {
    return;
  }
  ip6config_->UpdateDNSServers(vector<string>());
  UpdateIPConfigsProperty();
}

void Device::StopAllActivities() {
  StopTrafficMonitor();
  StopPortalDetection();
  StopConnectivityTest();
  StopConnectionDiagnostics();
  StopLinkMonitor();
  StopDNSTest();
  StopIPv6DNSServerTimer();
}

void Device::AddWakeOnPacketConnection(const string& ip_endpoint,
                                       Error* error) {
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotSupported,
      "AddWakeOnPacketConnection not implemented for " + link_name_ + ".");
  return;
}

void Device::AddWakeOnPacketOfTypes(
    const std::vector<std::string>& packet_types, Error* error) {
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotSupported,
      "AddWakeOnPacketOfType not implemented for " + link_name_ + ".");
  return;
}

void Device::RemoveWakeOnPacketConnection(const string& ip_endpoint,
                                          Error* error) {
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotSupported,
      "RemoveWakeOnPacketConnection not implemented for " + link_name_ + ".");
  return;
}

void Device::RemoveWakeOnPacketOfTypes(
    const std::vector<std::string>& packet_types, Error* error) {
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotSupported,
      "RemoveWakeOnPacketOfType not implemented for " + link_name_ + ".");
  return;
}

void Device::RemoveAllWakeOnPacketConnections(Error* error) {
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotSupported,
      "RemoveAllWakeOnPacketConnections not implemented for " + link_name_ +
          ".");
  return;
}

void Device::SetUsbEthernetMacAddressSource(const std::string& source,
                                            Error* error,
                                            const ResultCallback& callback) {
  Error::PopulateAndLog(
      FROM_HERE, error, Error::kNotSupported,
      "SetUsbEthernetMacAddressSource not implemented for " + link_name_ + ".");
  return;
}

void Device::RenewDHCPLease(bool from_dbus, Error* /*error*/) {
  LOG(INFO) << __func__;

  if (ipconfig_) {
    SLOG(this, 3) << "Renewing IPv4 Address";
    ipconfig_->RenewIP();
  }
  if (ip6config_ && !from_dbus) {
    SLOG(this, 3) << "Waiting for new IPv6 configuration";
    // Invalidate the old IPv6 configuration, will receive notifications
    // from kernel for new IPv6 configuration if there is one.
    StopIPv6DNSServerTimer();
    ip6config_ = nullptr;
    UpdateIPConfigsProperty();
  }
  if (dhcpv6_config_) {
    SLOG(this, 3) << "Renewing DHCPv6 lease";
    dhcpv6_config_->RenewIP();
  }
}

bool Device::ShouldUseArpGateway() const {
  return false;
}

bool Device::IsUsingStaticIP() const {
  if (!selected_service_) {
    return false;
  }
  return selected_service_->HasStaticIPAddress();
}

bool Device::IsUsingStaticNameServers() const {
  if (!selected_service_) {
    return false;
  }
  return selected_service_->HasStaticNameServers();
}

bool Device::HasDirectConnectivityTo(const IPAddress& address) const {
  for (const auto& device_address :
       manager()->device_info()->GetAddresses(interface_index())) {
    if (device_address.family() == address.family() &&
        device_address.CanReachAddress(address)) {
      return true;
    }
  }
  return false;
}

bool Device::AcquireIPConfig() {
  return AcquireIPConfigWithLeaseName(string());
}

bool Device::AcquireIPConfigWithLeaseName(const string& lease_name) {
  DestroyIPConfig();
  StartIPv6();
  bool arp_gateway = manager_->GetArpGateway() && ShouldUseArpGateway();
  DhcpProperties dhcp_properties =
      selected_service_
          ? DhcpProperties::Combine(manager_->dhcp_properties(),
                                    selected_service_->dhcp_properties())
          : manager_->dhcp_properties();
  DHCPConfigRefPtr dhcp_config = dhcp_provider_->CreateIPv4Config(
      link_name_, lease_name, arp_gateway, dhcp_properties);
  const int minimum_mtu = manager()->GetMinimumMTU();
  if (minimum_mtu != IPConfig::kUndefinedMTU) {
    dhcp_config->set_minimum_mtu(minimum_mtu);
  }

  ipconfig_ = dhcp_config;
  ipconfig_->RegisterUpdateCallback(
      Bind(&Device::OnIPConfigUpdated, AsWeakPtr()));
  ipconfig_->RegisterFailureCallback(
      Bind(&Device::OnIPConfigFailed, AsWeakPtr()));
  ipconfig_->RegisterRefreshCallback(
      Bind(&Device::OnIPConfigRefreshed, AsWeakPtr()));
  ipconfig_->RegisterExpireCallback(
      Bind(&Device::OnIPConfigExpired, AsWeakPtr()));
  dispatcher()->PostTask(FROM_HERE,
                         Bind(&Device::ConfigureStaticIPTask, AsWeakPtr()));
  if (!ipconfig_->RequestIP()) {
    return false;
  }

#ifndef DISABLE_DHCPV6
  // Only start DHCPv6 configuration instance only if DHCPv6 is enabled
  // for this device.
  if (manager_->IsDHCPv6EnabledForDevice(link_name_)) {
    return AcquireIPv6ConfigWithLeaseName(lease_name);
  }
#endif  // DISABLE_DHCPV6
  return true;
}

#ifndef DISABLE_DHCPV6
bool Device::AcquireIPv6ConfigWithLeaseName(const string& lease_name) {
  auto dhcpv6_config = dhcp_provider_->CreateIPv6Config(link_name_, lease_name);
  dhcpv6_config_ = dhcpv6_config;
  dhcpv6_config_->RegisterUpdateCallback(
      Bind(&Device::OnDHCPv6ConfigUpdated, AsWeakPtr()));
  dhcpv6_config_->RegisterFailureCallback(
      Bind(&Device::OnDHCPv6ConfigFailed, AsWeakPtr()));
  dhcpv6_config_->RegisterExpireCallback(
      Bind(&Device::OnDHCPv6ConfigExpired, AsWeakPtr()));
  if (!dhcpv6_config_->RequestIP()) {
    return false;
  }
  return true;
}
#endif  // DISABLE_DHCPV6

void Device::UpdateBlackholeUserTraffic() {
  SLOG(this, 2) << __func__;
  if (ipconfig_) {
    bool updated;
    if (manager_->ShouldBlackholeUserTraffic(UniqueName())) {
      updated = ipconfig_->SetBlackholedUids(manager_->user_traffic_uids());
    } else {
      updated = ipconfig_->ClearBlackholedUids();
    }
    if (updated) {
      SetupConnection(ipconfig_);
    }
  }
}

void Device::FetchTrafficCounters(const ServiceRefPtr& old_service,
                                  const ServiceRefPtr& new_service) {
  set<string> devices{link_name_};
  patchpanel::Client* client = manager_->patchpanel_client();
  if (!client) {
    return;
  }
  traffic_counter_callback_id_++;
  traffic_counters_callback_map_[traffic_counter_callback_id_] =
      BindOnce(&Device::GetTrafficCountersCallback, AsWeakPtr(), old_service,
               new_service);
  client->GetTrafficCounters(
      devices, BindOnce(&Device::GetTrafficCountersPatchpanelCallback,
                        AsWeakPtr(), traffic_counter_callback_id_));
}

void Device::OnNeighborDisconnected(
    const IPAddress& ip_address,
    patchpanel::NeighborConnectedStateChangedSignal::Role role) {
  metrics()->NotifyNeighborLinkMonitorFailure(technology_, ip_address.family(),
                                              role);
}

void Device::AssignIPConfig(const IPConfig::Properties& properties) {
  DestroyIPConfig();
  StartIPv6();
  ipconfig_ = new IPConfig(control_interface(), link_name_);
  ipconfig_->set_properties(properties);
  dispatcher()->PostTask(FROM_HERE, Bind(&Device::OnIPConfigUpdated,
                                         AsWeakPtr(), ipconfig_, true));
}

void Device::DestroyIPConfigLease(const string& name) {
  dhcp_provider_->DestroyLease(name);
}

void Device::HelpRegisterDerivedBool(const string& name,
                                     bool (Device::*get)(Error* error),
                                     bool (Device::*set)(const bool&, Error*),
                                     void (Device::*clear)(Error*)) {
  store_.RegisterDerivedBool(
      name,
      BoolAccessor(new CustomAccessor<Device, bool>(this, get, set, clear)));
}

void Device::HelpRegisterConstDerivedString(
    const string& name, string (Device::*get)(Error* error)) {
  store_.RegisterDerivedString(
      name,
      StringAccessor(new CustomAccessor<Device, string>(this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedRpcIdentifier(
    const string& name, RpcIdentifier (Device::*get)(Error* error)) {
  store_.RegisterDerivedRpcIdentifier(
      name, RpcIdentifierAccessor(
                new CustomAccessor<Device, RpcIdentifier>(this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedRpcIdentifiers(
    const string& name, RpcIdentifiers (Device::*get)(Error*)) {
  store_.RegisterDerivedRpcIdentifiers(
      name, RpcIdentifiersAccessor(new CustomAccessor<Device, RpcIdentifiers>(
                this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedUint64(const string& name,
                                            uint64_t (Device::*get)(Error*)) {
  store_.RegisterDerivedUint64(
      name,
      Uint64Accessor(new CustomAccessor<Device, uint64_t>(this, get, nullptr)));
}

void Device::ConnectionTesterCallback(
    const PortalDetector::Result& http_result,
    const PortalDetector::Result& https_result) {
  LOG(INFO) << StringPrintf(
      "ConnectionTester completed with HTTP probe phase==%s, "
      "status==%s and HTTPS probe phase==%s, status==%s",
      PortalDetector::PhaseToString(http_result.phase).c_str(),
      PortalDetector::StatusToString(http_result.status).c_str(),
      PortalDetector::PhaseToString(https_result.phase).c_str(),
      PortalDetector::StatusToString(https_result.status).c_str());
  LOG(INFO) << "Device " << link_name() << ": Completed Connectivity Test";

  SetLooseRouting(false);
}

void Device::ConfigureStaticIPTask() {
  SLOG(this, 2) << __func__ << " selected_service " << selected_service_.get()
                << " ipconfig " << ipconfig_.get();

  if (!selected_service_ || !ipconfig_) {
    return;
  }

  if (IsUsingStaticIP()) {
    SLOG(this, 2) << __func__ << " "
                  << " configuring static IP parameters.";
    // If the parameters contain an IP address, apply them now and bring
    // the interface up.  When DHCP information arrives, it will supplement
    // the static information.
    OnIPConfigUpdated(ipconfig_, true);
  } else {
    // Either |ipconfig_| has just been created in AcquireIPConfig() or
    // we're being called by OnIPConfigRefreshed().  In either case a
    // DHCP client has been started, and will take care of calling
    // OnIPConfigUpdated() when it completes.
    SLOG(this, 2) << __func__ << " "
                  << " no static IP address.";
  }
}

bool Device::IPConfigCompleted(const IPConfigRefPtr& ipconfig) {
  return ipconfig && !ipconfig->properties().address.empty() &&
         !ipconfig->properties().dns_servers.empty();
}

void Device::OnIPv6ConfigUpdated() {
  if (ip6config_ && connection_) {
    connection_->UpdateGatewayMetric(ip6config_);
  }

  // Setup connection using IPv6 configuration only if the IPv6 configuration
  // is ready for connection (contained both IP address and DNS servers), and
  // there is no existing IPv4 connection. We always prefer IPv4
  // configuration over IPv6.
  if (IPConfigCompleted(ip6config_) &&
      (!connection_ || connection_->IsIPv6())) {
    SetupConnection(ip6config_);
  }
}

void Device::SetupConnection(const IPConfigRefPtr& ipconfig) {
  CreateConnection();
  if (manager_->ShouldBlackholeUserTraffic(UniqueName())) {
    ipconfig->SetBlackholedUids(manager_->user_traffic_uids());
  } else {
    ipconfig->ClearBlackholedUids();
  }
  connection_->UpdateFromIPConfig(ipconfig);

  // Report connection type.
  Metrics::NetworkConnectionIPType ip_type =
      connection_->IsIPv6() ? Metrics::kNetworkConnectionIPTypeIPv6
                            : Metrics::kNetworkConnectionIPTypeIPv4;
  metrics()->NotifyNetworkConnectionIPType(technology_, ip_type);

  // Report if device have IPv6 connectivity
  bool ipv6_connectivity = IPConfigCompleted(ip6config_);
  metrics()->NotifyIPv6ConnectivityStatus(technology_, ipv6_connectivity);

  // SetConnection must occur after the UpdateFromIPConfig so the
  // service can use the values derived from the connection.
  if (selected_service_) {
    // The service state change needs to happen after this call, so that
    // at the time we report the state change to the manager, the service
    // has its connection.
    selected_service_->SetConnection(connection_);

    // If this function was called due to a DHCP renewal, avoid transitioning
    // from Connected->Online->Connected because that can affect the service
    // sort order.  In this case, perform portal detection "optimistically"
    // in the Online state, and transition from Online->Portal if it fails.
    if (!selected_service_->IsOnline())
      SetServiceState(Service::kStateConnected);
    OnConnected();

    // Subtle: Start portal detection after transitioning the service
    // to the Connected state because this call may immediately transition
    // to the Online state.
    StartPortalDetection();
  }

  SetHostname(ipconfig->properties().accepted_hostname);
  StartLinkMonitor();
  StartTrafficMonitor();
}

bool Device::SetHostname(const std::string& hostname) {
  if (hostname.empty() || !manager()->ShouldAcceptHostnameFrom(link_name_)) {
    return false;
  }

  string fixed_hostname = hostname;
  if (fixed_hostname.length() > MAXHOSTNAMELEN) {
    auto truncate_length = fixed_hostname.find('.');
    if (truncate_length == string::npos || truncate_length > MAXHOSTNAMELEN) {
      truncate_length = MAXHOSTNAMELEN;
    }
    fixed_hostname.resize(truncate_length);
  }

  return manager_->device_info()->SetHostname(fixed_hostname);
}

void Device::PrependDNSServersIntoIPConfig(const IPConfigRefPtr& ipconfig) {
  const auto& properties = ipconfig->properties();

  vector<string> servers(properties.dns_servers.begin(),
                         properties.dns_servers.end());
  PrependDNSServers(properties.address_family, &servers);
  if (servers == properties.dns_servers) {
    // If the server list is the same after being augmented then there's no need
    // to update the config's list of servers.
    return;
  }

  ipconfig->UpdateDNSServers(std::move(servers));
}

void Device::PrependDNSServers(const IPAddress::Family family,
                               vector<string>* servers) {
  vector<string> output_servers =
      manager_->FilterPrependDNSServersByFamily(family);

  set<string> unique(output_servers.begin(), output_servers.end());
  for (const auto& server : *servers) {
    if (unique.find(server) == unique.end()) {
      output_servers.push_back(server);
      unique.insert(server);
    }
  }
  servers->swap(output_servers);
}

void Device::ConnectionDiagnosticsCallback(
    const std::string& connection_issue,
    const std::vector<ConnectionDiagnostics::Event>& diagnostic_events) {
  SLOG(this, 2) << "Device " << link_name()
                << ": Completed Connection diagnostics";
  // TODO(samueltan): add connection diagnostics metrics.
}

void Device::OnIPConfigUpdated(const IPConfigRefPtr& ipconfig,
                               bool /*new_lease_acquired*/) {
  SLOG(this, 2) << __func__;
  if (selected_service_) {
    ipconfig->ApplyStaticIPParameters(
        selected_service_->mutable_static_ip_parameters());
    if (IsUsingStaticIP()) {
      // If we are using a statically configured IP address instead
      // of a leased IP address, release any acquired lease so it may
      // be used by others.  This allows us to merge other non-leased
      // parameters (like DNS) when they're available from a DHCP server
      // and not overridden by static parameters, but at the same time
      // we avoid taking up a dynamic IP address the DHCP server could
      // assign to someone else who might actually use it.
      ipconfig->ReleaseIP(IPConfig::kReleaseReasonStaticIP);
    }
  }
  if (!IsUsingStaticNameServers()) {
    PrependDNSServersIntoIPConfig(ipconfig);
  }

  SetupConnection(ipconfig);
  UpdateIPConfigsProperty();
}

void Device::OnIPConfigFailed(const IPConfigRefPtr& ipconfig) {
  SLOG(this, 2) << __func__;
  // TODO(pstew): This logic gets yet more complex when multiple
  // IPConfig types are run in parallel (e.g. DHCP and DHCP6)
  if (selected_service_) {
    if (IsUsingStaticIP()) {
      // Consider three cases:
      //
      // 1. We're here because DHCP failed while starting up. There
      //    are two subcases:
      //    a. DHCP has failed, and Static IP config has _not yet_
      //       completed. It's fine to do nothing, because we'll
      //       apply the static config shortly.
      //    b. DHCP has failed, and Static IP config has _already_
      //       completed. It's fine to do nothing, because we can
      //       continue to use the static config that's already
      //       been applied.
      //
      // 2. We're here because a previously valid DHCP configuration
      //    is no longer valid. There's still a static IP config,
      //    because the condition in the if clause evaluated to true.
      //    Furthermore, the static config includes an IP address for
      //    us to use.
      //
      //    The current configuration may include some DHCP
      //    parameters, overriden by any static parameters
      //    provided. We continue to use this configuration, because
      //    the only configuration element that is leased to us (IP
      //    address) will be overriden by a static parameter.
      return;
    }
  }

  ipconfig->ResetProperties();
  UpdateIPConfigsProperty();

  // Fallback to IPv6 if possible.
  if (IPConfigCompleted(ip6config_)) {
    if (!connection_ || !connection_->IsIPv6()) {
      // Setup IPv6 connection.
      SetupConnection(ip6config_);
    } else {
      // Ignore IPv4 config failure, since IPv6 is up.
    }
    return;
  }

  OnIPConfigFailure();
  DestroyConnection();
}

void Device::OnIPConfigRefreshed(const IPConfigRefPtr& ipconfig) {
  // Clear the previously applied static IP parameters.
  ipconfig->RestoreSavedIPParameters(
      selected_service_->mutable_static_ip_parameters());

  dispatcher()->PostTask(FROM_HERE,
                         Bind(&Device::ConfigureStaticIPTask, AsWeakPtr()));
}

void Device::OnIPConfigFailure() {
  if (selected_service_) {
    Error error;
    selected_service_->DisconnectWithFailure(Service::kFailureDHCP, &error,
                                             __func__);
  }
}

void Device::OnIPConfigExpired(const IPConfigRefPtr& ipconfig) {
  metrics()->SendToUMA(
      metrics()->GetFullMetricName(
          Metrics::kMetricExpiredLeaseLengthSecondsSuffix, technology()),
      ipconfig->properties().lease_duration_seconds,
      Metrics::kMetricExpiredLeaseLengthSecondsMin,
      Metrics::kMetricExpiredLeaseLengthSecondsMax,
      Metrics::kMetricExpiredLeaseLengthSecondsNumBuckets);
}

void Device::OnDHCPv6ConfigUpdated(const IPConfigRefPtr& ipconfig,
                                   bool /*new_lease_acquired*/) {
  // Emit configuration update.
  UpdateIPConfigsProperty();
}

void Device::OnDHCPv6ConfigFailed(const IPConfigRefPtr& ipconfig) {
  // Reset configuration data.
  ipconfig->ResetProperties();
  UpdateIPConfigsProperty();
}

void Device::OnDHCPv6ConfigExpired(const IPConfigRefPtr& ipconfig) {
  // Reset configuration data.
  ipconfig->ResetProperties();
  UpdateIPConfigsProperty();
}

void Device::OnUnreliableLink() {
  SLOG(this, 2) << "Device " << link_name() << ": Link is unreliable.";
  selected_service_->set_unreliable(true);
  reliable_link_callback_.Cancel();
  metrics()->NotifyUnreliableLinkSignalStrength(technology_,
                                                selected_service_->strength());
}

void Device::OnReliableLink() {
  SLOG(this, 2) << "Device " << link_name() << ": Link is reliable.";
  selected_service_->set_unreliable(false);
  // TODO(zqiu): report signal strength to UMA.
}

void Device::OnConnected() {
  if (selected_service_->unreliable()) {
    // Post a delayed task to reset link back to reliable if no link
    // failure is detected in the next 5 minutes.
    reliable_link_callback_.Reset(
        base::Bind(&Device::OnReliableLink, base::Unretained(this)));
    dispatcher()->PostDelayedTask(FROM_HERE, reliable_link_callback_.callback(),
                                  kLinkUnreliableThresholdSeconds * 1000);
  }
}

void Device::OnConnectionUpdated() {
  if (selected_service_) {
    manager_->UpdateService(selected_service_);
  }
}

void Device::CreateConnection() {
  SLOG(this, 2) << __func__;
  if (!connection_) {
    connection_ = new Connection(interface_index_, link_name_, fixed_ip_params_,
                                 technology_, manager_->device_info(),
                                 control_interface());
  }
}

void Device::DestroyConnection() {
  SLOG(this, 2) << __func__ << " on " << link_name_;
  StopAllActivities();
  if (selected_service_) {
    SLOG(this, 3) << "Clearing connection of service "
                  << selected_service_->log_name();
    selected_service_->SetConnection(nullptr);
  }
  connection_ = nullptr;
}

void Device::GetTrafficCountersCallback(
    const ServiceRefPtr& old_service,
    const ServiceRefPtr& new_service,
    const std::vector<patchpanel::TrafficCounter>& counters) {
  if (old_service) {
    old_service->RefreshTrafficCounters(counters);
  }
  if (new_service) {
    // Update the snapshot values, which will be used in future refreshes to
    // diff against the counter values. Snapshot must be initialized before
    // layer 3 configuration to ensure that we capture all traffic for the
    // service.
    new_service->InitializeTrafficCounterSnapshot(counters);
  }
}

void Device::GetTrafficCountersPatchpanelCallback(
    unsigned int id, const std::vector<patchpanel::TrafficCounter>& counters) {
  auto iter = traffic_counters_callback_map_.find(id);
  if (iter == traffic_counters_callback_map_.end() || iter->second.is_null()) {
    LOG(ERROR) << "No callback found for ID " << id;
    return;
  }
  if (counters.empty()) {
    LOG(WARNING) << "No counters found for " << link_name_;
  }
  auto callback = std::move(iter->second);
  traffic_counters_callback_map_.erase(iter);
  std::move(callback).Run(counters);
}

void Device::SelectService(const ServiceRefPtr& service) {
  SLOG(this, 2) << __func__ << ": service "
                << (service ? service->log_name() : "*reset*") << " on "
                << link_name_;

  if (selected_service_.get() == service.get()) {
    // No change to |selected_service_|. Return early to avoid
    // changing its state.
    return;
  }

  ServiceRefPtr old_service;
  if (selected_service_) {
    old_service = selected_service_;
    if (selected_service_->state() != Service::kStateFailure) {
      selected_service_->SetState(Service::kStateIdle);
    }
    // Just in case the Device subclass has not already done so, make
    // sure the previously selected service has its connection removed.
    selected_service_->SetConnection(nullptr);
    // Reset link status for the previously selected service.
    selected_service_->set_unreliable(false);
    reliable_link_callback_.Cancel();
    StopAllActivities();
  }

  // Newly selected service (network), previous failures doesn't apply
  // anymore.
  last_link_monitor_failed_time_ = 0;

  selected_service_ = service;
  FetchTrafficCounters(old_service, selected_service_);
  adaptor_->EmitRpcIdentifierChanged(kSelectedServiceProperty,
                                     GetSelectedServiceRpcIdentifier(nullptr));
}

void Device::SetServiceState(Service::ConnectState state) {
  if (selected_service_) {
    selected_service_->SetState(state);
  }
}

void Device::SetServiceFailure(Service::ConnectFailure failure_state) {
  if (selected_service_) {
    selected_service_->SetFailure(failure_state);
  }
}

void Device::SetServiceFailureSilent(Service::ConnectFailure failure_state) {
  if (selected_service_) {
    selected_service_->SetFailureSilent(failure_state);
  }
}

bool Device::SetIPFlag(IPAddress::Family family,
                       const string& flag,
                       const string& value) {
  string ip_version;
  if (family == IPAddress::kFamilyIPv4) {
    ip_version = kIPFlagVersion4;
  } else if (family == IPAddress::kFamilyIPv6) {
    ip_version = kIPFlagVersion6;
  } else {
    NOTIMPLEMENTED();
  }
  FilePath flag_file(StringPrintf(kIPFlagTemplate, ip_version.c_str(),
                                  link_name_.c_str(), flag.c_str()));
  SLOG(this, 2) << "Writing " << value << " to flag file " << flag_file.value();
  if (base::WriteFile(flag_file, value.c_str(), value.length()) != 1) {
    string message = StringPrintf("IP flag write failed: %s to %s",
                                  value.c_str(), flag_file.value().c_str());
    if (!base::PathExists(flag_file) &&
        base::Contains(written_flags_, flag_file.value())) {
      SLOG(this, 2) << message << " (device is no longer present?)";
    } else {
      LOG(ERROR) << message;
    }
    return false;
  } else {
    written_flags_.insert(flag_file.value());
  }
  return true;
}

void Device::ResetByteCounters() {
  manager_->device_info()->GetByteCounts(
      interface_index_, &receive_byte_offset_, &transmit_byte_offset_);
  manager_->UpdateDevice(this);
}

bool Device::RestartPortalDetection() {
  StopPortalDetection();
  return StartPortalDetection();
}

bool Device::RequestPortalDetection() {
  if (!selected_service_) {
    SLOG(this, 2) << link_name()
                  << ": No selected service, so no need for portal check.";
    return false;
  }

  if (!connection_) {
    SLOG(this, 2) << link_name()
                  << ": No connection, so no need for portal check.";
    return false;
  }

  SLOG(this, 1) << __func__ << " for: " << selected_service_->log_name();

  // Do not run portal detection unless in a connected state (i.e. connected,
  // online, or portalled).
  if (!selected_service_->IsConnected()) {
    SLOG(this, 3)
        << link_name()
        << ": Service is not in a connected state. No need to start check.";
    return false;
  }

  if (!connection_->IsDefault()) {
    SLOG(this, 2) << link_name() << ": Service is not the default connection.  "
                  << "Don't start check.";
    return false;
  }

  if (portal_detector_.get() && portal_detector_->IsInProgress()) {
    SLOG(this, 2) << link_name() << ": Portal detection is already running.";
    return true;
  }

  return StartPortalDetection();
}

// Start portal detection for |selected_service_| if enabled.
// Note: This method used to also check for a proxy configuration, however a
// proxy may or may not return a portal response depending on how it is
// configured. We run additional portal detection in Chrome if a proxy is
// configured, but still run Shill portal detection first.
bool Device::StartPortalDetection() {
  DCHECK(selected_service_);
  SLOG(this, 1) << __func__ << " for: " << selected_service_->log_name();
  if (selected_service_->IsPortalDetectionDisabled()) {
    SLOG(this, 2) << "Service " << selected_service_->log_name()
                  << ": Portal detection is disabled; "
                  << "marking service online.";
    SetServiceConnectedState(Service::kStateOnline);
    return false;
  }

  if (selected_service_->IsPortalDetectionAuto() &&
      !manager_->IsPortalDetectionEnabled(technology())) {
    // If portal detection is disabled for this technology, immediately set
    // the service state to "Online".
    SLOG(this, 2) << "Device " << link_name()
                  << ": Portal detection is disabled; "
                  << "marking service online.";
    SetServiceConnectedState(Service::kStateOnline);
    return false;
  }

  portal_detector_.reset(
      new PortalDetector(connection_, dispatcher(), metrics(),
                         Bind(&Device::PortalDetectorCallback, AsWeakPtr())));
  PortalDetector::Properties props = manager_->GetPortalCheckProperties();
  if (!StartPortalDetectionTrial(portal_detector_.get(), props, 0)) {
    LOG(ERROR) << "Device " << link_name()
               << ": Portal detection failed to start: likely bad URL: "
               << props.http_url_string << " or " << props.https_url_string;
    SetServiceConnectedState(Service::kStateOnline);
    return false;
  }

  portal_check_interval_seconds_ = PortalDetector::kInitialCheckIntervalSeconds;

  SLOG(this, 2) << "Device " << link_name()
                << ": Portal detection has started.";
  return true;
}

void Device::StopPortalDetection() {
  SLOG(this, 2) << "Device " << link_name() << ": Portal detection stopping.";
  portal_check_interval_seconds_ = 0;
  portal_detector_.reset();
}

bool Device::StartConnectionDiagnosticsAfterPortalDetection(
    const PortalDetector::Result& http_result,
    const PortalDetector::Result& https_result) {
  connection_diagnostics_.reset(new ConnectionDiagnostics(
      connection_, dispatcher(), metrics(), manager_->device_info(),
      Bind(&Device::ConnectionDiagnosticsCallback, AsWeakPtr())));
  if (!connection_diagnostics_->StartAfterPortalDetection(
          manager_->GetPortalCheckHttpUrl(), http_result, https_result)) {
    LOG(ERROR) << "Device " << link_name()
               << ": Connection diagnostics failed to start: likely bad URL: "
               << manager_->GetPortalCheckHttpUrl();
    connection_diagnostics_.reset();
    return false;
  }

  SLOG(this, 2) << "Device " << link_name()
                << ": Connection diagnostics has started.";
  return true;
}

void Device::StopConnectionDiagnostics() {
  SLOG(this, 2) << "Device " << link_name()
                << ": Connection diagnostics stopping.";
  connection_diagnostics_.reset();
}

bool Device::StartConnectivityTest() {
  LOG(INFO) << "Device " << link_name() << " starting connectivity test.";

  connection_tester_.reset(
      new PortalDetector(connection_, dispatcher(), metrics(),
                         Bind(&Device::ConnectionTesterCallback, AsWeakPtr())));
  StartPortalDetectionTrial(connection_tester_.get(),
                            PortalDetector::Properties(), 0);
  return true;
}

void Device::StopConnectivityTest() {
  SLOG(this, 2) << "Device " << link_name() << ": Connectivity test stopping.";
  connection_tester_.reset();
}

void Device::set_link_monitor(LinkMonitor* link_monitor) {
  link_monitor_.reset(link_monitor);
}

void Device::set_mac_address(const std::string& mac_address) {
  mac_address_ = mac_address;
  adaptor_->EmitStringChanged(kAddressProperty, mac_address_);
}

bool Device::StartLinkMonitor() {
  if (!manager_->IsTechnologyLinkMonitorEnabled(technology())) {
    SLOG(this, 2) << "Device " << link_name()
                  << ": Link Monitoring is disabled.";
    return false;
  }

  if (connection_->IsIPv6()) {
    SLOG(this, 2) << "Device " << link_name()
                  << ": Link Monitoring is disabled for IPv6-only network.";
    return false;
  }

  if (selected_service_ && selected_service_->link_monitor_disabled()) {
    SLOG(this, 2) << "Device " << link_name()
                  << ": Link Monitoring is disabled for the selected service";
    return false;
  }

  if (!link_monitor()) {
    set_link_monitor(new LinkMonitor(
        connection_, dispatcher(), metrics(), manager_->device_info(),
        Bind(&Device::OnLinkMonitorFailure, AsWeakPtr()),
        Bind(&Device::OnLinkMonitorGatewayChange, AsWeakPtr())));
  }

  SLOG(this, 2) << "Device " << link_name() << ": Link Monitor starting.";
  return link_monitor_->Start();
}

void Device::StopLinkMonitor() {
  SLOG(this, 2) << "Device " << link_name() << ": Link Monitor stopping.";
  link_monitor_.reset();
}

void Device::OnLinkMonitorFailure() {
  SLOG(this, 2) << "Device " << link_name()
                << ": Link Monitor indicates failure.";
  if (!selected_service_) {
    return;
  }

  time_t now;
  time_->GetSecondsBoottime(&now);

  if (last_link_monitor_failed_time_ != 0 &&
      now - last_link_monitor_failed_time_ < kLinkUnreliableThresholdSeconds) {
    OnUnreliableLink();
  }
  last_link_monitor_failed_time_ = now;
}

void Device::OnLinkMonitorGatewayChange() {
  string gateway_mac = link_monitor()->gateway_mac_address().HexEncode();
  int connection_id =
      manager_->CalcConnectionId(ipconfig_->properties().gateway, gateway_mac);

  CHECK(selected_service_);
  selected_service_->set_connection_id(connection_id);

  manager_->ReportServicesOnSameNetwork(connection_id);
}

bool Device::StartDNSTest(
    const vector<string>& dns_servers,
    bool retry_until_success,
    const Callback<void(const DnsServerTester::Status)>& callback) {
  if (dns_server_tester_) {
    LOG(ERROR) << link_name() << ": "
               << "Failed to start DNS Test: current test still running";
    return false;
  }

  dns_server_tester_.reset(new DnsServerTester(
      connection_, dispatcher(), dns_servers, retry_until_success, callback));
  dns_server_tester_->Start();
  return true;
}

void Device::StopDNSTest() {
  dns_server_tester_.reset();
}

void Device::FallbackDNSResultCallback(const DnsServerTester::Status status) {
  StopDNSTest();
  int result = Metrics::kFallbackDNSTestResultFailure;
  if (status == DnsServerTester::kStatusSuccess) {
    result = Metrics::kFallbackDNSTestResultSuccess;

    // Switch to fallback DNS server if service is configured to allow DNS
    // fallback.
    CHECK(selected_service_);
    if (selected_service_->is_dns_auto_fallback_allowed()) {
      LOG(INFO) << "Device " << link_name()
                << ": Switching to fallback DNS servers.";
      // Save the DNS servers from ipconfig.
      config_dns_servers_ = ipconfig_->properties().dns_servers;
      SwitchDNSServers(vector<string>(std::begin(kFallbackDnsServers),
                                      std::end(kFallbackDnsServers)));
      // Start DNS test for configured DNS servers.
      StartDNSTest(config_dns_servers_, true,
                   Bind(&Device::ConfigDNSResultCallback, AsWeakPtr()));
    }
  }
  metrics()->NotifyFallbackDNSTestResult(technology_, result);
}

void Device::ConfigDNSResultCallback(const DnsServerTester::Status status) {
  StopDNSTest();
  // DNS test failed to start due to internal error.
  if (status == DnsServerTester::kStatusFailure) {
    return;
  }

  // Switch back to the configured DNS servers.
  LOG(INFO) << "Device " << link_name()
            << ": Switching back to configured DNS servers.";
  SwitchDNSServers(config_dns_servers_);
}

void Device::SwitchDNSServers(const vector<string>& dns_servers) {
  CHECK(ipconfig_);
  CHECK(connection_);
  // Push new DNS servers setting to the IP config object.
  ipconfig_->UpdateDNSServers(dns_servers);
  // Push new DNS servers setting to the current connection, so the resolver
  // will be updated to use the new DNS servers.
  connection_->UpdateDNSServers(dns_servers);
  // Allow the service to notify Chrome of ipconfig changes.
  selected_service_->NotifyIPConfigChanges();
  // Restart the portal detection with the new DNS setting.
  RestartPortalDetection();
}

void Device::set_traffic_monitor_for_test(
    std::unique_ptr<TrafficMonitor> traffic_monitor) {
  traffic_monitor_ = std::move(traffic_monitor);
}

bool Device::TimeToNextDHCPLeaseRenewal(uint32_t* result) {
  if (!ipconfig() && !ip6config()) {
    return false;
  }
  uint32_t time_to_ipv4_lease_expiry = UINT32_MAX;
  uint32_t time_to_ipv6_lease_expiry = UINT32_MAX;
  if (ipconfig()) {
    ipconfig()->TimeToLeaseExpiry(&time_to_ipv4_lease_expiry);
  }
  if (ip6config()) {
    ip6config()->TimeToLeaseExpiry(&time_to_ipv6_lease_expiry);
  }
  *result = std::min(time_to_ipv4_lease_expiry, time_to_ipv6_lease_expiry);
  return true;
}

bool Device::IsTrafficMonitorEnabled() const {
  return false;
}

void Device::StartTrafficMonitor() {
  // Return if traffic monitor is not enabled for this device.
  if (!IsTrafficMonitorEnabled()) {
    return;
  }

  SLOG(this, 2) << "Device " << link_name() << ": Traffic Monitor starting.";
  if (!traffic_monitor_) {
    traffic_monitor_ = std::make_unique<TrafficMonitor>(
        this, dispatcher(),
        Bind(&Device::OnEncounterNetworkProblem, AsWeakPtr()));
  }
  traffic_monitor_->Start();
}

void Device::StopTrafficMonitor() {
  // Return if traffic monitor is not enabled for this device.
  if (!IsTrafficMonitorEnabled()) {
    return;
  }

  if (traffic_monitor_) {
    SLOG(this, 2) << "Device " << link_name() << ": Traffic Monitor stopping.";
    traffic_monitor_->Stop();
  }
  traffic_monitor_.reset();
}

void Device::OnEncounterNetworkProblem(int reason) {
  int metric_code;
  switch (reason) {
    case TrafficMonitor::kNetworkProblemCongestedTxQueue:
      metric_code = Metrics::kNetworkProblemCongestedTCPTxQueue;
      break;
    case TrafficMonitor::kNetworkProblemDNSFailure:
      metric_code = Metrics::kNetworkProblemDNSFailure;
      break;
    default:
      LOG(ERROR) << "Invalid network problem code: " << reason;
      return;
  }

  metrics()->NotifyNetworkProblemDetected(technology_, metric_code);
  // Stop the traffic monitor, only report the first network problem detected
  // on the connection for now.
  StopTrafficMonitor();
}

void Device::SetServiceConnectedState(Service::ConnectState state) {
  DCHECK(selected_service_.get());

  if (!selected_service_) {
    LOG(ERROR) << link_name() << ": "
               << "Portal detection completed but no selected service exists!";
    return;
  }

  if (!selected_service_->IsConnected()) {
    LOG(ERROR) << link_name() << ": "
               << "Portal detection completed but selected service "
               << selected_service_->log_name()
               << " is in non-connected state.";
    return;
  }

  SLOG(this, 2) << __func__ << " Service: "
                << GetSelectedServiceRpcIdentifier(nullptr).value()
                << " State: " << static_cast<int>(state);

  if (Service::IsPortalledState(state) && connection_->IsDefault() &&
      portal_check_interval_seconds_ != 0) {
    CHECK(portal_detector_.get());
    PortalDetector::Properties props = manager_->GetPortalCheckProperties();
    int start_delay =
        portal_detector_->AdjustStartDelay(portal_check_interval_seconds_);
    if (!StartPortalDetectionTrial(portal_detector_.get(), props,
                                   start_delay)) {
      LOG(ERROR) << "Device " << link_name()
                 << ": Portal detection failed to restart: likely bad URL: "
                 << props.http_url_string << " or " << props.https_url_string;
      SetServiceState(Service::kStateOnline);
      StopPortalDetection();
      return;
    }

    portal_check_interval_seconds_ =
        std::min(portal_check_interval_seconds_ * 2,
                 PortalDetector::kMaxPortalCheckIntervalSeconds);
    SLOG(this, 2) << "Device " << link_name() << ": Portal detection retrying.";
  } else {
    SLOG(this, 2) << "Device " << link_name() << ": Portal will not retry.";
    StopPortalDetection();
  }

  SetServiceState(state);
}

bool Device::StartPortalDetectionTrial(PortalDetector* portal_detector,
                                       const PortalDetector::Properties& props,
                                       int delay_seconds) {
  if (portal_detector->StartAfterDelay(props, delay_seconds)) {
    // Accept traffic routed to this connection even if it is not the default.
    SetLooseRouting(true);
    return true;
  }
  return false;
}

void Device::PortalDetectorCallback(
    const PortalDetector::Result& http_result,
    const PortalDetector::Result& https_result) {
  SLOG(this, 2) << __func__ << " Device: " << link_name() << " Service: "
                << GetSelectedServiceRpcIdentifier(nullptr).value()
                << " Received status: "
                << PortalDetector::StatusToString(http_result.status);

  SetLooseRouting(false);

  int portal_status = Metrics::PortalDetectionResultToEnum(http_result);
  metrics()->SendEnumToUMA(
      metrics()->GetFullMetricName(Metrics::kMetricPortalResultSuffix,
                                   technology()),
      portal_status, Metrics::kPortalResultMax);

  Service::ConnectState state =
      CalculatePortalStateFromProbeResults(http_result, https_result);
  if (selected_service_) {
    // Set the probe URL. It should be empty if there is no redirect.
    selected_service_->SetProbeUrl(http_result.probe_url_string);
  }
  if (state == Service::kStateOnline) {
    SetServiceConnectedState(state);

    metrics()->SendToUMA(
        metrics()->GetFullMetricName(
            Metrics::kMetricPortalAttemptsToOnlineSuffix, technology()),
        http_result.num_attempts, Metrics::kMetricPortalAttemptsToOnlineMin,
        Metrics::kMetricPortalAttemptsToOnlineMax,
        Metrics::kMetricPortalAttemptsToOnlineNumBuckets);
  } else {
    // Set failure phase and status.
    if (selected_service_) {
      selected_service_->SetPortalDetectionFailure(
          PortalDetector::PhaseToString(http_result.phase),
          PortalDetector::StatusToString(http_result.status),
          http_result.status_code);
    }
    SetServiceConnectedState(state);

    StartConnectionDiagnosticsAfterPortalDetection(http_result, https_result);

    // TODO(zqiu): Only support fallback DNS server for IPv4 for now.
    if (connection_->IsIPv6()) {
      return;
    }

    // Perform fallback DNS test if the portal failure is DNS related.
    // The test will send a  DNS request to Google's DNS server to determine
    // if the DNS failure is due to bad DNS server settings.
    if ((portal_status == Metrics::kPortalResultDNSFailure) ||
        (portal_status == Metrics::kPortalResultDNSTimeout)) {
      StartDNSTest(vector<string>(std::begin(kFallbackDnsServers),
                                  std::end(kFallbackDnsServers)),
                   false,
                   Bind(&Device::FallbackDNSResultCallback, AsWeakPtr()));
    }
  }
}

RpcIdentifier Device::GetSelectedServiceRpcIdentifier(Error* /*error*/) {
  if (!selected_service_) {
    return RpcIdentifier("/");
  }
  return selected_service_->GetRpcIdentifier();
}

RpcIdentifiers Device::AvailableIPConfigs(Error* /*error*/) {
  RpcIdentifiers identifiers;
  if (ipconfig_) {
    identifiers.push_back(ipconfig_->GetRpcIdentifier());
  }
  if (ip6config_) {
    identifiers.push_back(ip6config_->GetRpcIdentifier());
  }
  if (dhcpv6_config_) {
    identifiers.push_back(dhcpv6_config_->GetRpcIdentifier());
  }
  return identifiers;
}

uint64_t Device::GetLinkMonitorResponseTime(Error* error) {
  if (!link_monitor_) {
    // It is not strictly an error that the link monitor does not
    // exist, but returning an error here allows the GetProperties
    // call in our Adaptor to omit this parameter.
    error->Populate(Error::kNotFound, "Device is not running LinkMonitor");
    return 0;
  }
  return link_monitor_->GetResponseTimeMilliseconds();
}

uint64_t Device::GetReceiveByteCount() {
  uint64_t rx_byte_count = 0, tx_byte_count = 0;
  manager_->device_info()->GetByteCounts(interface_index_, &rx_byte_count,
                                         &tx_byte_count);
  return rx_byte_count - receive_byte_offset_;
}

uint64_t Device::GetTransmitByteCount() {
  uint64_t rx_byte_count = 0, tx_byte_count = 0;
  manager_->device_info()->GetByteCounts(interface_index_, &rx_byte_count,
                                         &tx_byte_count);
  return tx_byte_count - transmit_byte_offset_;
}

uint64_t Device::GetReceiveByteCountProperty(Error* /*error*/) {
  return GetReceiveByteCount();
}

uint64_t Device::GetTransmitByteCountProperty(Error* /*error*/) {
  return GetTransmitByteCount();
}

bool Device::IsUnderlyingDeviceEnabled() const {
  return false;
}

// callback
void Device::OnEnabledStateChanged(const ResultCallback& callback,
                                   const Error& error) {
  SLOG(this, 1) << __func__ << " (target: " << enabled_pending_ << ","
                << " success: " << error.IsSuccess() << ")"
                << " on " << link_name_;
  if (error.IsSuccess()) {
    enabled_ = enabled_pending_;
    if (!enabled_ && ShouldBringNetworkInterfaceDownAfterDisabled()) {
      BringNetworkInterfaceDown();
    }
    manager_->UpdateEnabledTechnologies();
    adaptor_->EmitBoolChanged(kPoweredProperty, enabled_);
  }
  enabled_pending_ = enabled_;
  if (!callback.is_null())
    callback.Run(error);
}

void Device::SetEnabled(bool enable) {
  SLOG(this, 1) << __func__ << "(" << enable << ")";
  Error error;
  SetEnabledChecked(enable, false, &error, ResultCallback());

  // SetEnabledInternal might fail here if there is an unfinished enable or
  // disable operation. Don't log error in this case, as this method is only
  // called when the underlying device is already in the target state and the
  // pending operation should eventually bring the device to the expected
  // state.
  LOG_IF(ERROR, error.IsFailure() && !error.IsOngoing() &&
                    error.type() != Error::kInProgress)
      << "Enabled failed, but no way to report the failure.";
}

void Device::SetEnabledNonPersistent(bool enable,
                                     Error* error,
                                     const ResultCallback& callback) {
  SLOG(this, 1) << __func__ << "(" << enable << ")";
  SetEnabledChecked(enable, false, error, callback);
}

void Device::SetEnabledPersistent(bool enable,
                                  Error* error,
                                  const ResultCallback& callback) {
  SLOG(this, 1) << __func__ << "(" << enable << ")";
  SetEnabledChecked(enable, true, error, callback);
}

void Device::SetEnabledChecked(bool enable,
                               bool persist,
                               Error* error,
                               const ResultCallback& callback) {
  DCHECK(error);
  SLOG(this, 1) << __func__ << ": Device " << link_name_ << " "
                << (enable ? "starting" : "stopping");
  if (enable && manager_->IsTechnologyProhibited(technology())) {
    error->Populate(Error::kPermissionDenied, "The " + technology().GetName() +
                                                  " technology is prohibited");
    return;
  }

  if (enable == enabled_) {
    if (enable != enabled_pending_ && persist) {
      // Return an error, as there is an ongoing operation to achieve the
      // opposite.
      Error::PopulateAndLog(
          FROM_HERE, error, Error::kOperationFailed,
          enable ? "Cannot enable while the device is disabling."
                 : "Cannot disable while the device is enabling.");
      return;
    }
    SLOG(this, 1) << "Already in desired enable state.";
    error->Reset();
    return;
  }

  if (enabled_pending_ == enable) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInProgress,
                          "Enable operation already in progress");
    return;
  }

  if (persist) {
    enabled_persistent_ = enable;
    manager_->UpdateDevice(this);
  }

  SetEnabledUnchecked(enable, error, callback);
}

void Device::SetEnabledUnchecked(bool enable,
                                 Error* error,
                                 const ResultCallback& on_enable_complete) {
  SLOG(this, 1) << __func__ << ": link: " << link_name()
                << " enable: " << enable;
  enabled_pending_ = enable;
  EnabledStateChangedCallback chained_callback =
      Bind(&Device::OnEnabledStateChanged, AsWeakPtr(), on_enable_complete);
  if (enable) {
    running_ = true;
    Start(error, chained_callback);
  } else {
    running_ = false;
    DestroyIPConfig();       // breaks a reference cycle
    SelectService(nullptr);  // breaks a reference cycle
    if (!ShouldBringNetworkInterfaceDownAfterDisabled()) {
      BringNetworkInterfaceDown();
    }
    SLOG(this, 3) << "Device " << link_name_ << " ipconfig_ "
                  << (ipconfig_ ? "is set." : "is not set.");
    SLOG(this, 3) << "Device " << link_name_ << " ip6config_ "
                  << (ip6config_ ? "is set." : "is not set.");
    SLOG(this, 3) << "Device " << link_name_ << " connection_ "
                  << (connection_ ? "is set." : "is not set.");
    SLOG(this, 3) << "Device " << link_name_ << " selected_service_ "
                  << (selected_service_ ? "is set." : "is not set.");
    Stop(error, chained_callback);
  }
}

void Device::UpdateIPConfigsProperty() {
  adaptor_->EmitRpcIdentifierArrayChanged(kIPConfigsProperty,
                                          AvailableIPConfigs(nullptr));
}

bool Device::ResolvePeerMacAddress(const string& input,
                                   string* output,
                                   Error* error) {
  if (!MakeHardwareAddressFromString(input).empty()) {
    // Input is already a MAC address.
    *output = input;
    return true;
  }

  IPAddress ip_address(IPAddress::kFamilyIPv4);
  if (!ip_address.SetAddressFromString(input)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Peer is neither an IP Address nor a MAC address");
    return false;
  }

  // Peer address was specified as an IP address which we need to resolve.
  const DeviceInfo* device_info = manager()->device_info();
  if (!HasDirectConnectivityTo(ip_address)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "IP address is not local to this interface");
    return false;
  }

  ByteString mac_address;
  if (device_info->GetMacAddressOfPeer(interface_index_, ip_address,
                                       &mac_address)) {
    *output = MakeStringFromHardwareAddress(
        vector<uint8_t>(mac_address.GetConstData(),
                        mac_address.GetConstData() + mac_address.GetLength()));
    SLOG(this, 2) << "ARP cache lookup returned peer: " << *output;
    return true;
  }

  Icmp pinger;
  if (!pinger.Start(ip_address, interface_index_) ||
      !pinger.TransmitEchoRequest(1, 1)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          "Failed to send ICMP request to peer to setup ARP");
  } else {
    // ARP request was transmitted successfully, address resolution is still
    // pending.
    error->Populate(Error::kInProgress,
                    "Peer MAC address was not found in the ARP cache, "
                    "but an ARP request was sent to find it.  "
                    "Please try again.");
  }
  return false;
}

// static
vector<uint8_t> Device::MakeHardwareAddressFromString(
    const string& address_string) {
  string address_nosep;
  base::RemoveChars(address_string, ":", &address_nosep);
  vector<uint8_t> address_bytes;
  base::HexStringToBytes(address_nosep, &address_bytes);
  if (address_bytes.size() != kHardwareAddressLength) {
    return vector<uint8_t>();
  }
  return address_bytes;
}

// static
string Device::MakeStringFromHardwareAddress(
    const vector<uint8_t>& address_bytes) {
  CHECK_EQ(kHardwareAddressLength, address_bytes.size());
  return StringPrintf("%02x:%02x:%02x:%02x:%02x:%02x", address_bytes[0],
                      address_bytes[1], address_bytes[2], address_bytes[3],
                      address_bytes[4], address_bytes[5]);
}

bool Device::RequestRoam(const std::string& addr, Error* error) {
  return false;
}

bool Device::ShouldBringNetworkInterfaceDownAfterDisabled() const {
  return false;
}

void Device::BringNetworkInterfaceDown() {
  // If |fixed_ip_params_| is true, we don't manipulate the interface state.
  if (!fixed_ip_params_)
    rtnl_handler_->SetInterfaceFlags(interface_index(), 0, IFF_UP);
}

ControlInterface* Device::control_interface() const {
  return manager_->control_interface();
}

EventDispatcher* Device::dispatcher() const {
  return manager_->dispatcher();
}

Metrics* Device::metrics() const {
  return manager_->metrics();
}

}  // namespace shill
