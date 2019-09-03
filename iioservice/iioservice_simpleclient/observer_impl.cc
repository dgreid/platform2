// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/observer_impl.h"

#include <utility>

#include <base/bind.h>

#include "iioservice/include/common.h"

namespace iioservice {

namespace {

constexpr int kSetUpChannelTimeoutInMilliseconds = 3000;

}

// static
void ObserverImpl::ObserverImplDeleter(ObserverImpl* observer) {
  if (observer == nullptr)
    return;

  if (!observer->ipc_task_runner_->RunsTasksInCurrentSequence()) {
    observer->ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&ObserverImpl::ObserverImplDeleter, observer));
    return;
  }

  delete observer;
}

// static
ObserverImpl::ScopedObserverImpl ObserverImpl::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    int device_id,
    cros::mojom::DeviceType device_type,
    std::vector<std::string> channel_ids,
    double frequency,
    int timeout,
    QuitCallback quit_callback) {
  ScopedObserverImpl observer(
      new ObserverImpl(ipc_task_runner, device_id, device_type,
                       std::move(channel_ids), frequency, timeout,
                       std::move(quit_callback)),
      ObserverImplDeleter);

  return observer;
}

void ObserverImpl::BindClient(
    mojo::PendingReceiver<cros::mojom::SensorHalClient> client) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!client_.is_bound());

  client_.Bind(std::move(client));
  client_.set_disconnect_handler(base::BindOnce(
      &ObserverImpl::OnClientDisconnect, weak_factory_.GetWeakPtr()));
}

void ObserverImpl::SetUpChannel(
    mojo::PendingRemote<cros::mojom::SensorService> pending_remote) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!sensor_service_remote_.is_bound());

  sensor_service_remote_.Bind(std::move(pending_remote));
  sensor_service_remote_.set_disconnect_handler(base::BindOnce(
      &ObserverImpl::OnServiceDisconnect, weak_factory_.GetWeakPtr()));

  if (device_id_ < 0)
    GetDeviceIdsByType();
  else
    GetSensorDevice();
}

void ObserverImpl::OnSampleUpdated(
    const base::flat_map<int32_t, int64_t>& sample) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (sample.size() != channel_indices_.size()) {
    LOGF(ERROR) << "Invalid sample size: " << sample.size()
                << ", expected size: " << channel_indices_.size();
  }

  for (auto chn : sample)
    LOGF(INFO) << iio_chn_ids_[chn.first] << ": " << chn.second;

  if (++num_success_reads_ >= kNumSuccessReads) {
    // Don't Change: Used as a check sentence in the tast test.
    LOGF(INFO) << "Number of success reads " << kNumSuccessReads << " achieved";
    if (quit_callback_)
      std::move(quit_callback_).Run();
  }
}

void ObserverImpl::OnErrorOccurred(cros::mojom::ObserverErrorType type) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  // Don't Change: Used as a check sentence in the tast test.
  LOGF(ERROR) << "OnErrorOccurred: " << type;
  if (quit_callback_)
    std::move(quit_callback_).Run();
}

ObserverImpl::ObserverImpl(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    int device_id,
    cros::mojom::DeviceType device_type,
    std::vector<std::string> channel_ids,
    double frequency,
    int timeout,
    QuitCallback quit_callback)
    : ipc_task_runner_(ipc_task_runner),
      device_id_(device_id),
      device_type_(device_type),
      channel_ids_(std::move(channel_ids)),
      frequency_(frequency),
      timeout_(timeout),
      quit_callback_(std::move(quit_callback)),
      receiver_(this) {
  ipc_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ObserverImpl::SetUpChannelTimeout,
                     weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kSetUpChannelTimeoutInMilliseconds));
}

mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver>
ObserverImpl::GetRemote() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  auto remote = receiver_.BindNewPipeAndPassRemote();
  receiver_.set_disconnect_handler(base::BindOnce(
      &ObserverImpl::OnObserverDisconnect, weak_factory_.GetWeakPtr()));

  return remote;
}

void ObserverImpl::SetUpChannelTimeout() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (sensor_service_remote_.is_bound())
    return;

  // Don't Change: Used as a check sentence in the tast test.
  LOGF(ERROR) << "SetUpChannelTimeout";
  if (quit_callback_)
    std::move(quit_callback_).Run();
}

void ObserverImpl::OnClientDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SensorHalClient disconnected";
  if (quit_callback_)
    std::move(quit_callback_).Run();
}

void ObserverImpl::OnServiceDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SensorService disconnected";
  if (quit_callback_)
    std::move(quit_callback_).Run();
}

void ObserverImpl::OnDeviceDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "SensorDevice disconnected";
  if (quit_callback_)
    std::move(quit_callback_).Run();
}

void ObserverImpl::OnObserverDisconnect() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  LOGF(ERROR) << "Observer diconnected";
  if (quit_callback_)
    std::move(quit_callback_).Run();
}

void ObserverImpl::GetDeviceIdsByType() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  DCHECK_NE(device_type_, cros::mojom::DeviceType::NONE);

  sensor_service_remote_->GetDeviceIds(
      device_type_, base::BindOnce(&ObserverImpl::GetDeviceIdsCallback,
                                   weak_factory_.GetWeakPtr()));
}

void ObserverImpl::GetDeviceIdsCallback(
    const std::vector<int32_t>& iio_device_ids) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (iio_device_ids.empty()) {
    LOGF(ERROR) << "No device found give device type: " << device_type_;
    std::move(quit_callback_).Run();
  }

  // Take the first id.
  device_id_ = iio_device_ids.front();
  GetSensorDevice();
}

void ObserverImpl::GetSensorDevice() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (sensor_device_remote_.is_bound())
    sensor_device_remote_.reset();

  sensor_service_remote_->GetDevice(
      device_id_, sensor_device_remote_.BindNewPipeAndPassReceiver());

  sensor_device_remote_.set_disconnect_handler(base::BindOnce(
      &ObserverImpl::OnDeviceDisconnect, weak_factory_.GetWeakPtr()));
  GetAllChannelIds();
}

void ObserverImpl::GetAllChannelIds() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  sensor_device_remote_->GetAllChannelIds(base::BindOnce(
      &ObserverImpl::GetAllChannelIdsCallback, weak_factory_.GetWeakPtr()));
}

void ObserverImpl::GetAllChannelIdsCallback(
    const std::vector<std::string>& iio_chn_ids) {
  iio_chn_ids_ = std::move(iio_chn_ids);
  channel_indices_.clear();

  for (int32_t i = 0; i < channel_ids_.size(); ++i) {
    for (int32_t j = 0; j < iio_chn_ids_.size(); ++j) {
      if (channel_ids_[i] == iio_chn_ids_[j]) {
        channel_indices_.push_back(j);
        break;
      }
    }
  }

  if (channel_indices_.empty()) {
    LOGF(ERROR) << "No available channels";
    if (quit_callback_)
      std::move(quit_callback_).Run();

    return;
  }

  StartReading();
}

void ObserverImpl::StartReading() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  sensor_device_remote_->SetTimeout(timeout_);
  sensor_device_remote_->SetFrequency(
      frequency_, base::BindOnce(&ObserverImpl::SetFrequencyCallback,
                                 weak_factory_.GetWeakPtr()));
  sensor_device_remote_->SetChannelsEnabled(
      channel_indices_, true,
      base::BindOnce(&ObserverImpl::SetChannelsEnabledCallback,
                     weak_factory_.GetWeakPtr()));

  sensor_device_remote_->StartReadingSamples(GetRemote());
}

void ObserverImpl::SetFrequencyCallback(double result_freq) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (result_freq > 0.0)
    return;

  LOGF(ERROR) << "Failed to set frequency";
  if (quit_callback_)
    std::move(quit_callback_).Run();
}

void ObserverImpl::SetChannelsEnabledCallback(
    const std::vector<int32_t>& failed_indices) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  for (int32_t index : failed_indices) {
    LOGF(ERROR) << "Failed channel index: " << index;
    bool found = false;
    for (auto it = channel_indices_.begin(); it != channel_indices_.end();
         ++it) {
      if (index == *it) {
        found = true;
        channel_indices_.erase(it);
        break;
      }
    }

    if (!found)
      LOGF(ERROR) << index << " not in requested indices";
  }

  if (channel_indices_.empty()) {
    LOGF(ERROR) << "No channel enabled";
    if (quit_callback_)
      std::move(quit_callback_).Run();
  }
}

}  // namespace iioservice
