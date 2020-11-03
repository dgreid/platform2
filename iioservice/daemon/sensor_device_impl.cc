// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/sensor_device_impl.h"

#include <utility>

#include <base/bind.h>
#include <base/strings/string_util.h>
#include <libmems/common_types.h>
#include <libmems/iio_channel.h>

#include "iioservice/include/common.h"

namespace iioservice {

// static
void SensorDeviceImpl::SensorDeviceImplDeleter(SensorDeviceImpl* device) {
  if (device == nullptr)
    return;

  if (!device->ipc_task_runner_->RunsTasksInCurrentSequence()) {
    device->ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SensorDeviceImpl::SensorDeviceImplDeleter, device));
    return;
  }

  delete device;
}

// static
SensorDeviceImpl::ScopedSensorDeviceImpl SensorDeviceImpl::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    libmems::IioContext* context) {
  DCHECK(ipc_task_runner->RunsTasksInCurrentSequence());

  ScopedSensorDeviceImpl device(nullptr, SensorDeviceImplDeleter);

  std::unique_ptr<base::Thread> thread(new base::Thread("SensorDeviceImpl"));
  if (!thread->StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOGF(ERROR) << "Failed to start thread with TYPE_IO";
    device.reset();
    return device;
  }

  // TODO(chenghaoyang): Check how to detect it's Samus, which doesn't use fifo.
  device.reset(new SensorDeviceImpl(std::move(ipc_task_runner), context,
                                    std::move(thread), true));

  return device;
}

SensorDeviceImpl::~SensorDeviceImpl() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  samples_handlers_.clear();
  sample_thread_->Stop();
  receiver_set_.Clear();
  clients_.clear();
}

void SensorDeviceImpl::AddReceiver(
    int32_t iio_device_id,
    mojo::PendingReceiver<cros::mojom::SensorDevice> request) {
  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&SensorDeviceImpl::AddReceiverOnThread,
                                weak_factory_.GetWeakPtr(), iio_device_id,
                                std::move(request)));
}

void SensorDeviceImpl::SetTimeout(uint32_t timeout) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  clients_[id].timeout = timeout;
}

void SensorDeviceImpl::GetAttributes(const std::vector<std::string>& attr_names,
                                     GetAttributesCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  std::vector<base::Optional<std::string>> values;
  values.reserve(attr_names.size());
  for (const auto& attr_name : attr_names) {
    auto value_opt = clients_[id].iio_device->ReadStringAttribute(attr_name);
    if (value_opt.has_value()) {
      value_opt =
          base::TrimString(value_opt.value(), base::StringPiece("\0\n", 2),
                           base::TRIM_TRAILING)
              .as_string();
    }

    values.push_back(std::move(value_opt));
  }

  std::move(callback).Run(std::move(values));
}

void SensorDeviceImpl::SetFrequency(double frequency,
                                    SetFrequencyCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  ClientData& client = clients_[id];

  if (AddSamplesHandlerIfNotSet(client.iio_device)) {
    samples_handlers_.at(client.iio_device)
        ->UpdateFrequency(&client, frequency, std::move(callback));
    return;
  }

  // Failed to add the SamplesHandler
  client.frequency = frequency;
  std::move(callback).Run(frequency);
}

void SensorDeviceImpl::StartReadingSamples(
    mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> observer) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  ClientData& client = clients_[id];

  if (client.observer.is_bound()) {
    LOGF(ERROR) << "Reading already started: " << id;
    mojo::Remote<cros::mojom::SensorDeviceSamplesObserver>(std::move(observer))
        ->OnErrorOccurred(cros::mojom::ObserverErrorType::ALREADY_STARTED);
    return;
  }

  if (!AddSamplesHandlerIfNotSet(client.iio_device)) {
    observer.reset();
    return;
  }
  client.observer.Bind(std::move(observer));

  samples_handlers_.at(client.iio_device)->AddClient(&client);
}

void SensorDeviceImpl::StopReadingSamples() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  ClientData& client = clients_[id];

  if (samples_handlers_.find(client.iio_device) != samples_handlers_.end())
    samples_handlers_.at(client.iio_device)->RemoveClient(&client);

  client.observer.reset();
}

void SensorDeviceImpl::GetAllChannelIds(GetAllChannelIdsCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  auto iio_device = clients_[id].iio_device;
  std::vector<std::string> chn_ids;
  for (auto iio_channel : iio_device->GetAllChannels())
    chn_ids.push_back(iio_channel->GetId());

  std::move(callback).Run(std::move(chn_ids));
}

void SensorDeviceImpl::SetChannelsEnabled(
    const std::vector<int32_t>& iio_chn_indices,
    bool en,
    SetChannelsEnabledCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  ClientData& client = clients_[id];

  if (AddSamplesHandlerIfNotSet(client.iio_device)) {
    samples_handlers_.at(client.iio_device)
        ->UpdateChannelsEnabled(&client, std::move(iio_chn_indices), en,
                                std::move(callback));
    return;
  }

  // List of channels failed to enabled.
  std::vector<int32_t> failed_indices;

  if (en) {
    for (int32_t chn_index : iio_chn_indices) {
      auto chn = client.iio_device->GetChannel(chn_index);
      if (!chn || !chn->IsEnabled()) {
        LOG(ERROR) << "Failed to enable chn with index: " << chn_index;
        failed_indices.push_back(chn_index);
        continue;
      }

      client.enabled_chn_indices.emplace(chn_index);
    }
  } else {
    for (int32_t chn_index : iio_chn_indices)
      client.enabled_chn_indices.erase(chn_index);
  }

  std::move(callback).Run(std::move(failed_indices));
}

void SensorDeviceImpl::GetChannelsEnabled(
    const std::vector<int32_t>& iio_chn_indices,
    GetChannelsEnabledCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  ClientData& client = clients_[id];

  if (AddSamplesHandlerIfNotSet(client.iio_device)) {
    samples_handlers_.at(client.iio_device)
        ->GetChannelsEnabled(&client, std::move(iio_chn_indices),
                             std::move(callback));
    return;
  }

  // List of channels failed to enabled.
  std::vector<bool> enabled;

  for (int32_t chn_index : iio_chn_indices) {
    enabled.push_back(client.enabled_chn_indices.find(chn_index) !=
                      client.enabled_chn_indices.end());
  }

  std::move(callback).Run(std::move(enabled));
}

void SensorDeviceImpl::GetChannelsAttributes(
    const std::vector<int32_t>& iio_chn_indices,
    const std::string& attr_name,
    GetChannelsAttributesCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();
  ClientData& client = clients_[id];
  auto iio_device = client.iio_device;

  std::vector<base::Optional<std::string>> values;

  for (int32_t chn_index : iio_chn_indices) {
    auto chn = iio_device->GetChannel(chn_index);

    if (!chn) {
      LOG(ERROR) << "Cannot find chn with index: " << chn_index;
      values.push_back(base::nullopt);
      continue;
    }

    values.push_back(chn->ReadStringAttribute(attr_name));
  }

  std::move(callback).Run(std::move(values));
}

SensorDeviceImpl::SensorDeviceImpl(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    libmems::IioContext* context,
    std::unique_ptr<base::Thread> thread,
    bool use_fifo)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      context_(std::move(context)),
      sample_thread_(std::move(thread)),
      use_fifo_(use_fifo) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  receiver_set_.set_disconnect_handler(base::BindRepeating(
      &SensorDeviceImpl::ConnectionErrorCallback, weak_factory_.GetWeakPtr()));
}

void SensorDeviceImpl::AddReceiverOnThread(
    int32_t iio_device_id,
    mojo::PendingReceiver<cros::mojom::SensorDevice> request) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  auto iio_device = context_->GetDeviceById(iio_device_id);
  if (!iio_device) {
    LOGF(ERROR) << "Invalid iio_device_id: " << iio_device_id;
    return;
  }

  mojo::ReceiverId id =
      receiver_set_.Add(this, std::move(request), ipc_task_runner_);
  clients_[id].id = id;
  clients_[id].iio_device = iio_device;
}

void SensorDeviceImpl::ConnectionErrorCallback() {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  mojo::ReceiverId id = receiver_set_.current_receiver();

  LOGF(INFO) << "Connection error, ReceiverId: " << id;
  StopReadingSamples();
  sample_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SensorDeviceImpl::RemoveClient,
                                base::Unretained(this), id));
}

void SensorDeviceImpl::RemoveClient(mojo::ReceiverId id) {
  DCHECK(sample_thread_->task_runner()->RunsTasksInCurrentSequence());

  clients_.erase(id);
}

bool SensorDeviceImpl::AddSamplesHandlerIfNotSet(
    libmems::IioDevice* iio_device) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (samples_handlers_.find(iio_device) != samples_handlers_.end())
    return true;

  SamplesHandler::ScopedSamplesHandler handler = {
      nullptr, SamplesHandler::SamplesHandlerDeleter};

  auto sample_cb = base::BindRepeating(
      &SensorDeviceImpl::OnSampleUpdatedCallback, weak_factory_.GetWeakPtr());
  auto error_cb = base::BindRepeating(
      &SensorDeviceImpl::OnErrorOccurredCallback, weak_factory_.GetWeakPtr());

  if (use_fifo_) {
    handler = SamplesHandler::CreateWithFifo(
        ipc_task_runner_, sample_thread_->task_runner(), iio_device,
        std::move(sample_cb), std::move(error_cb));
  } else {
    handler = SamplesHandler::CreateWithoutFifo(
        ipc_task_runner_, sample_thread_->task_runner(), context_, iio_device,
        std::move(sample_cb), std::move(error_cb));
  }

  if (!handler) {
    LOGF(ERROR) << "Failed to create the samples handler for device: "
                << iio_device->GetId();
    return false;
  }

  samples_handlers_.emplace(iio_device, std::move(handler));
  return true;
}

void SensorDeviceImpl::OnSampleUpdatedCallback(
    mojo::ReceiverId id, libmems::IioDevice::IioSample sample) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  auto it = clients_.find(id);
  if (it == clients_.end()) {
    LOGF(WARNING) << "Sample not sent, as the client doesn't exist: " << id;
    return;
  }

  if (!it->second.observer.is_bound()) {
    LOGF(WARNING) << "Sample not sent, as the client has stopped reading: "
                  << id;
    return;
  }

  it->second.observer->OnSampleUpdated(std::move(sample));
}

void SensorDeviceImpl::OnErrorOccurredCallback(
    mojo::ReceiverId id, cros::mojom::ObserverErrorType type) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());
  auto it = clients_.find(id);
  if (it == clients_.end()) {
    LOGF(WARNING) << "Error not sent, as the client doesn't exist: " << id;
    return;
  }

  if (!it->second.observer.is_bound()) {
    LOGF(WARNING) << "Sample not sent, as the client has stopped reading: "
                  << id;
    return;
  }

  it->second.observer->OnErrorOccurred(type);
}

}  // namespace iioservice
