// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_PORT_TRACKER_H_
#define PERMISSION_BROKER_PORT_TRACKER_H_

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/sequenced_task_runner.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

namespace permission_broker {

using patchpanel::ModifyPortRuleRequest;
using Operation = patchpanel::ModifyPortRuleRequest::Operation;
using Protocol = patchpanel::ModifyPortRuleRequest::Protocol;
using RuleType = patchpanel::ModifyPortRuleRequest::RuleType;

class PortTracker {
 public:
  struct PortRuleKey {
    Protocol proto;
    uint16_t input_dst_port;
    std::string input_ifname;

    bool operator==(const PortRuleKey& other) const {
      return proto == other.proto && input_dst_port == other.input_dst_port &&
             input_ifname == other.input_ifname;
    }
  };

  // Helper for using PortRuleKey as key entries in std::unordered_maps.
  struct PortRuleKeyHasher {
    std::size_t operator()(const PortRuleKey& k) const {
      return ((std::hash<int>()(k.proto) ^
               (std::hash<uint16_t>()(k.input_dst_port) << 1)) >>
              1) ^
             (std::hash<std::string>()(k.input_ifname) << 1);
    }
  };

  // The different types of port rules supported.
  enum PortRuleType : uint8_t {
    // Forces default PortRuleType zero values to be different from any valid
    // port rule type.
    kUnknownRule = 0,
    // Rule for opening ingress traffic on a destination port.
    kAccessRule = 1,
    // Rule for closing a destination port to locally originated traffic.
    kLockdownRule = 2,
    // Rule for forwarding ingress traffic on a destination port.
    kForwardingRule = 3,
    // Rule for forwarding ADB traffic to arc-networkd's adb-proxy.
    kAdbForwardingRule = 4,
    // Guard value used by FuzzedDataProvider. The name must remain unchanged.
    kMaxValue = kForwardingRule
  };

  struct PortRule {
    int lifeline_fd;
    PortRuleType type;
    Protocol proto;
    std::string input_dst_ip;
    uint16_t input_dst_port;
    std::string input_ifname;
    std::string dst_ip;
    uint16_t dst_port;
  };

  PortTracker();
  virtual ~PortTracker();

  bool AllowTcpPortAccess(uint16_t port, const std::string& iface, int dbus_fd);
  bool AllowUdpPortAccess(uint16_t port, const std::string& iface, int dbus_fd);
  bool RevokeTcpPortAccess(uint16_t port, const std::string& iface);
  bool RevokeUdpPortAccess(uint16_t port, const std::string& iface);
  bool LockDownLoopbackTcpPort(uint16_t port, int dbus_fd);
  bool ReleaseLoopbackTcpPort(uint16_t port);
  bool StartTcpPortForwarding(uint16_t input_dst_port,
                              const std::string& input_ifname,
                              const std::string& dst_ip,
                              uint16_t dst_port,
                              int dbus_fd);
  bool StartUdpPortForwarding(uint16_t input_dst_port,
                              const std::string& input_ifname,
                              const std::string& dst_ip,
                              uint16_t dst_port,
                              int dbus_fd);
  bool StopTcpPortForwarding(uint16_t input_dst_port,
                             const std::string& input_ifname);
  bool StopUdpPortForwarding(uint16_t input_dst_port,
                             const std::string& input_ifname);
  bool StartAdbPortForwarding(const std::string& input_ifname, int dbus_fd);
  bool StopAdbPortForwarding(const std::string& input_ifname);
  bool HasActiveRules();

  // Close all outstanding firewall holes, revoke all forwarding rules, and
  // unblock all loopback ports.
  void RevokeAllPortRules();

 protected:
  explicit PortTracker(scoped_refptr<base::SequencedTaskRunner> task_runner);

 private:
  // Call patchpanel's DBus API to create or remove firewall rule.
  virtual bool ModifyPortRule(Operation op, const PortRule& rule);

  // Helper functions for process lifetime tracking.
  virtual int AddLifelineFd(int dbus_fd);
  virtual bool DeleteLifelineFd(int fd);
  virtual void CheckLifelineFds(bool reschedule_check);
  virtual void ScheduleLifelineCheck();

  bool AddPortRule(const PortRule& rule, int dbus_fd);
  bool ValidatePortRule(const PortRule& rule);

  // Revoke port rule keyed by |key|. Create a copy of |key| to prevent
  // accidentally deleting |key| through |lifeline_fds_| or |port_rules_|.
  bool RevokePortRule(const PortRuleKey key);

  // epoll(7) helper functions.
  virtual bool InitializeEpollOnce();

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  int epfd_;

  // For each port rule (protocol, port, interface), keep track of which fd
  // requested it.  We need this for Release{Tcp|Udp}Port(), to avoid
  // traversing |lifeline_fds_| each time.
  std::unordered_map<PortRuleKey, PortRule, PortRuleKeyHasher> port_rules_;

  // For each fd (process), keep track of which rule (protocol, port, interface)
  // it requested.
  std::map<int, PortRuleKey> lifeline_fds_;

  DISALLOW_COPY_AND_ASSIGN(PortTracker);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_PORT_TRACKER_H_
