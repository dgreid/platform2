// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/sensor_hal_server_impl.h"

#include <memory>
#include <utility>

#include <libmems/iio_channel_impl.h>
#include <libmems/iio_context_impl.h>
#include <libmems/iio_device_impl.h>

#include "iioservice/include/common.h"
#include "iioservice/include/constants.h"

namespace iioservice {

// static
void SensorHalServerImpl::SensorHalServerImplDeleter(
    SensorHalServerImpl* server) {
  if (server == nullptr)
    return;

  if (!server->ipc_task_runner_->RunsTasksInCurrentSequence()) {
    server->ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SensorHalServerImpl::SensorHalServerImplDeleter,
                       server));
    return;
  }

  delete server;
}

// static
SensorHalServerImpl::ScopedSensorHalServerImpl SensorHalServerImpl::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    mojo::PendingReceiver<cros::mojom::SensorHalServer> server_receiver,
    MojoOnFailureCallback mojo_on_failure_callback) {
  DCHECK(ipc_task_runner->RunsTasksInCurrentSequence());

  ScopedSensorHalServerImpl server(
      new SensorHalServerImpl(std::move(ipc_task_runner),
                              std::move(server_receiver),
                              std::move(mojo_on_failure_callback)),
      SensorHalServerImplDeleter);

  server->SetSensorService();

  return server;
}

void SensorHalServerImpl::CreateChannel(
    mojo::PendingReceiver<cros::mojom::SensorService> sensor_service_request) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(sensor_service_);

  LOGF(INFO) << "Received SensorService from sensor HAL dispatcher";

  sensor_service_->AddReceiver(std::move(sensor_service_request));
}

SensorHalServerImpl::SensorHalServerImpl(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    mojo::PendingReceiver<cros::mojom::SensorHalServer> server_receiver,
    MojoOnFailureCallback mojo_on_failure_callback)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      receiver_(this),
      mojo_on_failure_callback_(std::move(mojo_on_failure_callback)) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  receiver_.Bind(std::move(server_receiver));

  receiver_.set_disconnect_handler(
      base::BindOnce(&SensorHalServerImpl::OnSensorHalServerError,
                     weak_factory_.GetWeakPtr()));
}

void SensorHalServerImpl::SetSensorService() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  sensor_service_ = SensorServiceImpl::Create(
      ipc_task_runner_, std::make_unique<libmems::IioContextImpl>());
}

void SensorHalServerImpl::OnSensorHalServerError() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(receiver_.is_bound());

  LOGF(ERROR) << "Connection to broker lost";
  receiver_.reset();
  std::move(mojo_on_failure_callback_).Run();
}

}  // namespace iioservice
