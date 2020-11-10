// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_DHCPV6_CONFIG_H_
#define SHILL_DHCP_DHCPV6_CONFIG_H_

#include <string>
#include <vector>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/dhcp/dhcp_config.h"

namespace shill {

// This class provides a DHCPv6 client instance for the device |device_name|.
//
// The DHCPv6Config instance asks the DHCPv6 client to request both ia_na
// (Non-temporary Addresss) and ia_pd (Prefix Delegation) options from the
// DHCPv6 server.
class DHCPv6Config : public DHCPConfig {
 public:
  DHCPv6Config(ControlInterface* control_interface,
               EventDispatcher* dispatcher,
               DHCPProvider* provider,
               const std::string& device_name,
               const std::string& lease_file_suffix);
  DHCPv6Config(const DHCPv6Config&) = delete;
  DHCPv6Config& operator=(const DHCPv6Config&) = delete;

  ~DHCPv6Config() override;

  // Inherited from DHCPConfig.
  void ProcessEventSignal(const std::string& reason,
                          const KeyValueStore& configuration) override;
  void ProcessStatusChangeSignal(const std::string& status) override;

 protected:
  // Inherited from DHCPConfig.
  void CleanupClientState() override;
  std::vector<std::string> GetFlags() override;

 private:
  friend class DHCPv6ConfigTest;
  FRIEND_TEST(DHCPv6ConfigCallbackTest, ProcessEventSignalFail);
  FRIEND_TEST(DHCPv6ConfigCallbackTest, ProcessEventSignalSuccess);
  FRIEND_TEST(DHCPv6ConfigCallbackTest, ProcessEventSignalUnknown);
  FRIEND_TEST(DHCPv6ConfigCallbackTest, StoppedDuringFailureCallback);
  FRIEND_TEST(DHCPv6ConfigCallbackTest, StoppedDuringSuccessCallback);
  FRIEND_TEST(DHCPv6ConfigTest, ParseConfiguration);

  static const char kDHCPCDPathFormatPID[];

  static const char kConfigurationKeyDelegatedPrefix[];
  static const char kConfigurationKeyDelegatedPrefixLength[];
  static const char kConfigurationKeyDelegatedPrefixLeaseTime[];
  static const char kConfigurationKeyDelegatedPrefixPreferredLeaseTime[];
  static const char kConfigurationKeyDelegatedPrefixIaid[];
  static const char kConfigurationKeyDNS[];
  static const char kConfigurationKeyDomainSearch[];
  static const char kConfigurationKeyIPAddress[];
  static const char kConfigurationKeyIPAddressLeaseTime[];
  static const char kConfigurationKeyIPAddressPreferredLeaseTime[];
  static const char kConfigurationKeyIPAddressIaid[];
  static const char kConfigurationKeyServerIdentifier[];

  static const char kReasonBound[];
  static const char kReasonFail[];
  static const char kReasonRebind[];
  static const char kReasonReboot[];
  static const char kReasonRenew[];

  static const char kType[];

  // Parses |configuration| into |properties|. Returns true on success, and
  // false otherwise.
  bool ParseConfiguration(const KeyValueStore& configuration);

  void UpdateLeaseTime(uint32_t lease_time);

  // Non-temporary address and prefix delegation are considered separate
  // requests with separate leases, which mean there will be a dedicated
  // response/event for each. So maintain a configuration properties here to
  // combine the two lease/configuration into one. The lease time of the combine
  // configuration will be the shorter of the two leases (most likely the two
  // lease time will be identical).
  IPConfig::Properties properties_;
};

}  // namespace shill

#endif  // SHILL_DHCP_DHCPV6_CONFIG_H_
