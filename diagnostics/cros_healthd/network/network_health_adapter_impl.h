// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_NETWORK_NETWORK_HEALTH_ADAPTER_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_NETWORK_NETWORK_HEALTH_ADAPTER_IMPL_H_

#include <base/memory/weak_ptr.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/network/network_health_adapter.h"
#include "mojo/network_health.mojom.h"

namespace diagnostics {

// Production implementation of the NetworkHealthAdapter.
class NetworkHealthAdapterImpl final : public NetworkHealthAdapter {
 public:
  NetworkHealthAdapterImpl();
  NetworkHealthAdapterImpl(const NetworkHealthAdapterImpl&) = delete;
  NetworkHealthAdapterImpl& operator=(const NetworkHealthAdapterImpl&) = delete;
  ~NetworkHealthAdapterImpl() override;

  // NetworkHealthAdapterInterface overrides:
  void GetNetworkHealthState(FetchNetworkStateCallback callback) override;
  void SetServiceRemote(
      mojo::PendingRemote<chromeos::network_health::mojom::NetworkHealthService>
          remote) override;

 private:
  mojo::Remote<chromeos::network_health::mojom::NetworkHealthService>
      network_health_remote_;

  // Must be the last member of the class.
  base::WeakPtrFactory<NetworkHealthAdapterImpl> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_NETWORK_NETWORK_HEALTH_ADAPTER_IMPL_H_
