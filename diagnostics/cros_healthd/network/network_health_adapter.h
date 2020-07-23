// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_NETWORK_NETWORK_HEALTH_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_NETWORK_NETWORK_HEALTH_ADAPTER_H_

#include <base/callback_forward.h>
#include <base/optional.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

#include "mojo/network_health.mojom-forward.h"

namespace diagnostics {

// Interface for interacting with the NetworkHealthService in Chrome.
class NetworkHealthAdapter {
 public:
  using FetchNetworkStateCallback = base::OnceCallback<void(
      base::Optional<chromeos::network_health::mojom::NetworkHealthStatePtr>)>;
  virtual ~NetworkHealthAdapter() = default;

  // Request to get the NetworkHealthState snapshot. Will return the
  // NetworkHealthState if the remote is bound, or base::nullopt if the remote
  // is not bound through the callback.
  virtual void GetNetworkHealthState(FetchNetworkStateCallback callback) = 0;

  // Method that sets the internal NetworkHealthService remote.
  virtual void SetServiceRemote(
      mojo::PendingRemote<chromeos::network_health::mojom::NetworkHealthService>
          remote) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_NETWORK_NETWORK_HEALTH_ADAPTER_H_
