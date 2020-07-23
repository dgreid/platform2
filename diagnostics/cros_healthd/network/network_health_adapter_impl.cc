// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/network/network_health_adapter_impl.h"

#include <utility>

#include <base/callback.h>
#include <base/optional.h>

#include "mojo/network_health.mojom.h"

namespace diagnostics {

namespace {

namespace network_health_ipc = chromeos::network_health::mojom;

// Forwards the response from the network health remote to |callback|.
void OnNetworkHealthStateReceived(
    base::OnceCallback<void(
        base::Optional<network_health_ipc::NetworkHealthStatePtr>)> callback,
    network_health_ipc::NetworkHealthStatePtr response) {
  std::move(callback).Run(std::move(response));
}

}  // namespace

NetworkHealthAdapterImpl::NetworkHealthAdapterImpl() = default;
NetworkHealthAdapterImpl::~NetworkHealthAdapterImpl() = default;

void NetworkHealthAdapterImpl::GetNetworkHealthState(
    FetchNetworkStateCallback callback) {
  if (!network_health_remote_.is_bound()) {
    std::move(callback).Run(base::nullopt);
    return;
  }

  network_health_remote_->GetHealthSnapshot(
      base::BindOnce(&OnNetworkHealthStateReceived, std::move(callback)));
}

void NetworkHealthAdapterImpl::SetServiceRemote(
    mojo::PendingRemote<network_health_ipc::NetworkHealthService> remote) {
  if (network_health_remote_.is_bound())
    network_health_remote_.reset();
  network_health_remote_.Bind(std::move(remote));
}

}  // namespace diagnostics
