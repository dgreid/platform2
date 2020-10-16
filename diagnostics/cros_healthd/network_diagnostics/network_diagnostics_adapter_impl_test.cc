// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/run_loop.h>
#include <base/test/bind_test_util.h>
#include <base/test/task_environment.h>
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
  base::test::SingleThreadTaskEnvironment task_environment_;
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

// Test that the GatewayCanBePinged routine can be run.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunGatewayCanBePingedRoutine) {
  MockNetworkDiagnosticsRoutines network_diagnostics_routines;
  network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      network_diagnostics_routines.pending_remote());

  base::RunLoop run_loop;
  EXPECT_CALL(network_diagnostics_routines, GatewayCanBePinged(testing::_))
      .WillOnce(testing::Invoke(
          [&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                  GatewayCanBePingedCallback callback) {
            std::move(callback).Run(
                network_diagnostics_ipc::RoutineVerdict::kNoProblem, {});
          }));

  network_diagnostics_adapter()->RunGatewayCanBePingedRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<
                  network_diagnostics_ipc::GatewayCanBePingedProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNoProblem);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the HasSecureWiFiConnection routine can be run.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunHasSecureWiFiConnectionRoutine) {
  MockNetworkDiagnosticsRoutines network_diagnostics_routines;
  network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      network_diagnostics_routines.pending_remote());

  base::RunLoop run_loop;
  EXPECT_CALL(network_diagnostics_routines, HasSecureWiFiConnection(testing::_))
      .WillOnce(testing::Invoke(
          [&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                  HasSecureWiFiConnectionCallback callback) {
            std::move(callback).Run(
                network_diagnostics_ipc::RoutineVerdict::kNoProblem, {});
          }));

  network_diagnostics_adapter()->RunHasSecureWiFiConnectionRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<
                  network_diagnostics_ipc::HasSecureWiFiConnectionProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNoProblem);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the DnsResolverPresent routine can be run.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunDnsResolverPresentRoutine) {
  MockNetworkDiagnosticsRoutines network_diagnostics_routines;
  network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      network_diagnostics_routines.pending_remote());

  base::RunLoop run_loop;
  EXPECT_CALL(network_diagnostics_routines, DnsResolverPresent(testing::_))
      .WillOnce(testing::Invoke(
          [&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                  DnsResolverPresentCallback callback) {
            std::move(callback).Run(
                network_diagnostics_ipc::RoutineVerdict::kNoProblem, {});
          }));

  network_diagnostics_adapter()->RunDnsResolverPresentRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<
                  network_diagnostics_ipc::DnsResolverPresentProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNoProblem);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the DnsLatency routine can be run.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunDnsLatencyRoutine) {
  MockNetworkDiagnosticsRoutines network_diagnostics_routines;
  network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      network_diagnostics_routines.pending_remote());

  base::RunLoop run_loop;
  EXPECT_CALL(network_diagnostics_routines, DnsLatency(testing::_))
      .WillOnce(testing::Invoke(
          [&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                  DnsLatencyCallback callback) {
            std::move(callback).Run(
                network_diagnostics_ipc::RoutineVerdict::kNoProblem, {});
          }));

  network_diagnostics_adapter()->RunDnsLatencyRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::DnsLatencyProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNoProblem);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the DnsResolution routine can be run.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunDnsResolutionRoutine) {
  MockNetworkDiagnosticsRoutines network_diagnostics_routines;
  network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      network_diagnostics_routines.pending_remote());

  base::RunLoop run_loop;
  EXPECT_CALL(network_diagnostics_routines, DnsResolution(testing::_))
      .WillOnce(testing::Invoke(
          [&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                  DnsResolutionCallback callback) {
            std::move(callback).Run(
                network_diagnostics_ipc::RoutineVerdict::kNoProblem, {});
          }));

  network_diagnostics_adapter()->RunDnsResolutionRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::DnsResolutionProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNoProblem);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the CaptivePortal routine can be run.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunCaptivePortalRoutine) {
  MockNetworkDiagnosticsRoutines network_diagnostics_routines;
  network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      network_diagnostics_routines.pending_remote());

  base::RunLoop run_loop;
  EXPECT_CALL(network_diagnostics_routines, CaptivePortal(testing::_))
      .WillOnce(testing::Invoke(
          [&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                  CaptivePortalCallback callback) {
            std::move(callback).Run(
                network_diagnostics_ipc::RoutineVerdict::kNoProblem, {});
          }));

  network_diagnostics_adapter()->RunCaptivePortalRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::CaptivePortalProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNoProblem);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the HttpFirewall routine can be run.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunHttpFirewallRoutine) {
  MockNetworkDiagnosticsRoutines network_diagnostics_routines;
  network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      network_diagnostics_routines.pending_remote());

  base::RunLoop run_loop;
  EXPECT_CALL(network_diagnostics_routines, HttpFirewall(testing::_))
      .WillOnce(testing::Invoke(
          [&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                  HttpFirewallCallback callback) {
            std::move(callback).Run(
                network_diagnostics_ipc::RoutineVerdict::kNoProblem, {});
          }));

  network_diagnostics_adapter()->RunHttpFirewallRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::HttpFirewallProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNoProblem);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the LanConnectivity routine returns RoutineVerdict::kNotRun if a
// valid NetworkDiagnosticsRoutines remote was never sent.
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

// Test that the SignalStrength routine returns RoutineVerdict::kNotRun if a
// valid NetworkDiagnosticsRoutines remote was never sent.
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

// Test that the GatewayCanBePinged routine returns RoutineVerdict::kNotRun if a
// valid NetworkDiagnosticsRoutines remote was never sent.
TEST_F(NetworkDiagnosticsAdapterImplTest,
       RunGatewayCanBePingedRoutineWithNoRemote) {
  base::RunLoop run_loop;
  network_diagnostics_adapter()->RunGatewayCanBePingedRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<
                  network_diagnostics_ipc::GatewayCanBePingedProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNotRun);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the HasSecureWiFiConnection routine returns RoutineVerdict::kNotRun
// if a valid NetworkDiagnosticsRoutines remote was never sent.
TEST_F(NetworkDiagnosticsAdapterImplTest,
       RunHasSecureWiFiConnectionRoutineWithNoRemote) {
  base::RunLoop run_loop;
  network_diagnostics_adapter()->RunHasSecureWiFiConnectionRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<
                  network_diagnostics_ipc::HasSecureWiFiConnectionProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNotRun);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the DnsResolverPresent routine returns RoutineVerdict::kNotRun if a
// valid NetworkDiagnosticsRoutines remote was never sent.
TEST_F(NetworkDiagnosticsAdapterImplTest,
       RunDnsResolverPresentRoutineWithNoRemote) {
  base::RunLoop run_loop;
  network_diagnostics_adapter()->RunDnsResolverPresentRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<
                  network_diagnostics_ipc::DnsResolverPresentProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNotRun);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the DnsLatency routine returns RoutineVerdict::kNotRun if a valid
// NetworkDiagnosticsRoutines remote was never sent.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunDnsLatencyRoutineWithNoRemote) {
  base::RunLoop run_loop;
  network_diagnostics_adapter()->RunDnsLatencyRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::DnsLatencyProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNotRun);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the DnsResolution routine returns RoutineVerdict::kNotRun if a
// valid NetworkDiagnosticsRoutines remote was never sent.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunDnsResolutionRoutineWithNoRemote) {
  base::RunLoop run_loop;
  network_diagnostics_adapter()->RunDnsResolutionRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::DnsResolutionProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNotRun);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the CaptivePortal routine returns RoutineVerdict::kNotRun if a
// valid NetworkDiagnosticsRoutines remote was never sent.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunCaptivePortalRoutineWithNoRemote) {
  base::RunLoop run_loop;
  network_diagnostics_adapter()->RunCaptivePortalRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::CaptivePortalProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNotRun);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

// Test that the HttpFirewall routine returns RoutineVerdict::kNotRun if a valid
// NetworkDiagnosticsRoutines remote was never sent.
TEST_F(NetworkDiagnosticsAdapterImplTest, RunHttpFirewallRoutineWithNoRemote) {
  base::RunLoop run_loop;
  network_diagnostics_adapter()->RunHttpFirewallRoutine(
      base::BindLambdaForTesting(
          [&](network_diagnostics_ipc::RoutineVerdict response,
              const std::vector<network_diagnostics_ipc::HttpFirewallProblem>&
                  problems) {
            EXPECT_EQ(response,
                      network_diagnostics_ipc::RoutineVerdict::kNotRun);
            EXPECT_EQ(problems.size(), 0);
            run_loop.Quit();
          }));

  run_loop.Run();
}

}  // namespace diagnostics
