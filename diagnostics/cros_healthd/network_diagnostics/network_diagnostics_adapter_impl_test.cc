// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <base/test/bind_test_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter_impl.h"

namespace diagnostics {

namespace {

using testing::_;
using testing::Invoke;
using testing::WithArg;

namespace network_diagnostics_ipc = chromeos::network_diagnostics::mojom;

constexpr network_diagnostics_ipc::RoutineVerdict kNoProblem =
    network_diagnostics_ipc::RoutineVerdict::kNoProblem;

class MockNetworkDiagnosticsRoutines final
    : public network_diagnostics_ipc::NetworkDiagnosticsRoutines {
 public:
  MockNetworkDiagnosticsRoutines() : receiver_{this} {}
  MockNetworkDiagnosticsRoutines(const MockNetworkDiagnosticsRoutines&) =
      delete;
  MockNetworkDiagnosticsRoutines& operator=(
      const MockNetworkDiagnosticsRoutines&) = delete;

  MOCK_METHOD(void,
              LanConnectivity,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   LanConnectivityCallback),
              (override));
  MOCK_METHOD(void,
              SignalStrength,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   SignalStrengthCallback),
              (override));
  MOCK_METHOD(void,
              GatewayCanBePinged,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   GatewayCanBePingedCallback),
              (override));
  MOCK_METHOD(void,
              HasSecureWiFiConnection,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   HasSecureWiFiConnectionCallback),
              (override));
  MOCK_METHOD(void,
              DnsResolverPresent,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   DnsResolverPresentCallback),
              (override));
  MOCK_METHOD(
      void,
      DnsLatency,
      (network_diagnostics_ipc::NetworkDiagnosticsRoutines::DnsLatencyCallback),
      (override));
  MOCK_METHOD(void,
              DnsResolution,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   DnsResolutionCallback),
              (override));
  MOCK_METHOD(void,
              CaptivePortal,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   CaptivePortalCallback),
              (override));
  MOCK_METHOD(void,
              HttpFirewall,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   HttpFirewallCallback),
              (override));
  MOCK_METHOD(void,
              HttpsFirewall,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   HttpsFirewallCallback),
              (override));
  MOCK_METHOD(void,
              HttpsLatency,
              (network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                   HttpsLatencyCallback),
              (override));

  mojo::PendingRemote<network_diagnostics_ipc::NetworkDiagnosticsRoutines>
  pending_remote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

 private:
  mojo::Receiver<network_diagnostics_ipc::NetworkDiagnosticsRoutines> receiver_;
};

}  // namespace

class NetworkDiagnosticsAdapterImplTest : public testing::Test {
 protected:
  NetworkDiagnosticsAdapterImplTest() = default;
  NetworkDiagnosticsAdapterImplTest(const NetworkDiagnosticsAdapterImplTest&) =
      delete;
  NetworkDiagnosticsAdapterImplTest& operator=(
      const NetworkDiagnosticsAdapterImplTest&) = delete;

  NetworkDiagnosticsAdapterImpl* network_diagnostics_adapter() {
    return &network_diagnostics_adapter_;
  }

 private:
  base::MessageLoop message_loop_;
  NetworkDiagnosticsAdapterImpl network_diagnostics_adapter_;
};

// Test that the LanConnectivity routine can be run.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunLanConnectivityRoutine) {
  MockNetworkDiagnosticsRoutines network_diagnostics_routines;
  network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      network_diagnostics_routines.pending_remote());

  base::RunLoop run_loop;
  EXPECT_CALL(network_diagnostics_routines, LanConnectivity(_))
      .WillOnce(WithArg<0>(
          Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                         LanConnectivityCallback callback) {
            std::move(callback).Run(kNoProblem);
          })));

  network_diagnostics_adapter()->RunLanConnectivityRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response) {
            EXPECT_EQ(response, kNoProblem);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the SignalStrength routine can be run.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunSignalStrengthRoutine) {
  MockNetworkDiagnosticsRoutines network_diagnostics_routines;
  network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      network_diagnostics_routines.pending_remote());

  base::RunLoop run_loop;
  EXPECT_CALL(network_diagnostics_routines, SignalStrength(_))
      .WillOnce(WithArg<0>(
          Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                         SignalStrengthCallback callback) {
            std::move(callback).Run(kNoProblem, /*problems=*/{});
          })));

  network_diagnostics_adapter()->RunSignalStrengthRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::SignalStrengthProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNoProblem);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that RoutineVerdict::kNotRun is returned if a valid
// NetworkDiagnosticsRoutines remote was never sent.
TEST_F(NetworkDiagnosticsAdapterImplTest,
       RunLanConnectivityRoutineWithNoRemote) {
  base::RunLoop run_loop;
  network_diagnostics_adapter()->RunLanConnectivityRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict routine_verdict) {
            EXPECT_EQ(routine_verdict,
                      network_diagnostics_ipc::RoutineVerdict::kNotRun);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that RoutineVerdict::kNotRun is returned if a valid
// NetworkDiagnosticsRoutines remote was never sent.
TEST_F(NetworkDiagnosticsAdapterImplTest,
       RunSignalStrengthRoutineWithNoRemote) {
  base::RunLoop run_loop;
  network_diagnostics_adapter()->RunSignalStrengthRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::SignalStrengthProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNotRun);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

}  // namespace diagnostics
