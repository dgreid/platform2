// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_CLIENT_H_
#define IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_CLIENT_H_

#include <memory>

#include <base/bind.h>
#include <base/sequenced_task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "mojo/cros_sensor_service.mojom.h"
#include "mojo/sensor.mojom.h"

#include "iioservice/include/export.h"

namespace iioservice {

// A helper class to connect to Chromium via unix socket and wait for
// mojo::PendingRemote<SensorService> connecting to iioservice.
// Upon disconnection errors from iioservice, the user doesn't need to do
// anything except for waiting a new remote's arrival again.
class IIOSERVICE_EXPORT SensorClient final
    : public cros::mojom::SensorHalClient {
 public:
  using SensorServiceReceivedCallback = base::RepeatingCallback<void(
      mojo::PendingRemote<cros::mojom::SensorService>)>;
  using ClientOnFailureCallback = base::RepeatingCallback<void()>;

  static void SensorClientDeleter(SensorClient* client);
  using ScopedSensorClient =
      std::unique_ptr<SensorClient, decltype(&SensorClientDeleter)>;

  // Create a SensorClient instance by providing a task runner for mojo IPC, a
  // callback to receive SensorService remote from |SetUpChannel|, and a
  // callback to abort when an error occurs.
  static ScopedSensorClient Create(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      mojo::PendingReceiver<cros::mojom::SensorHalClient> pending_receiver,
      SensorServiceReceivedCallback sensor_service_received_callback,
      ClientOnFailureCallback client_on_failure_callback);

  // Implementation of cros::mojom::SensorHalClient. Called by sensor HAL
  // dispatcher to provide the SensorService interface.
  void SetUpChannel(mojo::PendingRemote<cros::mojom::SensorService>
                        sensor_service_ptr) override;

 private:
  SensorClient(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      mojo::PendingReceiver<cros::mojom::SensorHalClient> pending_receiver,
      SensorServiceReceivedCallback sensor_service_received_callback,
      ClientOnFailureCallback client_on_failure_callback);

  void OnClientError();

  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;

  mojo::Receiver<cros::mojom::SensorHalClient> receiver_;
  SensorServiceReceivedCallback sensor_service_received_callback_;
  ClientOnFailureCallback client_on_failure_callback_;

  base::WeakPtrFactory<SensorClient> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_CLIENT_H_
