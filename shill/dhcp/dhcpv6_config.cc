// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp/dhcpv6_config.h"

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/dhcp/dhcp_provider.h"
#include "shill/logging.h"
#include "shill/net/ip_address.h"

using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
static string ObjectID(const DHCPv6Config* d) {
  if (d == nullptr)
    return "(DHCPv6_config)";
  else
    return d->device_name();
}
}  // namespace Logging

// static
const char DHCPv6Config::kDHCPCDPathFormatPID[] =
    "var/run/dhcpcd/dhcpcd-%s-6.pid";

const char DHCPv6Config::kConfigurationKeyDelegatedPrefix[] =
    "DHCPv6DelegatedPrefix";
const char DHCPv6Config::kConfigurationKeyDelegatedPrefixLength[] =
    "DHCPv6DelegatedPrefixLength";
const char DHCPv6Config::kConfigurationKeyDelegatedPrefixLeaseTime[] =
    "DHCPv6DelegatedPrefixLeaseTime";
const char DHCPv6Config::kConfigurationKeyDelegatedPrefixPreferredLeaseTime[] =
    "DHCPv6DelegatedPrefixPreferredLeaseTime";
const char DHCPv6Config::kConfigurationKeyDelegatedPrefixIaid[] =
    "DHCPv6DelegatedPrefixIAID";
const char DHCPv6Config::kConfigurationKeyDNS[] = "DHCPv6NameServers";
const char DHCPv6Config::kConfigurationKeyDomainSearch[] = "DHCPv6DomainSearch";
const char DHCPv6Config::kConfigurationKeyIPAddress[] = "DHCPv6Address";
const char DHCPv6Config::kConfigurationKeyIPAddressLeaseTime[] =
    "DHCPv6AddressLeaseTime";
const char DHCPv6Config::kConfigurationKeyIPAddressPreferredLeaseTime[] =
    "DHCPv6AddressPreferredLeaseTime";
const char DHCPv6Config::kConfigurationKeyServerIdentifier[] =
    "DHCPv6ServerIdentifier";
const char DHCPv6Config::kConfigurationKeyIPAddressIaid[] = "DHCPv6AddressIAID";

const char DHCPv6Config::kReasonBound[] = "BOUND6";
const char DHCPv6Config::kReasonFail[] = "FAIL6";
const char DHCPv6Config::kReasonRebind[] = "REBIND6";
const char DHCPv6Config::kReasonReboot[] = "REBOOT6";
const char DHCPv6Config::kReasonRenew[] = "RENEW6";

const char DHCPv6Config::kType[] = "dhcp6";

DHCPv6Config::DHCPv6Config(ControlInterface* control_interface,
                           EventDispatcher* dispatcher,
                           DHCPProvider* provider,
                           const string& device_name,
                           const string& lease_file_suffix)
    : DHCPConfig(control_interface,
                 dispatcher,
                 provider,
                 device_name,
                 kType,
                 lease_file_suffix) {
  SLOG(this, 2) << __func__ << ": " << device_name;
}

DHCPv6Config::~DHCPv6Config() {
  SLOG(this, 2) << __func__ << ": " << device_name();
}

void DHCPv6Config::ProcessEventSignal(const string& reason,
                                      const KeyValueStore& configuration) {
  LOG(INFO) << "Event reason: " << reason;
  if (reason == kReasonFail) {
    LOG(ERROR) << "Received failure event from DHCPv6 client.";
    NotifyFailure();
    return;
  } else if (reason != kReasonBound && reason != kReasonRebind &&
             reason != kReasonReboot && reason != kReasonRenew) {
    LOG(WARNING) << "Event ignored.";
    return;
  }

  CHECK(ParseConfiguration(configuration));

  // This needs to be set before calling UpdateProperties() below since
  // those functions may indirectly call other methods like ReleaseIP that
  // depend on or change this value.
  set_is_lease_active(true);

  DHCPConfig::UpdateProperties(properties_, true);
}

void DHCPv6Config::ProcessStatusChangeSignal(const string& status) {
  SLOG(this, 2) << __func__ << ": " << status;
  // TODO(zqiu): metric reporting for status.
}

void DHCPv6Config::CleanupClientState() {
  DHCPConfig::CleanupClientState();

  // Delete lease file if it is ephemeral.
  if (IsEphemeralLease()) {
    base::DeleteFile(root().Append(base::StringPrintf(
        DHCPProvider::kDHCPCDPathFormatLease6, device_name().c_str())));
  }
  base::DeleteFile(root().Append(
      base::StringPrintf(kDHCPCDPathFormatPID, device_name().c_str())));

  // Reset configuration data.
  properties_ = IPConfig::Properties();
}

vector<string> DHCPv6Config::GetFlags() {
  // Get default flags first.
  vector<string> flags = DHCPConfig::GetFlags();

  flags.push_back("-6");  // IPv6 only.
  flags.push_back("-a");  // Request ia_na and ia_pd.
  return flags;
}

bool DHCPv6Config::ParseConfiguration(const KeyValueStore& configuration) {
  SLOG(nullptr, 2) << __func__;
  properties_.method = kTypeDHCP6;
  properties_.address_family = IPAddress::kFamilyIPv6;

  if (configuration.Contains<uint32_t>(kConfigurationKeyIPAddressIaid)) {
    properties_.dhcpv6_addresses.clear();
  }
  if (configuration.Contains<uint32_t>(kConfigurationKeyDelegatedPrefixIaid)) {
    properties_.dhcpv6_delegated_prefixes.clear();
  }

  // This is the number of addresses and prefixes we currently export from
  // dhcpcd.  Note that dhcpcd's numbering starts from 1.
  for (int i = 1; i < 4; ++i) {
    const std::string prefix_key =
        base::StringPrintf("%s%d", kConfigurationKeyDelegatedPrefix, i);
    const std::string prefix_length_key =
        base::StringPrintf("%s%d", kConfigurationKeyDelegatedPrefixLength, i);
    const std::string prefix_lease_time_key = base::StringPrintf(
        "%s%d", kConfigurationKeyDelegatedPrefixLeaseTime, i);
    const std::string prefix_preferred_lease_time_key = base::StringPrintf(
        "%s%d", kConfigurationKeyDelegatedPrefixPreferredLeaseTime, i);

    if (configuration.Contains<string>(prefix_key) &&
        configuration.Contains<uint32_t>(prefix_length_key) &&
        configuration.Contains<uint32_t>(prefix_lease_time_key) &&
        configuration.Contains<uint32_t>(prefix_preferred_lease_time_key)) {
      uint32_t lease_time = configuration.Get<uint32_t>(prefix_lease_time_key);
      uint32_t preferred_lease_time =
          configuration.Get<uint32_t>(prefix_preferred_lease_time_key);
      properties_.dhcpv6_delegated_prefixes.push_back({
          {kDhcpv6AddressProperty, configuration.Get<string>(prefix_key)},
          {kDhcpv6LengthProperty,
           base::NumberToString(
               configuration.Get<uint32_t>(prefix_length_key))},
          {kDhcpv6LeaseDurationSecondsProperty,
           base::NumberToString(lease_time)},
          {kDhcpv6PreferredLeaseDurationSecondsProperty,
           base::NumberToString(preferred_lease_time)},
      });
      UpdateLeaseTime(lease_time);
    }

    const std::string address_key =
        base::StringPrintf("%s%d", kConfigurationKeyIPAddress, i);
    const std::string address_lease_time_key =
        base::StringPrintf("%s%d", kConfigurationKeyIPAddressLeaseTime, i);
    const std::string address_preferred_lease_time_key = base::StringPrintf(
        "%s%d", kConfigurationKeyIPAddressPreferredLeaseTime, i);

    if (configuration.Contains<string>(address_key) &&
        configuration.Contains<uint32_t>(address_lease_time_key) &&
        configuration.Contains<uint32_t>(address_preferred_lease_time_key)) {
      uint32_t lease_time = configuration.Get<uint32_t>(address_lease_time_key);
      uint32_t preferred_lease_time =
          configuration.Get<uint32_t>(address_preferred_lease_time_key);
      properties_.dhcpv6_addresses.push_back({
          {kDhcpv6AddressProperty, configuration.Get<string>(address_key)},
          {kDhcpv6LengthProperty, "128"},  // IPv6 addresses are 128 bits long.
          {kDhcpv6LeaseDurationSecondsProperty,
           base::NumberToString(lease_time)},
          {kDhcpv6PreferredLeaseDurationSecondsProperty,
           base::NumberToString(preferred_lease_time)},
      });
      UpdateLeaseTime(lease_time);
    }
  }

  if (configuration.Contains<Strings>(kConfigurationKeyDNS)) {
    properties_.dns_servers = configuration.Get<Strings>(kConfigurationKeyDNS);
  }
  if (configuration.Contains<Strings>(kConfigurationKeyDomainSearch)) {
    properties_.domain_search =
        configuration.Get<Strings>(kConfigurationKeyDomainSearch);
  }

  return true;
}

void DHCPv6Config::UpdateLeaseTime(uint32_t lease_time) {
  // IP address and delegated prefix are provided as separate lease. Use
  // the shorter time of the two lease as the lease time. However, ignore zero
  // lease times as those are for expired leases.
  if (lease_time > 0 && (properties_.lease_duration_seconds == 0 ||
                         lease_time < properties_.lease_duration_seconds)) {
    properties_.lease_duration_seconds = lease_time;
  }
}

}  // namespace shill
