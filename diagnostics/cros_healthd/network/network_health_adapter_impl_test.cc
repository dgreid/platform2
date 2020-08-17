// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/callback.h>
#include <base/optional.h>
#include <base/run_loop.h>
#include <base/test/bind_test_util.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>

#include "diagnostics/cros_healthd/network/network_health_adapter_impl.h"
#include "mojo/network_health.mojom.h"

namespace diagnostics {

namespace {

namespace network_health_ipc = chromeos::network_health::mojom;

class MockNetworkHealthService
    : public network_health_ipc::NetworkHealthService {
 public:
  MockNetworkHealthService() : receiver_{this} {}
  MockNetworkHealthService(const MockNetworkHealthService&) = delete;
  MockNetworkHealthService& operator=(const MockNetworkHealthService&) = delete;

  MOCK_METHOD(void,
              GetNetworkList,
              (NetworkHealthService::GetNetworkListCallback),
              (override));
  MOCK_METHOD(void,
              GetHealthSnapshot,
              (NetworkHealthService::GetHealthSnapshotCallback),
              (override));

  mojo::PendingRemote<network_health_ipc::NetworkHealthService>
  pending_remote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

 private:
  mojo::Receiver<NetworkHealthService> receiver_;
};

}  // namespace

class NetworkHealthAdapterImplTest : public testing::Test {
 protected:
  NetworkHealthAdapterImplTest() { mojo::core::Init(); }

  NetworkHealthAdapterImpl* network_health_adapter() {
    return &network_health_adapter_;
  }

 private:
  base::test::TaskEnvironment task_environment_;
  NetworkHealthAdapterImpl network_health_adapter_;
};

// Test that the NetworkHealthAdapterImpl can set the NetworkHealthService
// remote and request the NetworkHealthState.
TEST_F(NetworkHealthAdapterImplTest, RequestNetworkHealthState) {
  MockNetworkHealthService service;
  network_health_adapter()->SetServiceRemote(service.pending_remote());

  base::RunLoop run_loop;
  auto canned_response = network_health_ipc::NetworkHealthState::New();
  EXPECT_CALL(service, GetHealthSnapshot(testing::_))
      .WillOnce(testing::Invoke([&](network_health_ipc::NetworkHealthService::
                                        GetHealthSnapshotCallback callback) {
        std::move(callback).Run(canned_response.Clone());
      }));

  network_health_adapter()->GetNetworkHealthState(base::BindLambdaForTesting(
      [&](base::Optional<network_health_ipc::NetworkHealthStatePtr> response) {
        ASSERT_TRUE(response.has_value());
        EXPECT_EQ(canned_response, response);
        run_loop.Quit();
      }));

  run_loop.Run();
}

// Test a base::nullopt is returned if no remote is bound;
TEST_F(NetworkHealthAdapterImplTest, NoRemote) {
  base::RunLoop run_loop;
  network_health_adapter()->GetNetworkHealthState(base::BindLambdaForTesting(
      [&](base::Optional<network_health_ipc::NetworkHealthStatePtr> response) {
        EXPECT_FALSE(response.has_value());
        run_loop.Quit();
      }));

  run_loop.Run();
}

}  // namespace diagnostics
