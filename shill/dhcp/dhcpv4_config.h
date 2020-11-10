// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_DHCPV4_CONFIG_H_
#define SHILL_DHCP_DHCPV4_CONFIG_H_

#include <string>
#include <vector>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/dhcp/dhcp_config.h"
#include "shill/dhcp/dhcp_properties.h"

namespace shill {

class Metrics;

// DHCPv4 client instance.
// |dhcp_props| may contain values for the request hostname and vendor class.
// If these properties have non-empty values, they will be used in the DHCP
// request.  If the Hostname property in dhcp_props is non-empty, it asks the
// DHCP server to register this hostname on our behalf, for purposes of
// administration or creating a dynamic DNS entry.
class DHCPv4Config : public DHCPConfig {
 public:
  DHCPv4Config(ControlInterface* control_interface,
               EventDispatcher* dispatcher,
               DHCPProvider* provider,
               const std::string& device_name,
               const std::string& lease_file_suffix,
               bool arp_gateway,
               const DhcpProperties& dhcp_props,
               Metrics* metrics);
  DHCPv4Config(const DHCPv4Config&) = delete;
  DHCPv4Config& operator=(const DHCPv4Config&) = delete;

  ~DHCPv4Config() override;

  // Inherited from DHCPConfig.
  void ProcessEventSignal(const std::string& reason,
                          const KeyValueStore& configuration) override;
  void ProcessStatusChangeSignal(const std::string& status) override;

 protected:
  // Inherited from DHCPConfig.
  void CleanupClientState() override;
  bool ShouldFailOnAcquisitionTimeout() override;
  bool ShouldKeepLeaseOnDisconnect() override;
  std::vector<std::string> GetFlags() override;

 private:
  friend class DHCPv4ConfigTest;
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalFail);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalGatewayArp);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalGatewayArpNak);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalSuccess);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, ProcessEventSignalUnknown);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, StoppedDuringFailureCallback);
  FRIEND_TEST(DHCPv4ConfigCallbackTest, StoppedDuringSuccessCallback);
  FRIEND_TEST(DHCPv4ConfigTest, GetIPv4AddressString);
  FRIEND_TEST(DHCPv4ConfigTest, ParseClasslessStaticRoutes);
  FRIEND_TEST(DHCPv4ConfigTest, ParseConfiguration);
  FRIEND_TEST(DHCPv4ConfigTest, ParseConfigurationWithMinimumMTU);
  FRIEND_TEST(DHCPv4ConfigTest, ProcessStatusChangeSingal);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithEmptyHostname);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithHostname);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithVendorClass);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithoutArpGateway);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithoutHostname);
  FRIEND_TEST(DHCPv4ConfigTest, StartWithoutVendorClass);

  static const char kDHCPCDPathFormatPID[];

  static const char kConfigurationKeyBroadcastAddress[];
  static const char kConfigurationKeyClasslessStaticRoutes[];
  static const char kConfigurationKeyDNS[];
  static const char kConfigurationKeyDomainName[];
  static const char kConfigurationKeyDomainSearch[];
  static const char kConfigurationKeyHostname[];
  static const char kConfigurationKeyIPAddress[];
  static const char kConfigurationKeyiSNSOptionData[];
  static const char kConfigurationKeyLeaseTime[];
  static const char kConfigurationKeyMTU[];
  static const char kConfigurationKeyRouters[];
  static const char kConfigurationKeySubnetCIDR[];
  static const char kConfigurationKeyVendorEncapsulatedOptions[];
  static const char kConfigurationKeyWebProxyAutoDiscoveryUrl[];

  static const char kReasonBound[];
  static const char kReasonFail[];
  static const char kReasonGatewayArp[];
  static const char kReasonNak[];
  static const char kReasonRebind[];
  static const char kReasonReboot[];
  static const char kReasonRenew[];

  static const char kStatusArpGateway[];
  static const char kStatusArpSelf[];
  static const char kStatusBound[];
  static const char kStatusDiscover[];
  static const char kStatusIgnoreAdditionalOffer[];
  static const char kStatusIgnoreFailedOffer[];
  static const char kStatusIgnoreInvalidOffer[];
  static const char kStatusIgnoreNonOffer[];
  static const char kStatusInform[];
  static const char kStatusInit[];
  static const char kStatusNakDefer[];
  static const char kStatusRebind[];
  static const char kStatusReboot[];
  static const char kStatusRelease[];
  static const char kStatusRenew[];
  static const char kStatusRequest[];

  static const char kType[];

  // Parses |classless_routes| into |properties|.  Sets the default gateway
  // if one is supplied and |properties| does not already contain one.  It
  // also sets the "routes" parameter of the IPConfig properties for all
  // routes not converted into the default gateway.  Returns true on
  // success, and false otherwise.
  static bool ParseClasslessStaticRoutes(const std::string& classless_routes,
                                         IPConfig::Properties* properties);

  // Parses |configuration| into |properties|. Returns true on success, and
  // false otherwise.
  bool ParseConfiguration(const KeyValueStore& configuration,
                          IPConfig::Properties* properties);

  // Returns the string representation of the IP address |address|, or an
  // empty string on failure.
  static std::string GetIPv4AddressString(unsigned int address);

  // Specifies whether to supply an argument to the DHCP client to validate
  // the acquired IP address using an ARP request to the gateway IP address.
  bool arp_gateway_;

  // Whether it is valid to retain the lease acquired via gateway ARP.
  bool is_gateway_arp_active_;

  // Hostname to be used in DHCP request.  Set from DhcpProperties in
  // constructor when present.
  std::string hostname_;

  // Vendor Class to be used in DHCP request.  Set from DhcpProperties in
  // constructor when present.
  std::string vendor_class_;

  Metrics* metrics_;
};

}  // namespace shill

#endif  // SHILL_DHCP_DHCPV4_CONFIG_H_
