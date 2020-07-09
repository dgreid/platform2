// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_COUNTERS_SERVICE_H_
#define PATCHPANEL_COUNTERS_SERVICE_H_

#include <set>
#include <string>
#include <vector>

#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

// This class manages the iptables rules for traffic counters, and queries
// iptables to get the counters when a request comes. This class will set up
// several iptable rules to track the counters for each possible combination of
// {bytes, packets} x (Traffic source) x (shill device) x {rx, tx} x {IPv4,
// IPv6}. These counters will never be removed after they are set up, and thus
// they represent the traffic usage from boot time.
//
// TODO(jiejiang): The following will be implemented in the following patches.
//
// Implementation details
//
// Rules: All the rules/chains for accounting are in (INPUT, FORWARD or
// POSTROUTING) chain in the mangle table. These rules take effect after routing
// and will not change the fate of a packet. When a new interface comes up, we
// will create the following new rules/chains (using both iptables and
// ip6tables):
// - Four accounting chains:
//   - For rx packets, `ingress_input_{ifname}` and `ingress_forward_{ifname}`
//     for INPUT and FORWARD chain, respectively;
//   - For tx packets, `egress_postrouting_{ifname}` and
//     `egress_forward_{ifname}` for POSTROUTING and FORWARD chain,
//     respectively. Note that we use `--socket-exists` in POSTROUTING chain to
//     avoid packets from FORWARD being matched again here.
// - One accounting rule in each accounting chain, which provides the actual
//   counter for accounting. We will extend this to several rules when source
//   marking is ready.
// - One jumping rule for each accounting chain in the corresponding prebuilt
//   chain, which matches packets with this new interface.
// The above rules and chains will never be removed once created, so we will
// check if one rule exists before creating it.
//
// Query: Two commands (iptables and ip6tables) will be executed in the mangle
// table to get all the chains and rules. And then we perform a text parsing on
// the output to get the counters. Counters for the same entry will be merged
// before return.
class CountersService {
 public:
  CountersService(ShillClient* shill_client, MinijailedProcessRunner* runner);
  ~CountersService() = default;

 private:
  // TODO(b/161060333): Move the following two functions elsewhere.
  // Creates a new chain using both iptables and ip6tables in the mangle table.
  void IptablesNewChain(const std::string& chain_name);

  // Creates a new rule using both iptables and ip6tables in the mangle table.
  // The first element in |params| should be "-I" (insert) or "-A" (append), and
  // this function will replace it with "-C" to do the check before executing
  // the actual insert or append command. This function will also append "-w" to
  // |params|. Note that |params| is passed by value because it will be modified
  // inside the function, and the normal pattern to use this function is passing
  // an rvalue (e.g., `IptablesNewRule({"-I", "INPUT", ...})`), so no extra copy
  // should happen in such cases.
  void IptablesNewRule(std::vector<std::string> params);

  void OnDeviceChanged(const std::set<std::string>& added,
                       const std::set<std::string>& removed);

  ShillClient* shill_client_;
  MinijailedProcessRunner* runner_;

  base::WeakPtrFactory<CountersService> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_COUNTERS_SERVICE_H_
