// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_H_

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "mojo/network_diagnostics.mojom.h"

namespace diagnostics {

// Interface which allows cros_healthd to access the browser's
// NetworkDiagnosticsRoutines interface.
class NetworkDiagnosticsAdapter {
 public:
  virtual ~NetworkDiagnosticsAdapter() = default;

  // Sets the NetworkDiagnosticsRoutines remote sent by the browser.
  virtual void SetNetworkDiagnosticsRoutines(
      mojo::PendingRemote<
          chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
          network_diagnostics_routines) = 0;

  // Requests that the browser invokes the LanConnectivity routine.
  virtual void RunLanConnectivityRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          LanConnectivityCallback) = 0;

  // Requests the browser to invoke the SignalStrength routine.
  virtual void RunSignalStrengthRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          SignalStrengthCallback) = 0;

  // Requests the browser to invoke the GatewayCanBePinged routine.
  virtual void RunGatewayCanBePingedRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          GatewayCanBePingedCallback) = 0;

  // Requests the browser to invoke the HasSecureWiFiConnection routine.
  virtual void RunHasSecureWiFiConnectionRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          HasSecureWiFiConnectionCallback) = 0;

  // Requests the browser to invoke the DnsResolverPresent routine.
  virtual void RunDnsResolverPresentRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          DnsResolverPresentCallback) = 0;

  // Requests the browser to invoke the DnsLatency routine.
  virtual void RunDnsLatencyRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          DnsLatencyCallback) = 0;

  // Requests the browser to invoke the DnsResolution routine.
  virtual void RunDnsResolutionRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          DnsResolutionCallback) = 0;

  // Requests the browser to invoke the CaptivePortal routine.
  virtual void RunCaptivePortalRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          CaptivePortalCallback) = 0;

  // Requests the browser to invoke the HttpFirewall routine.
  virtual void RunHttpFirewallRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          HttpFirewallCallback) = 0;

  // Requests the browser to invoke the HttpsFirewall routine.
  virtual void RunHttpsFirewallRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          HttpsFirewallCallback) = 0;

  // Requests the browser to invoke the HttpsLatency routine.
  virtual void RunHttpsLatencyRoutine(
      chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines::
          HttpsLatencyCallback) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_H_
