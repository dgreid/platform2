// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/libiioservice_ipc/sensor_client.h"

#include <memory>
#include <utility>

#include <mojo/core/embedder/embedder.h>

#include "iioservice/include/common.h"
#include "iioservice/include/constants.h"

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
    mojo::PendingReceiver<cros::mojom::SensorHalClient> pending_receiver,
    SensorServiceReceivedCallback sensor_service_received_callback,
    ClientOnFailureCallback client_on_failure_callback) {
  DCHECK(ipc_task_runner->RunsTasksInCurrentSequence());

  ScopedSensorClient client(
      new SensorClient(ipc_task_runner, std::move(pending_receiver),
                       std::move(sensor_service_received_callback),
                       std::move(client_on_failure_callback)),
      SensorClientDeleter);

  return client;
}

void SensorClient::SetUpChannel(
    mojo::PendingRemote<cros::mojom::SensorService> sensor_service_ptr) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(INFO) << "Received SensorService from sensor HAL dispatcher";

  sensor_service_received_callback_.Run(std::move(sensor_service_ptr));
}

SensorClient::SensorClient(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    mojo::PendingReceiver<cros::mojom::SensorHalClient> pending_receiver,
    SensorServiceReceivedCallback sensor_service_received_callback,
    ClientOnFailureCallback client_on_failure_callback)
    : ipc_task_runner_(ipc_task_runner),
      receiver_(this),
      sensor_service_received_callback_(
          std::move(sensor_service_received_callback)),
      client_on_failure_callback_(std::move(client_on_failure_callback)) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  receiver_.Bind(std::move(pending_receiver), ipc_task_runner_);
  receiver_.set_disconnect_handler(
      base::BindOnce(&SensorClient::OnClientError, base::Unretained(this)));
  LOGF(INFO) << "Connected to broker";
}

void SensorClient::OnClientError() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(receiver_.is_bound());
  DCHECK(!client_on_failure_callback_.is_null());

  LOGF(ERROR) << "Connection to broker lost";
  receiver_.reset();
  std::move(client_on_failure_callback_).Run();
}

}  // namespace iioservice
