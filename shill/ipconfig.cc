// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/ipconfig.h"

#include <sys/time.h>

#include <limits>
#include <utility>

#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/net/shill_time.h"
#include "shill/static_ip_parameters.h"

using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kInet;
static string ObjectID(const IPConfig* i) {
  return i->GetRpcIdentifier().value();
}
}  // namespace Logging

namespace {

const time_t kDefaultLeaseExpirationTime = std::numeric_limits<time_t>::max();

}  // namespace

// static
const int IPConfig::kDefaultMTU = 1500;
const int IPConfig::kMinIPv4MTU = 576;
const int IPConfig::kMinIPv6MTU = 1280;
const int IPConfig::kUndefinedMTU = 0;
const char IPConfig::kType[] = "ip";

// static
uint32_t IPConfig::global_serial_ = 0;

IPConfig::IPConfig(ControlInterface* control_interface,
                   const std::string& device_name)
    : IPConfig(control_interface, device_name, kType) {}

IPConfig::IPConfig(ControlInterface* control_interface,
                   const std::string& device_name,
                   const std::string& type)
    : device_name_(device_name),
      type_(type),
      serial_(global_serial_++),
      adaptor_(control_interface->CreateIPConfigAdaptor(this)),
      current_lease_expiration_time_({kDefaultLeaseExpirationTime, 0}),
      time_(Time::GetInstance()),
      weak_ptr_factory_(this) {
  store_.RegisterConstString(kAddressProperty, &properties_.address);
  store_.RegisterConstString(kBroadcastProperty,
                             &properties_.broadcast_address);
  store_.RegisterConstString(kDomainNameProperty, &properties_.domain_name);
  store_.RegisterConstString(kAcceptedHostnameProperty,
                             &properties_.accepted_hostname);
  store_.RegisterConstString(kGatewayProperty, &properties_.gateway);
  store_.RegisterConstString(kMethodProperty, &properties_.method);
  store_.RegisterConstInt32(kMtuProperty, &properties_.mtu);
  store_.RegisterConstStrings(kNameServersProperty, &properties_.dns_servers);
  store_.RegisterConstString(kPeerAddressProperty, &properties_.peer_address);
  store_.RegisterConstInt32(kPrefixlenProperty, &properties_.subnet_prefix);
  store_.RegisterConstStrings(kSearchDomainsProperty,
                              &properties_.domain_search);
  store_.RegisterConstByteArray(kVendorEncapsulatedOptionsProperty,
                                &properties_.vendor_encapsulated_options);
  store_.RegisterConstString(kWebProxyAutoDiscoveryUrlProperty,
                             &properties_.web_proxy_auto_discovery);
  store_.RegisterStringmaps(kDhcpv6AddressesProperty,
                            &properties_.dhcpv6_addresses);
  store_.RegisterStringmaps(kDhcpv6DelegatedPrefixesProperty,
                            &properties_.dhcpv6_delegated_prefixes);
  store_.RegisterConstUint32(kLeaseDurationSecondsProperty,
                             &properties_.lease_duration_seconds);
  store_.RegisterConstByteArray(kiSNSOptionDataProperty,
                                &properties_.isns_option_data);
  SLOG(this, 2) << __func__ << " device: " << device_name_;
}

IPConfig::~IPConfig() {
  SLOG(this, 2) << __func__ << " device: " << device_name();
}

const RpcIdentifier& IPConfig::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

bool IPConfig::RequestIP() {
  return false;
}

bool IPConfig::RenewIP() {
  return false;
}

bool IPConfig::ReleaseIP(ReleaseReason reason) {
  return false;
}

void IPConfig::Refresh() {
  if (!refresh_callback_.is_null()) {
    refresh_callback_.Run(this);
  }
  RenewIP();
}

void IPConfig::ApplyStaticIPParameters(
    StaticIPParameters* static_ip_parameters) {
  static_ip_parameters->ApplyTo(&properties_);
  static_ip_parameters->SetIPConfig(weak_ptr_factory_.GetWeakPtr());
  EmitChanges();
}

void IPConfig::RestoreSavedIPParameters(
    StaticIPParameters* static_ip_parameters) {
  static_ip_parameters->RestoreTo(&properties_);
  EmitChanges();
}

void IPConfig::UpdateLeaseExpirationTime(uint32_t new_lease_duration) {
  struct timeval new_expiration_time;
  time_->GetTimeBoottime(&new_expiration_time);
  new_expiration_time.tv_sec += new_lease_duration;
  current_lease_expiration_time_ = new_expiration_time;
}

void IPConfig::ResetLeaseExpirationTime() {
  current_lease_expiration_time_ = {kDefaultLeaseExpirationTime, 0};
}

bool IPConfig::TimeToLeaseExpiry(uint32_t* time_left) {
  if (current_lease_expiration_time_.tv_sec == kDefaultLeaseExpirationTime) {
    SLOG(this, 2) << __func__ << ": No current DHCP lease";
    return false;
  }
  struct timeval now;
  time_->GetTimeBoottime(&now);
  if (now.tv_sec > current_lease_expiration_time_.tv_sec) {
    SLOG(this, 2) << __func__ << ": Current DHCP lease has already expired";
    return false;
  }
  *time_left = current_lease_expiration_time_.tv_sec - now.tv_sec;
  return true;
}

bool IPConfig::SetBlackholedUids(const std::vector<uint32_t>& uids) {
  if (properties_.blackholed_uids == uids) {
    return false;
  }
  properties_.blackholed_uids = uids;
  return true;
}

bool IPConfig::ClearBlackholedUids() {
  return SetBlackholedUids(std::vector<uint32_t>());
}

void IPConfig::UpdateProperties(const Properties& properties,
                                bool new_lease_acquired) {
  // Take a reference of this instance to make sure we don't get destroyed in
  // the middle of this call. (The |update_callback_| may cause a reference
  // to be dropped. See, e.g., EthernetService::Disconnect and
  // Ethernet::DropConnection.)
  IPConfigRefPtr me = this;

  properties_ = properties;

  if (!update_callback_.is_null()) {
    update_callback_.Run(this, new_lease_acquired);
  }
  EmitChanges();
}

void IPConfig::UpdateDNSServers(std::vector<std::string> dns_servers) {
  properties_.dns_servers = std::move(dns_servers);
  EmitChanges();
}

void IPConfig::NotifyFailure() {
  // Take a reference of this instance to make sure we don't get destroyed in
  // the middle of this call. (The |update_callback_| may cause a reference
  // to be dropped. See, e.g., EthernetService::Disconnect and
  // Ethernet::DropConnection.)
  IPConfigRefPtr me = this;

  if (!failure_callback_.is_null()) {
    failure_callback_.Run(this);
  }
}

void IPConfig::NotifyExpiry() {
  if (!expire_callback_.is_null()) {
    expire_callback_.Run(this);
  }
}

void IPConfig::RegisterUpdateCallback(const UpdateCallback& callback) {
  update_callback_ = callback;
}

void IPConfig::RegisterFailureCallback(const Callback& callback) {
  failure_callback_ = callback;
}

void IPConfig::RegisterRefreshCallback(const Callback& callback) {
  refresh_callback_ = callback;
}

void IPConfig::RegisterExpireCallback(const Callback& callback) {
  expire_callback_ = callback;
}

void IPConfig::ResetProperties() {
  properties_ = Properties();
  EmitChanges();
}

void IPConfig::EmitChanges() {
  adaptor_->EmitStringChanged(kAddressProperty, properties_.address);
  adaptor_->EmitStringsChanged(kNameServersProperty, properties_.dns_servers);
}

}  // namespace shill
