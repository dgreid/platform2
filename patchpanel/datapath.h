// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DATAPATH_H_
#define PATCHPANEL_DATAPATH_H_

#include <net/route.h>
#include <sys/types.h>

#include <string>

#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "patchpanel/firewall.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/subnet.h"

namespace patchpanel {

// Simple enum of bitmasks used for specifying a set of IP family values.
enum IpFamily {
  NONE = 0,
  IPv4 = 1 << 0,
  IPv6 = 1 << 1,
  Dual = IPv4 | IPv6,  // (1 << 0) | (1 << 1);
};

// cros lint will yell to force using int16/int64 instead of long here, however
// note that unsigned long IS the correct signature for ioctl in Linux kernel -
// it's 32 bits on 32-bit platform and 64 bits on 64-bit one.
using ioctl_req_t = unsigned long;
typedef int (*ioctl_t)(int, ioctl_req_t, ...);

// Returns for given interface name the host name of a ARC veth pair.
std::string ArcVethHostName(const std::string& ifname);

// Returns the ARC bridge interface name for the given interface.
std::string ArcBridgeName(const std::string& ifname);

// ARC networking data path configuration utility.
// IPV4 addresses are always specified in singular dotted-form (a.b.c.d)
// (not in CIDR representation
class Datapath {
 public:
  // |process_runner| and |firewall| must not be null; it is not owned.
  Datapath(MinijailedProcessRunner* process_runner, Firewall* firewall);
  // Provided for testing only.
  Datapath(MinijailedProcessRunner* process_runner,
           Firewall* firewall,
           ioctl_t ioctl_hook);
  Datapath(const Datapath&) = delete;
  Datapath& operator=(const Datapath&) = delete;

  virtual ~Datapath() = default;

  // Start and stop the Datapath, creating or destroying the initial iptables
  // setup needed for forwarding traffic from VMs and containers and for
  // fwmark based routing.
  virtual void Start();
  virtual void Stop();

  // Attaches the name |netns_name| to a network namespace identified by
  // |netns_pid|. If |netns_name| had already been created, it will be deleted
  // first.
  virtual bool NetnsAttachName(const std::string& netns_name, pid_t netns_pid);

  // Deletes the name |netns_name| of a network namespace.
  virtual bool NetnsDeleteName(const std::string& netns_name);

  virtual bool AddBridge(const std::string& ifname,
                         uint32_t ipv4_addr,
                         uint32_t ipv4_prefix_len);
  virtual void RemoveBridge(const std::string& ifname);

  virtual bool AddToBridge(const std::string& br_ifname,
                           const std::string& ifname);

  // Adds a new TAP device.
  // |name| may be empty, in which case a default device name will be used;
  // it may be a template (e.g. vmtap%d), in which case the kernel will
  // generate the name; or it may be fully defined. In all cases, upon success,
  // the function returns the actual name of the interface.
  // |mac_addr| and |ipv4_addr| should be null if this interface will be later
  // bridged.
  // If |user| is empty, no owner will be set
  virtual std::string AddTAP(const std::string& name,
                             const MacAddress* mac_addr,
                             const SubnetAddress* ipv4_addr,
                             const std::string& user);

  // |ifname| must be the actual name of the interface.
  virtual void RemoveTAP(const std::string& ifname);

  // The following are iptables methods.
  // When specified, |ipv4_addr| is always singlar dotted-form (a.b.c.d)
  // IPv4 address (not a CIDR representation).

  // Creates a virtual interface pair split across the current namespace and the
  // namespace corresponding to |pid|, and set up the remote interface
  // |peer_ifname| according // to the given parameters.
  virtual bool ConnectVethPair(pid_t pid,
                               const std::string& netns_name,
                               const std::string& veth_ifname,
                               const std::string& peer_ifname,
                               const MacAddress& remote_mac_addr,
                               uint32_t remote_ipv4_addr,
                               uint32_t remote_ipv4_prefix_len,
                               bool remote_multicast_flag);

  virtual void RemoveInterface(const std::string& ifname);

  // Create (or delete) an OUTPUT DROP rule for any locally originated traffic
  // whose src IPv4 matches |src_ip| and would exit |oif|. This is mainly used
  // for dropping Chrome webRTC traffic incorrectly bound on ARC and other
  // guests virtual interfaces (chromium:898210).
  virtual bool AddSourceIPv4DropRule(const std::string& oif,
                                     const std::string& src_ip);
  virtual bool RemoveSourceIPv4DropRule(const std::string& oif,
                                        const std::string& src_ip);

  // Creates a virtual ethernet interface pair shared with the client namespace
  // of |pid| and sets up routing outside and inside the client namespace for
  // connecting the client namespace to the network.
  bool StartRoutingNamespace(pid_t pid,
                             const std::string& netns_name,
                             const std::string& host_ifname,
                             const std::string& peer_ifname,
                             uint32_t subnet_ipv4_addr,
                             uint32_t subnet_prefixlen,
                             uint32_t host_ipv4_addr,
                             uint32_t peer_ipv4_addr,
                             const MacAddress& peer_mac_addr);
  // Destroys the virtual ethernet interface, routing, and network namespace
  // name set for |netns_name| by StartRoutingNamespace. The default route set
  // inside the |netns_name| by patchpanel is not destroyed and it is assumed
  // the client will teardown the namespace.
  void StopRoutingNamespace(const std::string& netns_name,
                            const std::string& host_ifname,
                            uint32_t subnet_ipv4_addr,
                            uint32_t subnet_prefixlen,
                            uint32_t host_ipv4_addr);

  // Sets up IPv4 SNAT, IP forwarding, and traffic marking for the given
  // virtual device |int_ifname| associated to |source|. if |ext_ifname| is
  // empty, the device is implicitly routed through the highest priority
  // network.
  virtual void StartRoutingDevice(const std::string& ext_ifname,
                                  const std::string& int_ifname,
                                  uint32_t int_ipv4_addr,
                                  TrafficSource source);

  // Removes IPv4 iptables, IP forwarding, and traffic marking for the given
  // virtual device |int_ifname|.
  virtual void StopRoutingDevice(const std::string& ext_ifname,
                                 const std::string& int_ifname,
                                 uint32_t int_ipv4_addr,
                                 TrafficSource source);

  // Starts or stops marking conntrack entries routed to |ext_ifname| with its
  // associated fwmark routing tag. Once a conntrack entry is marked with the
  // fwmark routing tag of a external device, the connection will be pinned
  // to that deviced if conntrack fwmark restore is set for the source.
  virtual void StartConnectionPinning(const std::string& ext_ifname);
  virtual void StopConnectionPinning(const std::string& ext_ifname);

  // Methods supporting IPv6 configuration for ARC.
  virtual bool MaskInterfaceFlags(const std::string& ifname,
                                  uint16_t on,
                                  uint16_t off = 0);

  // Convenience functions for enabling or disabling IPv6 forwarding in both
  // directions between a pair of interfaces
  virtual bool AddIPv6Forwarding(const std::string& ifname1,
                                 const std::string& ifname2);
  virtual void RemoveIPv6Forwarding(const std::string& ifname1,
                                    const std::string& ifname2);

  virtual bool AddIPv6HostRoute(const std::string& ifname,
                                const std::string& ipv6_addr,
                                int ipv6_prefix_len);
  virtual void RemoveIPv6HostRoute(const std::string& ifname,
                                   const std::string& ipv6_addr,
                                   int ipv6_prefix_len);

  virtual bool AddIPv6Address(const std::string& ifname,
                              const std::string& ipv6_addr);
  virtual void RemoveIPv6Address(const std::string& ifname,
                                 const std::string& ipv6_addr);

  // Adds (or deletes) a route to direct to |gateway_addr| the traffic destined
  // to the subnet defined by |addr| and |netmask|.
  virtual bool AddIPv4Route(uint32_t gateway_addr,
                            uint32_t addr,
                            uint32_t netmask);
  virtual bool DeleteIPv4Route(uint32_t gateway_addr,
                               uint32_t addr,
                               uint32_t netmask);
  // Adds (or deletes) a route to direct to |ifname| the traffic destined to the
  // subnet defined by |addr| and |netmask|.
  virtual bool AddIPv4Route(const std::string& ifname,
                            uint32_t addr,
                            uint32_t netmask);
  virtual bool DeleteIPv4Route(const std::string& ifname,
                               uint32_t addr,
                               uint32_t netmask);

  // Adds (or deletes) an iptables rule for ADB port forwarding.
  virtual bool AddAdbPortForwardRule(const std::string& ifname);
  virtual void DeleteAdbPortForwardRule(const std::string& ifname);

  // Adds (or deletes) an iptables rule for ADB port access.
  virtual bool AddAdbPortAccessRule(const std::string& ifname);
  virtual void DeleteAdbPortAccessRule(const std::string& ifname);

  // Set or override the interface name to index mapping for |ifname|.
  // Only used for testing.
  void SetIfnameIndex(const std::string& ifname, int ifindex);

  MinijailedProcessRunner& runner() const;

 private:
  // Creates a virtual interface pair.
  bool AddVirtualInterfacePair(const std::string& netns_name,
                               const std::string& veth_ifname,
                               const std::string& peer_ifname);
  // Sets the configuration of an interface.
  bool ConfigureInterface(const std::string& ifname,
                          const MacAddress& mac_addr,
                          uint32_t ipv4_addr,
                          uint32_t ipv4_prefix_len,
                          bool up,
                          bool enable_multicast);
  // Sets the link status.
  bool ToggleInterface(const std::string& ifname, bool up);
  // Starts or stops accepting IP traffic forwarded between |iif| and |oif|
  // by adding or removing ACCEPT rules in the filter FORWARD chain of iptables
  // and/or ip6tables. If |iif| is empty, only specifies |oif| as the output
  // interface.  If |iif| is empty, only specifies |iif| as the input interface.
  // |oif| and |iif| cannot be both empty.
  bool StartIpForwarding(IpFamily family,
                         const std::string& iif,
                         const std::string& oif);
  bool StopIpForwarding(IpFamily family,
                        const std::string& iif,
                        const std::string& oif);
  // Create (or delete) pre-routing rules allowing direct ingress on |ifname|
  // to guest destination |ipv4_addr|.
  bool AddInboundIPv4DNAT(const std::string& ifname,
                          const std::string& ipv4_addr);
  void RemoveInboundIPv4DNAT(const std::string& ifname,
                             const std::string& ipv4_addr);
  // Create (or delete) a forwarding rule for |ifname|.
  // Creates (or deletes) the forwarding and postrouting rules for SNAT
  // fwmarked IPv4 traffic.
  bool AddSNATMarkRules();
  void RemoveSNATMarkRules();
  // Create (or delete) a mangle PREROUTING rule for marking IPv4 traffic
  // outgoing of |ifname| with the SNAT fwmark value 0x1.
  // TODO(hugobenichi) Refer to RoutingService to obtain the fwmark value and
  // add a fwmark mask in the generated rule.
  bool AddOutboundIPv4SNATMark(const std::string& ifname);
  void RemoveOutboundIPv4SNATMark(const std::string& ifname);

  bool ModifyConnmarkSetPostrouting(IpFamily family,
                                    const std::string& op,
                                    const std::string& oif);
  bool ModifyConnmarkSet(IpFamily family,
                         const std::string& chain,
                         const std::string& op,
                         const std::string& oif,
                         Fwmark mark,
                         Fwmark mask);
  bool ModifyConnmarkRestore(IpFamily family,
                             const std::string& chain,
                             const std::string& op,
                             const std::string& iif);
  bool ModifyFwmarkRoutingTag(const std::string& op,
                              const std::string& ext_ifname,
                              const std::string& int_ifname);
  bool ModifyFwmarkSourceTag(const std::string& op,
                             const std::string& iif,
                             TrafficSource source);
  bool ModifyFwmarkDefaultLocalSourceTag(const std::string& op,
                                         TrafficSource source);
  bool ModifyFwmarkLocalSourceTag(const std::string& op,
                                  const LocalSourceSpecs& source);
  bool ModifyFwmark(IpFamily family,
                    const std::string& chain,
                    const std::string& op,
                    const std::string& iif,
                    const std::string& uid_name,
                    Fwmark mark,
                    Fwmark mask,
                    bool log_failures = true);
  bool ModifyIpForwarding(IpFamily family,
                          const std::string& op,
                          const std::string& iif,
                          const std::string& oif,
                          bool log_failures = true);
  bool ModifyFwmarkVpnJumpRule(const std::string& chain,
                               const std::string& op,
                               const std::string& iif,
                               Fwmark mark,
                               Fwmark mask);
  bool ModifyChain(IpFamily family,
                   const std::string& table,
                   const std::string& op,
                   const std::string& chain);
  bool ModifyIptables(IpFamily family,
                      const std::string& table,
                      const std::vector<std::string>& argv);
  bool ModifyRtentry(ioctl_req_t op, struct rtentry* route);
  int FindIfIndex(const std::string& ifname);

  MinijailedProcessRunner* process_runner_;
  Firewall* firewall_;
  ioctl_t ioctl_;

  FRIEND_TEST(DatapathTest, AddInboundIPv4DNAT);
  FRIEND_TEST(DatapathTest, AddOutboundIPv4SNATMark);
  FRIEND_TEST(DatapathTest, AddSNATMarkRules);
  FRIEND_TEST(DatapathTest, AddVirtualInterfacePair);
  FRIEND_TEST(DatapathTest, ConfigureInterface);
  FRIEND_TEST(DatapathTest, RemoveInboundIPv4DNAT);
  FRIEND_TEST(DatapathTest, RemoveOutboundIPv4SNATMark);
  FRIEND_TEST(DatapathTest, RemoveSNATMarkRules);
  FRIEND_TEST(DatapathTest, StartStopIpForwarding);
  FRIEND_TEST(DatapathTest, ToggleInterface);

  // A map used for remembering the interface index of an interface. This
  // information is necessary when cleaning up iptables fwmark rules that
  // directly references the interface index. When removing these rules on
  // an RTM_DELLINK event, the interface index cannot be retrieved anymore.
  // A new entry is only added when a new physical device appears, and entries
  // are not removed.
  // TODO(b/161507671) Rely on RoutingService to obtain this information once
  // shill/routing_table.cc has been migrated to patchpanel.
  std::map<std::string, int> if_nametoindex_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_DATAPATH_H_
