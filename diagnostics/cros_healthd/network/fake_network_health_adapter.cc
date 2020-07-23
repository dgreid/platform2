// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/network/fake_network_health_adapter.h"

#include <utility>

#include <base/optional.h>

#include "mojo/network_health.mojom.h"

namespace {

using chromeos::network_health::mojom::NetworkHealthStatePtr;

}  // namespace

namespace diagnostics {

FakeNetworkHealthAdapter::FakeNetworkHealthAdapter() = default;
FakeNetworkHealthAdapter::~FakeNetworkHealthAdapter() = default;

void FakeNetworkHealthAdapter::GetNetworkHealthState(
    FetchNetworkStateCallback callback) {
  if (!bound_) {
    std::move(callback).Run(base::nullopt);
    return;
  }

  std::move(callback).Run(network_health_state_.Clone());
}

void FakeNetworkHealthAdapter::SetRemoteBound(bool bound) {
  bound_ = bound;
}

void FakeNetworkHealthAdapter::SetNetworkHealthStateResponse(
    NetworkHealthStatePtr network_health_state) {
  network_health_state_ = std::move(network_health_state);
}

}  // namespace diagnostics
