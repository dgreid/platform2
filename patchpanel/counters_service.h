// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_COUNTERS_SERVICE_H_
#define PATCHPANEL_COUNTERS_SERVICE_H_

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/datapath.h"
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
  using SourceDevice = std::pair<TrafficCounter::Source, std::string>;
  struct Counter {
    Counter() = default;
    Counter(uint64_t rx_bytes,
            uint64_t rx_packets,
            uint64_t tx_bytes,
            uint64_t tx_packets);

    uint64_t rx_bytes = 0;
    uint64_t rx_packets = 0;
    uint64_t tx_bytes = 0;
    uint64_t tx_packets = 0;
  };

  CountersService(ShillClient* shill_client,
                  Datapath* datapath,
                  MinijailedProcessRunner* runner);
  ~CountersService() = default;

  // Collects and returns counters from all the existing iptables rules.
  // |devices| is the set of interfaces for which counters should be returned,
  // any unknown interfaces will be ignored. If |devices| is empty, counters for
  // all known interfaces will be returned. An empty map will be returned on
  // any failure. Note that currently all traffic to/from an interface will be
  // counted by (UNKNOWN, ifname), i.e., no other sources except for UNKNOWN are
  // used.
  std::map<SourceDevice, Counter> GetCounters(
      const std::set<std::string>& devices);

 private:
  bool MakeAccountingChain(const std::string& chain_name);

  // Creates a new rule using both iptables and ip6tables in the mangle table.
  // The first element in |params| should be "-I" (insert) or "-A" (append), and
  // this function will replace it with "-C" to do the check before executing
  // the actual insert or append command. This function will also append "-w" to
  // |params|. Note that |params| is passed by value because it will be modified
  // inside the function, and the normal pattern to use this function is passing
  // an rvalue (e.g., `IptablesNewRule({"-I", "INPUT", ...})`), so no extra copy
  // should happen in such cases.
  void IptablesNewRule(std::vector<std::string> params);

  // Installs the required chains and rules for the given shill device.
  void SetupChainsAndRules(const std::string& ifname);
  // Installs the accounting rules in the given accounting chain. Currently we
  // only need one rule to match all the traffic, without distinguishing
  // different sources.
  void SetupAccountingRules(const std::string& chain_name);

  void OnDeviceChanged(const std::set<std::string>& added,
                       const std::set<std::string>& removed);

  ShillClient* shill_client_;
  Datapath* datapath_;
  MinijailedProcessRunner* runner_;

  base::WeakPtrFactory<CountersService> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_COUNTERS_SERVICE_H_
