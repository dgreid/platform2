// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/run_loop.h>
#include <base/optional.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/fetchers/network_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_probe.mojom.h"
#include "mojo/network_health.mojom.h"

namespace diagnostics {

namespace {

// Saves |response| to |response_destination|.
void OnGetNetworkInfoReceived(
    chromeos::cros_healthd::mojom::NetworkResultPtr* response_destination,
    base::Closure quit_closure,
    chromeos::cros_healthd::mojom::NetworkResultPtr response) {
  *response_destination = std::move(response);
  quit_closure.Run();
}

}  // namespace

class NetworkFetcherTest : public testing::Test {
 protected:
  NetworkFetcherTest() = default;

  void SetUp() override { ASSERT_TRUE(mock_context_.Initialize()); }

  chromeos::cros_healthd::mojom::NetworkResultPtr FetchNetworkInfo() {
    base::RunLoop run_loop;
    chromeos::cros_healthd::mojom::NetworkResultPtr result;
    network_fetcher_.FetchNetworkInfo(base::BindOnce(
        &OnGetNetworkInfoReceived, &result, run_loop.QuitClosure()));

    run_loop.Run();
    return result;
  }

  FakeNetworkHealthAdapter* network_adapter() {
    return mock_context_.fake_network_health_adapter();
  }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  NetworkFetcher network_fetcher_{&mock_context_};
};

// Test an appropriate error is returned if no remote is bound;
TEST_F(NetworkFetcherTest, NoRemote) {
  network_adapter()->SetRemoteBound(false);
  auto result = FetchNetworkInfo();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kServiceUnavailable);
}

// Test that if the remote is bound, the NetworkHealthState is returned.
TEST_F(NetworkFetcherTest, GetNetworkHealthState) {
  auto network = chromeos::network_health::mojom::Network::New();
  network->name = "My WiFi";
  network->type = chromeos::network_config::mojom::NetworkType::kWiFi;
  network->state = chromeos::network_health::mojom::NetworkState::kOnline;
  network->signal_strength =
      chromeos::network_health::mojom::UInt32Value::New(70);
  network->mac_address = "00:11:22:33:44:55";

  auto network_health_state =
      chromeos::network_health::mojom::NetworkHealthState::New();
  network_health_state->networks.push_back(network.Clone());

  network_adapter()->SetRemoteBound(true);
  network_adapter()->SetNetworkHealthStateResponse(
      std::move(network_health_state));
  auto result = FetchNetworkInfo();
  ASSERT_TRUE(result->is_network_health());
  EXPECT_EQ(result->get_network_health()->networks.size(), 1);
  EXPECT_EQ(result->get_network_health()->networks[0], network);
}

}  // namespace diagnostics
