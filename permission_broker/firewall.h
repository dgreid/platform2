// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_FIREWALL_H_
#define PERMISSION_BROKER_FIREWALL_H_

#include <stdint.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <brillo/errors/error.h>
#include <gtest/gtest_prod.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

namespace permission_broker {

using patchpanel::ModifyPortRuleRequest;
using Operation = patchpanel::ModifyPortRuleRequest::Operation;
using Protocol = patchpanel::ModifyPortRuleRequest::Protocol;
using RuleType = patchpanel::ModifyPortRuleRequest::RuleType;

extern const char kIpTablesPath[];
extern const char kIp6TablesPath[];

const std::string ProtocolName(Protocol proto);

class Firewall {
 public:
  typedef std::pair<uint16_t, std::string> Hole;

  Firewall() = default;
  ~Firewall() = default;

  bool AddAcceptRules(Protocol protocol,
                      uint16_t port,
                      const std::string& interface);
  bool DeleteAcceptRules(Protocol protocol,
                         uint16_t port,
                         const std::string& interface);
  bool AddLoopbackLockdownRules(Protocol protocol, uint16_t port);
  bool DeleteLoopbackLockdownRules(Protocol protocol, uint16_t port);
  bool AddIpv4ForwardRule(Protocol protocol,
                          const std::string& input_ip,
                          uint16_t port,
                          const std::string& interface,
                          const std::string& dst_ip,
                          uint16_t dst_port);
  bool DeleteIpv4ForwardRule(Protocol protocol,
                             const std::string& input_ip,
                             uint16_t port,
                             const std::string& interface,
                             const std::string& dst_ip,
                             uint16_t dst_port);

 private:
  friend class FirewallTest;
  // Adds ACCEPT chain rules to the filter INPUT chain.
  virtual bool AddAcceptRule(const std::string& executable_path,
                             Protocol protocol,
                             uint16_t port,
                             const std::string& interface);
  // Removes ACCEPT chain rules from the filter INPUT chain.
  virtual bool DeleteAcceptRule(const std::string& executable_path,
                                Protocol protocol,
                                uint16_t port,
                                const std::string& interface);
  // Adds or removes MASQUERADE chain rules to/from the nat PREROUTING chain.
  virtual bool ModifyIpv4DNATRule(Protocol protocol,
                                  const std::string& input_ip,
                                  uint16_t port,
                                  const std::string& interface,
                                  const std::string& dst_ip,
                                  uint16_t dst_port,
                                  const std::string& operation);
  // Adds or removes ACCEPT chain rules to/from the filter FORWARD chain.
  virtual bool ModifyIpv4ForwardChain(Protocol protocol,
                                      const std::string& interface,
                                      const std::string& dst_ip,
                                      uint16_t dst_port,
                                      const std::string& operation);
  virtual bool AddLoopbackLockdownRule(const std::string& executable_path,
                                       Protocol protocol,
                                       uint16_t port);
  virtual bool DeleteLoopbackLockdownRule(const std::string& executable_path,
                                          Protocol protocol,
                                          uint16_t port);

  // Even though permission_broker runs as a regular user, it can still add
  // other restrictions when launching 'iptables'.
  virtual int RunInMinijail(const std::vector<std::string>& argv);

  DISALLOW_COPY_AND_ASSIGN(Firewall);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_FIREWALL_H_
