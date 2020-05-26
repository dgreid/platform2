// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <libmems/test_fakes.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "iioservice/daemon/sensor_hal_server_impl.h"
#include "mojo/cros_sensor_service.mojom.h"

using ::testing::_;

namespace iioservice {

namespace {

class MockSensorServiceImpl final : public SensorServiceImpl {
 public:
  MockSensorServiceImpl(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      std::unique_ptr<libmems::IioContext> context)
      : SensorServiceImpl(std::move(ipc_task_runner), std::move(context)) {}

  void AddReceiver(
      mojo::PendingReceiver<cros::mojom::SensorService> request) override {
    DoAddReceiver(std::move(request));
  }
  MOCK_METHOD1(DoAddReceiver,
               void(mojo::PendingReceiver<cros::mojom::SensorService> request));
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
  void SetSensorService() override {
    std::unique_ptr<MockSensorServiceImpl,
                    decltype(&SensorServiceImpl::SensorServiceImplDeleter)>
        sensor_service(new MockSensorServiceImpl(
                           ipc_task_runner_,
                           std::make_unique<libmems::fakes::FakeIioContext>()),
                       SensorServiceImpl::SensorServiceImplDeleter);
    EXPECT_CALL(*sensor_service.get(), DoAddReceiver(_)).Times(1);
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
 public:
  SensorHalServerImplTest()
      : ipc_thread_(std::make_unique<base::Thread>("IPCThread")) {}

 protected:
  void SetUp() override {
    task_executor_ = std::make_unique<base::SingleThreadTaskExecutor>(
        base::MessagePumpType::IO);

    EXPECT_TRUE(ipc_thread_->Start());
    ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
        ipc_thread_->task_runner(),
        mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
  }

  MOCK_METHOD(void, DoMojoOnFailureCallback, ());
  void SetupServer() {
    base::RunLoop run_loop;

    ipc_thread_->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&SensorHalServerImplTest::CreateServerOnThread,
                       base::Unretained(this), run_loop.QuitClosure()));

    run_loop.Run();
  }
  void CreateServerOnThread(base::RepeatingClosure closure) {
    CHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

    server_ = FakeSensorHalServerImpl::Create(
        ipc_thread_->task_runner(), remote_.BindNewPipeAndPassReceiver(),
        base::BindOnce(&SensorHalServerImplTest::DoMojoOnFailureCallback,
                       base::Unretained(this)));

    closure.Run();
  }

  void TearDown() override {
    server_.reset();

    ipc_support_.reset();
    ipc_thread_->Stop();
    task_executor_.reset();
  }

  std::unique_ptr<base::Thread> ipc_thread_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  mojo::Remote<cros::mojom::SensorHalServer> remote_;

  std::unique_ptr<base::SingleThreadTaskExecutor> task_executor_;

  FakeSensorHalServerImpl::ScopedFakeSensorHalServerImpl server_ = {
      nullptr, SensorHalServerImpl::SensorHalServerImplDeleter};
};

TEST_F(SensorHalServerImplTest, CreateChannelAndDisconnect) {
  SetupServer();
  mojo::Remote<cros::mojom::SensorService> receiver;
  remote_->CreateChannel(receiver.BindNewPipeAndPassReceiver());

  EXPECT_CALL(*this, DoMojoOnFailureCallback).Times(1);
  remote_.reset();
}

}  // namespace

}  // namespace iioservice
