// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/libiioservice_ipc/sensor_client.h"

#include <memory>
#include <utility>

#include <mojo/core/embedder/embedder.h>

#include "iioservice/include/common.h"
#include "iioservice/include/constants.h"
#include "iioservice/libiioservice_ipc/ipc_util.h"

namespace cros {

namespace iioservice {

// static
void SensorClient::SensorClientDeleter(SensorClient* client) {
  if (client == nullptr)
    return;

  if (!client->ipc_task_runner_->RunsTasksInCurrentSequence()) {
    client->ipc_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&SensorClient::SensorClientDeleter, client));
    return;
  }

  delete client;
}

// static
SensorClient::ScopedSensorClient SensorClient::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    SensorServiceReceivedCallback sensor_service_received_callback,
    InitOnFailureCallback init_on_failure_callback) {
  ScopedSensorClient client(
      new SensorClient(ipc_task_runner,
                       std::move(sensor_service_received_callback),
                       std::move(init_on_failure_callback)),
      SensorClientDeleter);

  return client;
}

void SensorClient::SetUpChannel(
    mojo::PendingRemote<mojom::SensorService> sensor_service_ptr) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(INFO) << "Received SensorService from sensor HAL dispatcher";

  sensor_service_received_callback_.Run(std::move(sensor_service_ptr));
}

SensorClient::SensorClient(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    SensorServiceReceivedCallback sensor_service_received_callback,
    InitOnFailureCallback init_on_failure_callback)
    : ipc_task_runner_(ipc_task_runner),
      receiver_(this),
      sensor_service_received_callback_(
          std::move(sensor_service_received_callback)),
      init_on_failure_callback_(std::move(init_on_failure_callback)) {
  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SensorClient::InitOnThread, weak_factory_.GetWeakPtr()));
}

void SensorClient::InitOnThread() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ScopedMessagePipeHandle child_pipe;
  MojoResult res = CreateMojoChannelToParentByUnixDomainSocket(
      kIioserviceClientSocketPathString, &child_pipe);
  if (res != MOJO_RESULT_OK) {
    LOGF(ERROR) << "Failed to create mojo channel to broker";
    std::move(init_on_failure_callback_).Run();  // -ENODEV
    return;
  }

  receiver_.Bind(
      mojo::PendingReceiver<mojom::SensorHalClient>(std::move(child_pipe)));

  if (!receiver_.is_bound()) {
    LOGF(ERROR) << "Failed to connect to broker";
    std::move(init_on_failure_callback_).Run();  // -ENODEV
    return;
  }

  receiver_.set_disconnect_handler(
      base::BindOnce(&SensorClient::OnClientError, base::Unretained(this)));
  LOGF(INFO) << "Connected to broker";
}

void SensorClient::OnClientError() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "Connection to broker lost";
  receiver_.reset();
  InitOnThread();
}

}  // namespace iioservice

}  // namespace cros
