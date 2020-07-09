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
                                      const std::set<std::string>& removed) {}

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

}  // namespace patchpanel
