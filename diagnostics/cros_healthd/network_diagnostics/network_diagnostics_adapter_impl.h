// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_IMPL_H_

#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter.h"

namespace diagnostics {

// Production implementation of the NetworkDiagnosticsAdapter interface.
class NetworkDiagnosticsAdapterImpl final : public NetworkDiagnosticsAdapter {
 public:
  NetworkDiagnosticsAdapterImpl();
  NetworkDiagnosticsAdapterImpl(const NetworkDiagnosticsAdapterImpl&) = delete;
  NetworkDiagnosticsAdapterImpl& operator=(
      const NetworkDiagnosticsAdapterImpl&) = delete;
  ~NetworkDiagnosticsAdapterImpl() override;

  // NetworkDiagnosticsAdapter overrides:
  void SetNetworkDiagnosticsRoutines(
      mojo::PendingRemote<
          chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
          network_diagnostics_routines) override;
  void RunLanConnectivityRoutine(MojomLanConnectivityCallback) override;

 private:
  // NetworkDiagnosticsRoutines remote used to run network diagnostics.
  // In production, this interface is implemented by the browser.
  mojo::Remote<chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
      network_diagnostics_routines_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_IMPL_H_
