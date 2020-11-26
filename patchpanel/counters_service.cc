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

namespace patchpanel {

namespace {

using Counter = CountersService::Counter;
using SourceDevice = CountersService::SourceDevice;

constexpr char kMangleTable[] = "mangle";
constexpr char kVpnChainTag[] = "vpn";
constexpr char kRxTag[] = "rx_";
constexpr char kTxTag[] = "tx_";

// The following regexs and code is written and tested for iptables v1.6.2.
// Output code of iptables can be found at:
//   https://git.netfilter.org/iptables/tree/iptables/iptables.c?h=v1.6.2

// The chain line looks like:
//   "Chain tx_eth0 (2 references)".
// This regex extracts "tx" (direction), "eth0" (ifname) from this example.
constexpr LazyRE2 kChainLine = {R"(Chain (rx|tx)_(\w+).*)"};

// The counter line looks like (some spaces are deleted to make it fit in one
// line):
//   "    5374 876172 all -- any any anywhere anywhere mark match 0x2000/0x3f00"
// The first two counters are captured for pkts and bytes.
constexpr LazyRE2 kCounterLine = {R"( *(\d+) +(\d+).*mark match (.*)/0x3f00)"};

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

    // The next block of lines are the counters lines for individual sources.
    for (; it != lines.cend() && !it->empty(); it++) {
      uint64_t pkts, bytes;
      Fwmark mark;
      if (!RE2::FullMatch(*it, *kCounterLine, &pkts, &bytes,
                          RE2::Hex(&mark.fwmark))) {
        LOG(ERROR) << "Cannot parse counter line \"" << *it << "\"";
        return false;
      }

      if (pkts == 0 && bytes == 0)
        continue;

      TrafficCounter::Source source = TrafficSourceToProto(mark.Source());
      auto& counter = (*counters)[std::make_pair(source, ifname)];
      if (direction == "rx") {
        counter.rx_packets += pkts;
        counter.rx_bytes += bytes;
      } else {
        counter.tx_packets += pkts;
        counter.tx_bytes += bytes;
      }
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

CountersService::CountersService(Datapath* datapath,
                                 MinijailedProcessRunner* runner)
    : datapath_(datapath), runner_(runner) {}

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

void CountersService::Init(const std::set<std::string>& devices) {
  SetupAccountingRules(kVpnChainTag);
  for (const auto& device : devices) {
    OnPhysicalDeviceAdded(device);
  }
}

void CountersService::OnPhysicalDeviceAdded(const std::string& ifname) {
  if (SetupAccountingRules(ifname))
    SetupJumpRules("-A", ifname, ifname);
}

void CountersService::OnPhysicalDeviceRemoved(const std::string& ifname) {
  SetupJumpRules("-D", ifname, ifname);
}

void CountersService::OnVpnDeviceAdded(const std::string& ifname) {
  SetupJumpRules("-A", ifname, kVpnChainTag);
}

void CountersService::OnVpnDeviceRemoved(const std::string& ifname) {
  SetupJumpRules("-D", ifname, kVpnChainTag);
}

bool CountersService::MakeAccountingChain(const std::string& chain_name) {
  return datapath_->ModifyChain(IpFamily::Dual, kMangleTable, "-N", chain_name,
                                false /*log_failures*/);
}

bool CountersService::AddAccountingRule(const std::string& chain_name,
                                        TrafficSource source) {
  std::vector<std::string> args = {"-A",
                                   chain_name,
                                   "-m",
                                   "mark",
                                   "--mark",
                                   Fwmark::FromSource(source).ToString() + "/" +
                                       kFwmarkAllSourcesMask.ToString(),
                                   "-j",
                                   "RETURN",
                                   "-w"};
  return datapath_->ModifyIptables(IpFamily::Dual, kMangleTable, args);
}

bool CountersService::SetupAccountingRules(const std::string& chain_tag) {
  // For a new target accounting chain, create
  //  1) an accounting chain to jump to,
  //  2) source accounting rules in the chain.
  // Note that the length of a chain name must less than 29 chars and IFNAMSIZ
  // is 16 so we can only use at most 12 chars for the prefix.
  const std::string ingress_chain = kRxTag + chain_tag;
  const std::string egress_chain = kTxTag + chain_tag;

  // Creates egress and ingress traffic chains, or stops if they already exist.
  if (!MakeAccountingChain(egress_chain) ||
      !MakeAccountingChain(ingress_chain)) {
    LOG(INFO) << "Traffic accounting chains already exist for " << chain_tag;
    return false;
  }

  // Add source accounting rules.
  for (TrafficSource source : kAllSources) {
    AddAccountingRule(ingress_chain, source);
    AddAccountingRule(egress_chain, source);
  }
  // TODO(b/160112868): add default rules for counting any traffic left as
  // UNKNOWN.

  return true;
}

void CountersService::SetupJumpRules(const std::string& op,
                                     const std::string& ifname,
                                     const std::string& chain_tag) {
  // For each device create a jumping rule in mangle POSTROUTING for egress
  // traffic, and two jumping rules in mangle INPUT and FORWARD for ingress
  // traffic.
  datapath_->ModifyIptables(
      IpFamily::Dual, kMangleTable,
      {"-A", "FORWARD", "-i", ifname, "-j", kRxTag + chain_tag, "-w"});
  datapath_->ModifyIptables(
      IpFamily::Dual, kMangleTable,
      {"-A", "INPUT", "-i", ifname, "-j", kRxTag + chain_tag, "-w"});
  datapath_->ModifyIptables(
      IpFamily::Dual, kMangleTable,
      {"-A", "POSTROUTING", "-o", ifname, "-j", kTxTag + chain_tag, "-w"});
}

TrafficCounter::Source TrafficSourceToProto(TrafficSource source) {
  switch (source) {
    case CHROME:
      return TrafficCounter::CHROME;
    case USER:
      return TrafficCounter::USER;
    case UPDATE_ENGINE:
      return TrafficCounter::UPDATE_ENGINE;
    case SYSTEM:
      return TrafficCounter::SYSTEM;
    case HOST_VPN:
      return TrafficCounter::VPN;
    case ARC:
      return TrafficCounter::ARC;
    case CROSVM:
      return TrafficCounter::CROSVM;
    case PLUGINVM:
      return TrafficCounter::PLUGINVM;
    case TETHER_DOWNSTREAM:
      return TrafficCounter::SYSTEM;
    case ARC_VPN:
      return TrafficCounter::VPN;
    case UNKNOWN:
    default:
      return TrafficCounter::UNKNOWN;
  }
}

}  // namespace patchpanel
