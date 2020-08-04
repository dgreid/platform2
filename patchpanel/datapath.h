// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_DATAPATH_H_
#define PATCHPANEL_DATAPATH_H_

#include <net/route.h>
#include <sys/types.h>

#include <string>

#include <base/macros.h>

#include "patchpanel/firewall.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/subnet.h"

namespace patchpanel {

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
  virtual ~Datapath() = default;

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

  // Creates a virtual interface pair.
  virtual bool AddVirtualInterfacePair(const std::string& netns_name,
                                       const std::string& veth_ifname,
                                       const std::string& peer_ifname);

  // Sets the link status.
  virtual bool ToggleInterface(const std::string& ifname, bool up);

  // Sets the configuration of an interface.
  virtual bool ConfigureInterface(const std::string& ifname,
                                  const MacAddress& mac_addr,
                                  uint32_t ipv4_addr,
                                  uint32_t ipv4_prefix_len,
                                  bool up,
                                  bool enable_multicast);

  virtual void RemoveInterface(const std::string& ifname);

  // Create (or delete) pre-routing rules allowing direct ingress on |ifname|
  // to guest desintation |ipv4_addr|.
  virtual bool AddInboundIPv4DNAT(const std::string& ifname,
                                  const std::string& ipv4_addr);
  virtual void RemoveInboundIPv4DNAT(const std::string& ifname,
                                     const std::string& ipv4_addr);

  // Create (or delete) a forwarding rule for |ifname|.
  virtual bool AddOutboundIPv4(const std::string& ifname);
  virtual void RemoveOutboundIPv4(const std::string& ifname);

  // Creates (or deletes) the forwarding and postrouting rules for SNAT
  // fwmarked IPv4 traffic.
  virtual bool AddSNATMarkRules();
  virtual void RemoveSNATMarkRules();

  virtual bool AddInterfaceSNAT(const std::string& ifname);
  virtual void RemoveInterfaceSNAT(const std::string& ifname);

  // Create (or delete) a mangle PREROUTING rule for marking IPv4 traffic
  // outgoing of |ifname| with the SNAT fwmark value 0x1.
  // TODO(hugobenichi) Refer to RoutingService to obtain the fwmark value and
  // add a fwmark mask in the generated rule.
  virtual bool AddOutboundIPv4SNATMark(const std::string& ifname);
  virtual void RemoveOutboundIPv4SNATMark(const std::string& ifname);

  // Create (or delete) a forward rule for established connections.
  virtual bool AddForwardEstablishedRule();
  virtual void RemoveForwardEstablishedRule();

  // Methods supporting IPv6 configuration for ARC.
  virtual bool MaskInterfaceFlags(const std::string& ifname,
                                  uint16_t on,
                                  uint16_t off = 0);

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

  virtual bool AddIPv6Neighbor(const std::string& ifname,
                               const std::string& ipv6_addr);
  virtual void RemoveIPv6Neighbor(const std::string& ifname,
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

  MinijailedProcessRunner& runner() const;

 private:
  MinijailedProcessRunner* process_runner_;
  Firewall* firewall_;
  ioctl_t ioctl_;

  bool ModifyRtentry(unsigned long op, struct rtentry* route);

  DISALLOW_COPY_AND_ASSIGN(Datapath);
};

}  // namespace patchpanel

#endif  // PATCHPANEL_DATAPATH_H_
