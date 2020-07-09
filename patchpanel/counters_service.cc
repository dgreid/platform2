// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/counters_service.h"

#include <set>
#include <string>
#include <vector>

namespace patchpanel {

namespace {

constexpr char kMangleTable[] = "mangle";

}  // namespace

CountersService::CountersService(ShillClient* shill_client,
                                 MinijailedProcessRunner* runner)
    : shill_client_(shill_client), runner_(runner) {
  // Triggers the callback manually to make sure no device is missed.
  OnDeviceChanged(shill_client_->get_devices(), {});
  shill_client_->RegisterDevicesChangedHandler(base::BindRepeating(
      &CountersService::OnDeviceChanged, weak_factory_.GetWeakPtr()));
}

void CountersService::OnDeviceChanged(const std::set<std::string>& added,
                                      const std::set<std::string>& removed) {
  for (const auto& ifname : added)
    SetupChainsAndRules(ifname);
}

void CountersService::IptablesNewChain(const std::string& chain_name) {
  // There is no straightforward way to check if a chain exists or not.
  runner_->iptables(kMangleTable, {"-N", chain_name, "-w"},
                    false /*log_failures*/);
  runner_->ip6tables(kMangleTable, {"-N", chain_name, "-w"},
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
  // For each group, we need to create 1) an accounting chain, 2) a jumping rule
  // matching |ifname|, and 3) accounting rule(s) in the chain.
  // Note that the length of a chain name must less than 29 chars and IFNAMSIZ
  // is 16 so we can only use at most 12 chars for the prefix.

  // Egress traffic in FORWARD chain. Only traffic for interface-type sources
  // will be counted by these rules.
  const std::string egress_forward_chain = "tx_fwd_" + ifname;
  IptablesNewChain(egress_forward_chain);
  IptablesNewRule({"-A", "FORWARD", "-o", ifname, "-j", egress_forward_chain});
  SetupAccountingRules(egress_forward_chain);

  // Egress traffic in POSTROUTING chain. Only traffic for host-type sources
  // will be counted by these rules, by having a "-m owner --socket-exists" in
  // the jumping rule. Traffic via "FORWARD -> POSTROUTING" does not have a
  // socket so will only be counted in FORWARD, while traffic from OUTPUT will
  // always have an associated socket.
  const std::string egress_postrouting_chain = "tx_postrt_" + ifname;
  IptablesNewChain(egress_postrouting_chain);
  IptablesNewRule({"-A", "POSTROUTING", "-o", ifname, "-m", "owner",
                   "--socket-exists", "-j", egress_postrouting_chain});
  SetupAccountingRules(egress_postrouting_chain);

  // Ingress traffic in FORWARD chain. Only traffic for interface-type sources
  // will be counted by these rules.
  const std::string ingress_forward_chain = "rx_fwd_" + ifname;
  IptablesNewChain(ingress_forward_chain);
  IptablesNewRule({"-A", "FORWARD", "-i", ifname, "-j", ingress_forward_chain});
  SetupAccountingRules(ingress_forward_chain);

  // Ingress traffic in INPUT chain. Only traffic for host-type sources will be
  // counted by these rules.
  const std::string ingress_input_chain = "rx_input_" + ifname;
  IptablesNewChain(ingress_input_chain);
  IptablesNewRule({"-A", "INPUT", "-i", ifname, "-j", ingress_input_chain});
  SetupAccountingRules(ingress_input_chain);
}

void CountersService::SetupAccountingRules(const std::string& chain_name) {
  // TODO(jiejiang): This function will be extended to matching on fwmark for
  // different sources.
  IptablesNewRule({"-A", chain_name});
}

}  // namespace patchpanel
