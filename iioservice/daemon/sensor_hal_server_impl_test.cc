// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <libmems/test_fakes.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "iioservice/daemon/sensor_hal_server_impl.h"
#include "mojo/cros_sensor_service.mojom.h"

namespace iioservice {

namespace {

class FakeSensorServiceImpl final : public SensorServiceImpl {
 public:
  FakeSensorServiceImpl(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      std::unique_ptr<libmems::IioContext> context)
      : SensorServiceImpl(
            std::move(ipc_task_runner),
            std::move(context),
            nullptr,
            SensorDeviceImpl::ScopedSensorDeviceImpl(
                nullptr, SensorDeviceImpl::SensorDeviceImplDeleter)) {}

  ~FakeSensorServiceImpl() {
    // Expect |AddReceiver| is called exactly once.
    EXPECT_TRUE(receiver_.is_bound());
  }

  // SensorServiceImpl overrides:
  void AddReceiver(
      mojo::PendingReceiver<cros::mojom::SensorService> request) override {
    CHECK(!receiver_.is_bound());

    receiver_.Bind(std::move(request));
  }

 private:
  mojo::Receiver<cros::mojom::SensorService> receiver_{this};
};

class FakeSensorHalServerImpl final : public SensorHalServerImpl {
 public:
  using ScopedFakeSensorHalServerImpl =
      std::unique_ptr<FakeSensorHalServerImpl,
                      decltype(&SensorHalServerImplDeleter)>;

  static ScopedFakeSensorHalServerImpl Create(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      mojo::PendingReceiver<cros::mojom::SensorHalServer> server_receiver,
      MojoOnFailureCallback mojo_on_failure_callback) {
    ScopedFakeSensorHalServerImpl server(
        new FakeSensorHalServerImpl(std::move(ipc_task_runner),
                                    std::move(server_receiver),
                                    std::move(mojo_on_failure_callback)),
        SensorHalServerImplDeleter);

    server->SetSensorService();

    return server;
  }

 protected:
  // SensorHalServerImpl overrides:
  void SetSensorService() override {
    std::unique_ptr<FakeSensorServiceImpl,
                    decltype(&SensorServiceImpl::SensorServiceImplDeleter)>
        sensor_service(new FakeSensorServiceImpl(
                           ipc_task_runner_,
                           std::make_unique<libmems::fakes::FakeIioContext>()),
                       SensorServiceImpl::SensorServiceImplDeleter);
    sensor_service_ = std::move(sensor_service);
  }

 private:
  FakeSensorHalServerImpl(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      mojo::PendingReceiver<cros::mojom::SensorHalServer> server_receiver,
      MojoOnFailureCallback mojo_on_failure_callback)
      : SensorHalServerImpl(std::move(ipc_task_runner),
                            std::move(server_receiver),
                            std::move(mojo_on_failure_callback)) {}
};

class SensorHalServerImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
        task_environment_.GetMainThreadTaskRunner(),
        mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  FakeSensorHalServerImpl::ScopedFakeSensorHalServerImpl server_ = {
      nullptr, SensorHalServerImpl::SensorHalServerImplDeleter};
};

TEST_F(SensorHalServerImplTest, CreateChannelAndDisconnect) {
  base::RunLoop loop;

  mojo::Remote<cros::mojom::SensorHalServer> remote;
  server_ = FakeSensorHalServerImpl::Create(
      task_environment_.GetMainThreadTaskRunner(),
      remote.BindNewPipeAndPassReceiver(),
      base::BindOnce([](base::Closure closure) { closure.Run(); },
                     loop.QuitClosure()));

  mojo::Remote<cros::mojom::SensorService> sensor_service_remote;
  remote->CreateChannel(sensor_service_remote.BindNewPipeAndPassReceiver());
  remote.reset();

  // Run until the remote is disconnected;
  loop.Run();
}

}  // namespace

}  // namespace iioservice
