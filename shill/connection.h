// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CONNECTION_H_
#define SHILL_CONNECTION_H_

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/ipconfig.h"
#include "shill/net/ip_address.h"
#include "shill/refptr_types.h"
#include "shill/routing_policy_entry.h"
#include "shill/technology.h"

namespace shill {

class ControlInterface;
class DeviceInfo;
class RTNLHandler;
class Resolver;
class RoutingTable;

// The Connection maintains the implemented state of an IPConfig, e.g,
// the IP address, routing table and DNS table entries.
class Connection : public base::RefCounted<Connection> {
 public:
  // The routing rule priority used for the default service, whether physical or
  // VPN.
  static const uint32_t kDefaultPriority;
  // Priority for rules corresponding to IPConfig::Properties::routes.
  static const uint32_t kDstRulePriority;
  // Priority for the rule sending any remaining traffic to the default physical
  // interface.
  static const uint32_t kCatchallPriority;
  // The lowest priority value that is still valid.
  static const uint32_t kLeastPriority;
  // Space between the priorities of services. The Nth highest priority service
  // (starting from N=0) will have a rule priority of
  // |kDefaultPriority| + N*|kPriorityStep|.
  static const uint32_t kPriorityStep;

  Connection(int interface_index,
             const std::string& interface_name,
             bool fixed_ip_params,
             Technology technology_,
             const DeviceInfo* device_info,
             ControlInterface* control_interface);
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  // Add the contents of an IPConfig reference to the list of managed state.
  // This will replace all previous state for this address family.
  virtual void UpdateFromIPConfig(const IPConfigRefPtr& config);

  // Update the metric on the default route in |config|, if any.  This
  // should be called after the kernel notifies shill that a new IPv6
  // address+gateway have been configured.
  virtual void UpdateGatewayMetric(const IPConfigRefPtr& config);

  // Adds |interface_name| to the allowed input interfaces that are
  // allowed to use the connection and updates the routing table.
  virtual void AddInputInterfaceToRoutingTable(
      const std::string& interface_name);
  // Removes |interface_name| from the allowed input interfaces and
  // updates the routing table.
  virtual void RemoveInputInterfaceFromRoutingTable(
      const std::string& interface_name);

  // Routing policy rules have priorities, which establishes the order in which
  // policy rules will be matched against the current traffic. The higher the
  // priority value, the lower the priority of the rule. 0 is the highest rule
  // priority and is generally reserved for the kernel.
  //
  // Updates the kernel's routing policy rule database such that policy rules
  // corresponding to this Connection will use |priority| as the "base
  // priority". This call also updates the systemwide DNS configuration if
  // necessary, and triggers captive portal detection if the connection has
  // transitioned from non-default to default.
  virtual void SetPriority(uint32_t priority, bool is_primary_physical);

  // Returns true if this connection is currently the systemwide default.
  virtual bool IsDefault() const;

  // Determines whether this connection controls the system DNS settings.
  // This should only be true for one connection at a time.
  virtual void SetUseDNS(bool enable);

  // Update and apply the new DNS servers setting to this connection.
  virtual void UpdateDNSServers(const std::vector<std::string>& dns_servers);

  virtual const std::string& interface_name() const { return interface_name_; }
  virtual int interface_index() const { return interface_index_; }
  virtual const std::vector<std::string>& dns_servers() const {
    return dns_servers_;
  }
  virtual uint32_t table_id() const { return table_id_; }

  virtual const RpcIdentifier& ipconfig_rpc_identifier() const {
    return ipconfig_rpc_identifier_;
  }

  // Flush and (re)create routing policy rules for the connection.  If
  // |allowed_uids_| or |allowed_iifs_| is set, rules will be created
  // to restrict traffic to the allowed UIDs or input interfaces.
  // Otherwise, all system traffic will be allowed to use the connection.
  // The rule priority will be set to |priority_| so that Manager's service
  // sort ranking is respected.
  virtual void UpdateRoutingPolicy();

  // Return the subnet name for this connection.
  virtual std::string GetSubnetName() const;

  virtual const IPAddress& local() const { return local_; }
  virtual const IPAddress& gateway() const { return gateway_; }
  virtual Technology technology() const { return technology_; }
  void set_allowed_srcs(std::vector<IPAddress> addresses);
  virtual const std::string& tethering() const { return tethering_; }
  void set_tethering(const std::string& tethering) { tethering_ = tethering; }

  // Return true if this is an IPv6 connection.
  virtual bool IsIPv6();

 protected:
  friend class base::RefCounted<Connection>;

  virtual ~Connection();

 private:
  friend class ConnectionTest;

  // Create a link route to the gateway when the gateway is in a separate
  // subnet. This can work if the host LAN and gateway LAN are bridged
  // together, but is not a recommended network configuration.
  bool FixGatewayReachability(const IPAddress& local,
                              IPAddress* peer,
                              IPAddress* gateway);
  // Allow for the routes specified in |properties.routes| to be served by this
  // connection.
  bool SetupIncludedRoutes(const IPConfig::Properties& properties);
  // Ensure the destination subnets specified in |properties.exclusion_list|
  // will not be served by this connection.
  bool SetupExcludedRoutes(const IPConfig::Properties& properties,
                           const IPAddress& gateway);
  void SetMTU(int32_t mtu);

  // Allow for traffic corresponding to this Connection to match with
  // |table_id|. Note that this does *not* necessarily imply that the traffic
  // will actually be routed through a route in |table_id|. For example, if the
  // traffic matches one of the excluded destination addresses set up in
  // SetupExcludedRoutes, then no routes in the per-Device table for this
  // Connection will be used for that traffic.
  void AllowTrafficThrough(uint32_t table_id, uint32_t base_priority);

  // Send our DNS configuration to the resolver.
  void PushDNSConfig();

  base::WeakPtrFactory<Connection> weak_ptr_factory_;

  bool use_dns_;
  // The base priority for rules corresponding to this Connection. Set by
  // Manager through SetPriority. Note that this value is occasionally used as a
  // route metric value (e.g., SetPriority passes the priority value to
  // RoutingTable::SetDefaultMetric). This is simply done for convenience, such
  // that one could do something like `ip route show table 0 0/0` be able to
  // tell the rule priorities corresponding to the displayed default routes.
  uint32_t priority_;
  bool is_primary_physical_;
  bool has_broadcast_domain_;
  int interface_index_;
  const std::string interface_name_;
  Technology technology_;
  std::vector<std::string> dns_servers_;
  std::vector<std::string> dns_domain_search_;
  std::string dns_domain_name_;
  RpcIdentifier ipconfig_rpc_identifier_;

  // True if this device should have rules sending traffic whose src address
  // matches one of the interface's addresses to the per-device table.
  bool use_if_addrs_;
  // |allowed_{uids,iifs,dsts,srcs}| and |included_fwmarks_| allow for this
  // connection to serve more traffic than it would by default.
  // TODO(crbug.com/1022028) Replace this with a RoutingPolicy.
  std::vector<uint32_t> allowed_uids_;
  std::vector<std::string> allowed_iifs_;
  std::vector<IPAddress> allowed_srcs_;
  std::vector<IPAddress> allowed_dsts_;
  std::vector<RoutingPolicyEntry::FwMark> included_fwmarks_;
  std::vector<uint32_t> blackholed_uids_;

  // Do not reconfigure the IP addresses, subnet mask, broadcast, etc.
  bool fixed_ip_params_;
  uint32_t table_id_;
  uint32_t blackhole_table_id_;
  IPAddress local_;
  IPAddress gateway_;

  // Track the tethering status of the Service associated with this connection.
  // This property is set by a service as it takes ownership of a connection,
  // and is read by services that are bound through this connection.
  std::string tethering_;

  // Store cached copies of singletons for speed/ease of testing
  const DeviceInfo* device_info_;
  Resolver* resolver_;
  RoutingTable* routing_table_;
  RTNLHandler* rtnl_handler_;

  ControlInterface* control_interface_;
};

}  // namespace shill

#endif  // SHILL_CONNECTION_H_
