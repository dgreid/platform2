// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_NETWORK_FAKE_NETWORK_HEALTH_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_NETWORK_FAKE_NETWORK_HEALTH_ADAPTER_H_

#include "diagnostics/cros_healthd/network/network_health_adapter.h"
#include "mojo/network_health.mojom.h"

namespace diagnostics {

// Fake implementation of the NetworkHealthAdapter interface used for testing.
class FakeNetworkHealthAdapter final : public NetworkHealthAdapter {
 public:
  FakeNetworkHealthAdapter();
  FakeNetworkHealthAdapter(const FakeNetworkHealthAdapter&) = delete;
  FakeNetworkHealthAdapter& operator=(const FakeNetworkHealthAdapter&) = delete;
  ~FakeNetworkHealthAdapter() override;

  // NetworkHealthAdapterInterface overrides:
  void GetNetworkHealthState(FetchNetworkStateCallback callback) override;
  // Unimplemented. The fake implementation is not going to use the service
  // remote, so nothing needs to be done.
  void SetServiceRemote(
      mojo::PendingRemote<chromeos::network_health::mojom::NetworkHealthService>
          remote) override {}

  // Method to provide the canned response for the GetNetworkHealthState
  // request.
  void SetNetworkHealthStateResponse(
      chromeos::network_health::mojom::NetworkHealthStatePtr response);

  // Method to set if the internal NetworkHealthService remote is bound.
  void SetRemoteBound(bool bound);

 private:
  bool bound_;
  chromeos::network_health::mojom::NetworkHealthStatePtr network_health_state_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_NETWORK_FAKE_NETWORK_HEALTH_ADAPTER_H_
