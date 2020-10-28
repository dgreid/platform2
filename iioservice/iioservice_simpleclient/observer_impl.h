// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_IIOSERVICE_SIMPLECLIENT_OBSERVER_IMPL_H_
#define IIOSERVICE_IIOSERVICE_SIMPLECLIENT_OBSERVER_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/sequenced_task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "mojo/cros_sensor_service.mojom.h"
#include "mojo/sensor.mojom.h"

namespace iioservice {

class ObserverImpl final : public cros::mojom::SensorHalClient,
                           public cros::mojom::SensorDeviceSamplesObserver {
 public:
  using QuitCallback = base::OnceCallback<void()>;

  static void ObserverImplDeleter(ObserverImpl* observer);
  using ScopedObserverImpl =
      std::unique_ptr<ObserverImpl, decltype(&ObserverImplDeleter)>;

  // The task runner should be the same as the one provided to SensorClient.
  static ScopedObserverImpl Create(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      int device_id,
      cros::mojom::DeviceType device_type,
      std::vector<std::string> channel_ids,
      double frequency,
      int timeout,
      int samples,
      QuitCallback quit_callback);

  void BindClient(mojo::PendingReceiver<cros::mojom::SensorHalClient> client);

  // cros::mojom::SensorHalClient overrides:
  void SetUpChannel(
      mojo::PendingRemote<cros::mojom::SensorService> pending_remote) override;

  // cros::mojom::SensorDeviceSamplesObserver overrides:
  void OnSampleUpdated(const base::flat_map<int32_t, int64_t>& sample) override;
  void OnErrorOccurred(cros::mojom::ObserverErrorType type) override;

 private:
  ObserverImpl(scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
               int device_id,
               cros::mojom::DeviceType device_type,
               std::vector<std::string> channel_ids,
               double frequency,
               int timeout,
               int samples,
               QuitCallback quit_callback);

  void SetUpChannelTimeout();
  mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> GetRemote();

  void OnClientDisconnect();
  void OnServiceDisconnect();
  void OnDeviceDisconnect();
  void OnObserverDisconnect();

  void GetDeviceIdsByType();
  void GetDeviceIdsCallback(const std::vector<int32_t>& iio_device_ids);
  void GetSensorDevice();
  void GetAllChannelIds();
  void GetAllChannelIdsCallback(const std::vector<std::string>& iio_chn_ids);

  void StartReading();

  void SetFrequencyCallback(double result_freq);
  void SetChannelsEnabledCallback(const std::vector<int32_t>& failed_indices);

  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;

  int device_id_ = -1;
  cros::mojom::DeviceType device_type_ = cros::mojom::DeviceType::NONE;
  const std::vector<std::string> channel_ids_;
  double frequency_;
  double result_freq_ = 0.0;
  int timeout_;
  int samples_;
  QuitCallback quit_callback_;

  std::vector<int32_t> channel_indices_;
  std::vector<std::string> iio_chn_ids_;

  base::Optional<int> timestamp_index_ = base::nullopt;

  int num_success_reads_ = 0;

  base::TimeDelta total_latency_;
  std::vector<base::TimeDelta> latencies_;

  mojo::Receiver<cros::mojom::SensorHalClient> client_{this};

  mojo::Remote<cros::mojom::SensorService> sensor_service_remote_;
  mojo::Remote<cros::mojom::SensorDevice> sensor_device_remote_;

  mojo::Receiver<cros::mojom::SensorDeviceSamplesObserver> receiver_;

  base::WeakPtrFactory<ObserverImpl> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_IIOSERVICE_SIMPLECLIENT_OBSERVER_IMPL_H_
