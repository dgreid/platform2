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
  void RunLanConnectivityRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          LanConnectivityCallback) override;
  void RunSignalStrengthRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          SignalStrengthCallback) override;
  void RunGatewayCanBePingedRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          GatewayCanBePingedCallback) override;
  void RunHasSecureWiFiConnectionRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          HasSecureWiFiConnectionCallback) override;
  void RunDnsResolverPresentRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          DnsResolverPresentCallback) override;
  void RunDnsLatencyRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          DnsLatencyCallback) override;
  void RunDnsResolutionRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          DnsResolutionCallback) override;
  void RunCaptivePortalRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          CaptivePortalCallback) override;
  void RunHttpFirewallRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          HttpFirewallCallback) override;
  void RunHttpsFirewallRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          HttpsFirewallCallback) override;
  void RunHttpsLatencyRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          HttpsLatencyCallback) override;

 private:
  // NetworkDiagnosticsRoutines remote used to run network diagnostics.
  // In production, this interface is implemented by the browser.
  mojo::Remote<chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
      network_diagnostics_routines_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_IMPL_H_
