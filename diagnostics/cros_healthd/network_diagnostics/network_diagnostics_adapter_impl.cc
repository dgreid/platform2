// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter_impl.h"

#include <utility>

namespace diagnostics {

namespace {

namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

}  // namespace

NetworkDiagnosticsAdapterImpl::NetworkDiagnosticsAdapterImpl() = default;
NetworkDiagnosticsAdapterImpl::~NetworkDiagnosticsAdapterImpl() = default;

void NetworkDiagnosticsAdapterImpl::SetNetworkDiagnosticsRoutines(
    mojo::PendingRemote<network_diagnostics_ipc::NetworkDiagnosticsRoutines>
        network_diagnostics_routines) {
  network_diagnostics_routines_.Bind(std::move(network_diagnostics_routines));
}

void NetworkDiagnosticsAdapterImpl::RunLanConnectivityRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::LanConnectivityCallback
        callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    std::move(callback).Run(
        chromeos::network_diagnostics::mojom::RoutineVerdict::kNotRun);
    return;
  }
  network_diagnostics_routines_->LanConnectivity(std::move(callback));
}

void NetworkDiagnosticsAdapterImpl::RunSignalStrengthRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::SignalStrengthCallback
        callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    std::move(callback).Run(network_diagnostics_ipc::RoutineVerdict::kNotRun,
                            /*problems=*/{});
    return;
  }
  network_diagnostics_routines_->SignalStrength(std::move(callback));
}

void NetworkDiagnosticsAdapterImpl::RunGatewayCanBePingedRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        GatewayCanBePingedCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    std::move(callback).Run(
        chromeos::network_diagnostics::mojom::RoutineVerdict::kNotRun,
        /*problems=*/{});
    return;
  }
  network_diagnostics_routines_->GatewayCanBePinged(std::move(callback));
}

void NetworkDiagnosticsAdapterImpl::RunHasSecureWiFiConnectionRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        HasSecureWiFiConnectionCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    std::move(callback).Run(
        chromeos::network_diagnostics::mojom::RoutineVerdict::kNotRun,
        /*problems=*/{});
    return;
  }
  network_diagnostics_routines_->HasSecureWiFiConnection(std::move(callback));
}

void NetworkDiagnosticsAdapterImpl::RunDnsResolverPresentRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::
        DnsResolverPresentCallback callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    std::move(callback).Run(
        chromeos::network_diagnostics::mojom::RoutineVerdict::kNotRun,
        /*problems=*/{});
    return;
  }
  network_diagnostics_routines_->DnsResolverPresent(std::move(callback));
}

void NetworkDiagnosticsAdapterImpl::RunDnsLatencyRoutine(
    network_diagnostics_ipc::NetworkDiagnosticsRoutines::DnsLatencyCallback
        callback) {
  if (!network_diagnostics_routines_.is_bound()) {
    std::move(callback).Run(
        chromeos::network_diagnostics::mojom::RoutineVerdict::kNotRun,
        /*problems=*/{});
    return;
  }
  network_diagnostics_routines_->DnsLatency(std::move(callback));
}

}  // namespace diagnostics
