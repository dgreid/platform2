// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/counters_service.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/strings/strcat.h>
#include <base/strings/string_split.h>
#include <re2/re2.h>

#include "patchpanel/routing_service.h"

namespace patchpanel {

namespace {

using Counter = CountersService::Counter;
using SourceDevice = CountersService::SourceDevice;

constexpr char kMangleTable[] = "mangle";

// The following regexs and code is written and tested for iptables v1.6.2.
// Output code of iptables can be found at:
//   https://git.netfilter.org/iptables/tree/iptables/iptables.c?h=v1.6.2

// The chain line looks like:
//   "Chain tx_eth0 (2 references)".
// This regex extracts "tx" (direction), "eth0" (ifname) from this example.
constexpr LazyRE2 kChainLine = {R"(Chain (rx|tx)_(\w+).*)"};

// The counter line looks like (some spaces are deleted to make it fit in one
// line):
//   "    6511 68041668    all  --  any    any     anywhere   anywhere"
// The first two counters are captured for pkts and bytes.
constexpr LazyRE2 kCounterLine = {R"( *(\d+) +(\d+).*)"};

// Parses the output of `iptables -L -x -v` (or `ip6tables`) and adds the parsed
// values into the corresponding counters in |counters|. An example of |output|
// can be found in the test file. This function will try to find the pattern of:
//   <one chain line for an accounting chain>
//   <one header line>
//   <one counter line for an accounting rule>
// The interface name and direction (rx or tx) will be extracted from the chain
// line, and then the values extracted from the counter line will be added into
// the counter for that interface. Note that this function will not fully
// validate if |output| is an output from iptables.
bool ParseOutput(const std::string& output,
                 const std::set<std::string>& devices,
                 std::map<SourceDevice, Counter>* counters) {
  DCHECK(counters);
  const std::vector<std::string> lines = base::SplitString(
      output, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

  // Finds the chain line for an accounting chain first, and then parse the
  // following line(s) to get the counters for this chain. Repeats this process
  // until we reach the end of |output|.
  for (auto it = lines.cbegin(); it != lines.cend(); it++) {
    // Finds the chain name line.
    std::string direction, ifname;
    while (it != lines.cend() &&
           !RE2::FullMatch(*it, *kChainLine, &direction, &ifname))
      it++;

    if (it == lines.cend())
      break;

    // Skips this group if this ifname is not requested.
    if (!devices.empty() && devices.find(ifname) == devices.end())
      continue;

    // Skips the chain name line and the header line.
    if (lines.cend() - it <= 2) {
      LOG(ERROR) << "Invalid iptables output";
      return false;
    }
    it += 2;

    // The current line should be the accounting rule containing the counters.
    // Currently we only have one accounting rule (UNKNOWN source) for each
    // chain.
    // TODO(jiejiang): The following part will be extended to a loop when we
    // have more accounting rules.
    uint64_t pkts, bytes;
    if (!RE2::FullMatch(*it, *kCounterLine, &pkts, &bytes)) {
      LOG(ERROR) << "Cannot parse \"" << *it << "\"";
      return false;
    }

    TrafficCounter::Source source = TrafficCounter::UNKNOWN;
    auto& counter = (*counters)[std::make_pair(source, ifname)];
    if (direction == "rx") {
      counter.rx_packets += pkts;
      counter.rx_bytes += bytes;
    } else {
      counter.tx_packets += pkts;
      counter.tx_bytes += bytes;
    }
  }
  return true;
}

}  // namespace

Counter::Counter(uint64_t rx_bytes,
                 uint64_t rx_packets,
                 uint64_t tx_bytes,
                 uint64_t tx_packets)
    : rx_bytes(rx_bytes),
      rx_packets(rx_packets),
      tx_bytes(tx_bytes),
      tx_packets(tx_packets) {}

CountersService::CountersService(ShillClient* shill_client,
                                 Datapath* datapath,
                                 MinijailedProcessRunner* runner)
    : shill_client_(shill_client), datapath_(datapath), runner_(runner) {
  // Triggers the callback manually to make sure no device is missed.
  OnDeviceChanged(shill_client_->get_devices(), {});
  shill_client_->RegisterDevicesChangedHandler(base::BindRepeating(
      &CountersService::OnDeviceChanged, weak_factory_.GetWeakPtr()));
}

std::map<SourceDevice, Counter> CountersService::GetCounters(
    const std::set<std::string>& devices) {
  std::map<SourceDevice, Counter> counters;

  // Handles counters for IPv4 and IPv6 separately and returns failure if either
  // of the procession fails, since counters for only IPv4 or IPv6 are biased.
  std::string iptables_result;
  int ret = runner_->iptables(kMangleTable, {"-L", "-x", "-v", "-w"},
                              true /*log_failures*/, &iptables_result);
  if (ret != 0 || iptables_result.empty()) {
    LOG(ERROR) << "Failed to query IPv4 counters";
    return {};
  }
  if (!ParseOutput(iptables_result, devices, &counters)) {
    LOG(ERROR) << "Failed to parse IPv4 counters";
    return {};
  }

  std::string ip6tables_result;
  ret = runner_->ip6tables(kMangleTable, {"-L", "-x", "-v", "-w"},
                           true /*log_failures*/, &ip6tables_result);
  if (ret != 0 || ip6tables_result.empty()) {
    LOG(ERROR) << "Failed to query IPv6 counters";
    return {};
  }
  if (!ParseOutput(ip6tables_result, devices, &counters)) {
    LOG(ERROR) << "Failed to parse IPv6 counters";
    return {};
  }

  return counters;
}

void CountersService::OnDeviceChanged(const std::set<std::string>& added,
                                      const std::set<std::string>& removed) {
  for (const auto& ifname : added)
    SetupChainsAndRules(ifname);
}

bool CountersService::MakeAccountingChain(const std::string& chain_name) {
  return datapath_->ModifyChain(IpFamily::Dual, kMangleTable, "-N", chain_name,
                                false /*log_failures*/);
}

void CountersService::IptablesNewRule(std::vector<std::string> params) {
  DCHECK_GT(params.size(), 0);
  const std::string action = params[0];
  DCHECK(action == "-I" || action == "-A");
  params.emplace_back("-w");

  params[0] = "-C";
  if (runner_->iptables(kMangleTable, params, false /*log_failures*/) != 0) {
    params[0] = action;
    runner_->iptables(kMangleTable, params);
  }

  params[0] = "-C";
  if (runner_->ip6tables(kMangleTable, params, false /*log_failures*/) != 0) {
    params[0] = action;
    runner_->ip6tables(kMangleTable, params);
  }
}

void CountersService::SetupChainsAndRules(const std::string& ifname) {
  // For each device and traffic direction, we need to create:
  //  1) an accounting chain to jump to,
  //  2) jumping rules in mangle POSTROUTING for egress traffic, and in mangle
  //     INPUT and FORWARD for ingress traffic.
  //  3) source accounting rules in the chain.
  // Note that the length of a chain name must less than 29 chars and IFNAMSIZ
  // is 16 so we can only use at most 12 chars for the prefix.

  // Ingress traffic chain.
  const std::string ingress_chain = "rx_" + ifname;
  MakeAccountingChain(ingress_chain);
  IptablesNewRule({"-A", "FORWARD", "-i", ifname, "-j", ingress_chain});
  IptablesNewRule({"-A", "INPUT", "-i", ifname, "-j", ingress_chain});
  SetupAccountingRules(ingress_chain);

  // Egress traffic chain.
  const std::string egress_chain = "tx_" + ifname;
  MakeAccountingChain(egress_chain);
  IptablesNewRule({"-A", "POSTROUTING", "-o", ifname, "-j", egress_chain});
  SetupAccountingRules(egress_chain);
}

void CountersService::SetupAccountingRules(const std::string& chain_name) {
  // TODO(jiejiang): This function will be extended to matching on fwmark for
  // different sources.
  IptablesNewRule({"-A", chain_name});
}

}  // namespace patchpanel
