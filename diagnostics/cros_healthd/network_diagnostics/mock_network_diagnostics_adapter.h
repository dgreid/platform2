// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_MOCK_NETWORK_DIAGNOSTICS_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_MOCK_NETWORK_DIAGNOSTICS_ADAPTER_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter.h"

namespace diagnostics {

// Mock implementation of the NetworkDiagnosticsAdapter interface.
class MockNetworkDiagnosticsAdapter final : public NetworkDiagnosticsAdapter {
 public:
  MockNetworkDiagnosticsAdapter();
  MockNetworkDiagnosticsAdapter(const MockNetworkDiagnosticsAdapter&) = delete;
  MockNetworkDiagnosticsAdapter& operator=(
      const MockNetworkDiagnosticsAdapter&) = delete;
  ~MockNetworkDiagnosticsAdapter() override;

  MOCK_METHOD(
      void,
      SetNetworkDiagnosticsRoutines,
      (mojo::PendingRemote<
          chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>),
      (override));
  MOCK_METHOD(void,
              RunLanConnectivityRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::LanConnectivityCallback),
              (override));
  MOCK_METHOD(void,
              RunSignalStrengthRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::SignalStrengthCallback),
              (override));
  MOCK_METHOD(void,
              RunGatewayCanBePingedRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::GatewayCanBePingedCallback),
              (override));
  MOCK_METHOD(void,
              RunHasSecureWiFiConnectionRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::HasSecureWiFiConnectionCallback),
              (override));
  MOCK_METHOD(void,
              RunDnsResolverPresentRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::DnsResolverPresentCallback),
              (override));
  MOCK_METHOD(void,
              RunDnsLatencyRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::DnsLatencyCallback),
              (override));
  MOCK_METHOD(void,
              RunDnsResolutionRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::DnsResolutionCallback),
              (override));
  MOCK_METHOD(void,
              RunCaptivePortalRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::CaptivePortalCallback),
              (override));
  MOCK_METHOD(void,
              RunHttpFirewallRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::HttpFirewallCallback),
              (override));
  MOCK_METHOD(void,
              RunHttpsFirewallRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::HttpsFirewallCallback),
              (override));
  MOCK_METHOD(void,
              RunHttpsLatencyRoutine,
              (chromeos::network_diagnostics::mojom::
                   NetworkDiagnosticsRoutines::HttpsLatencyCallback),
              (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_MOCK_NETWORK_DIAGNOSTICS_ADAPTER_H_
