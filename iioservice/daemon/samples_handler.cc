// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/samples_handler.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/sequenced_task_runner.h>
#include <base/single_thread_task_runner.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>
#include <libmems/test_fakes.h>
#include <libmems/common_types.h>
#include <libmems/iio_channel.h>
#include <libmems/iio_context.h>
#include <libmems/iio_device.h>

#include "iioservice/include/common.h"
#include "iioservice/include/constants.h"

namespace iioservice {

namespace {

constexpr char kNoBatchChannels[][10] = {"timestamp", "count"};
constexpr char kHWFifoFlushPath[] = "buffer/hwfifo_flush";

}  // namespace

// static
void SamplesHandler::SamplesHandlerDeleter(SamplesHandler* handler) {
  if (handler == nullptr)
    return;

  if (!handler->sample_task_runner_->BelongsToCurrentThread()) {
    handler->sample_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SamplesHandler::SamplesHandlerDeleter, handler));
    return;
  }

  delete handler;
}

// static
bool SamplesHandler::GetDevMinMaxFrequency(libmems::IioDevice* iio_device,
                                           double* min_freq,
                                           double* max_freq) {
  auto available_opt =
      iio_device->ReadStringAttribute(kSamplingFrequencyAvailable);
  if (!available_opt.has_value()) {
    LOG(ERROR) << "Failed to read attribute: " << kSamplingFrequencyAvailable;
    return false;
  }

  std::string sampling_frequency_available = available_opt.value();
  // Remove trailing '\0' for parsing
  auto pos = available_opt->find_first_of('\0');
  if (pos != std::string::npos)
    sampling_frequency_available = available_opt->substr(0, pos);

  std::vector<std::string> sampling_frequencies =
      base::SplitString(sampling_frequency_available, " ",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  switch (sampling_frequencies.size()) {
    case 0:
      LOG(ERROR) << "Invalid format of " << kSamplingFrequencyAvailable << ": "
                 << sampling_frequency_available;
      return false;

    case 1:
      if (!base::StringToDouble(sampling_frequencies.front(), min_freq) ||
          *min_freq < 0.0 || *min_freq < kFrequencyEpsilon) {
        LOG(ERROR) << "Failed to parse min max sampling_frequency: "
                   << sampling_frequency_available;
        return false;
      }

      *max_freq = *min_freq;
      return true;

    default:
      if (!base::StringToDouble(sampling_frequencies.back(), max_freq) ||
          *max_freq < kFrequencyEpsilon) {
        LOG(ERROR) << "Failed to parse max sampling_frequency: "
                   << sampling_frequency_available;
        return false;
      }

      if (!base::StringToDouble(sampling_frequencies.front(), min_freq) ||
          *min_freq < 0.0) {
        LOG(ERROR) << "Failed to parse the first sampling_frequency: "
                   << sampling_frequency_available;
        return false;
      }

      if (*min_freq == 0.0) {
        if (!base::StringToDouble(sampling_frequencies[1], min_freq) ||
            *min_freq < 0.0 || *max_freq < *min_freq) {
          LOG(ERROR) << "Failed to parse min sampling_frequency: "
                     << sampling_frequency_available;
          return false;
        }
      }

      return true;
  }
}

// static
SamplesHandler::ScopedSamplesHandler SamplesHandler::CreateWithFifo(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> sample_task_runner,
    libmems::IioDevice* iio_device,
    OnSampleUpdatedCallback on_sample_updated_callback,
    OnErrorOccurredCallback on_error_occurred_callback) {
  ScopedSamplesHandler handler(nullptr, SamplesHandlerDeleter);

  double min_freq, max_freq;
  if (!GetDevMinMaxFrequency(iio_device, &min_freq, &max_freq))
    return handler;

  handler.reset(new SamplesHandler(
      std::move(ipc_task_runner), std::move(sample_task_runner), iio_device,
      min_freq, max_freq, std::move(on_sample_updated_callback),
      std::move(on_error_occurred_callback)));
  return handler;
}

// static
SamplesHandler::ScopedSamplesHandler SamplesHandler::CreateWithoutFifo(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> sample_task_runner,
    libmems::IioContext* iio_context,
    libmems::IioDevice* iio_device,
    OnSampleUpdatedCallback on_sample_updated_callback,
    OnErrorOccurredCallback on_error_occurred_callback) {
  ScopedSamplesHandler handler(nullptr, SamplesHandlerDeleter);

  double min_freq, max_freq;
  if (!GetDevMinMaxFrequency(iio_device, &min_freq, &max_freq))
    return handler;

  auto trigger_device = iio_device->GetTrigger();
  if (!trigger_device) {
    trigger_device = iio_context->GetTriggerById(iio_device->GetId() + 1);
    if (!trigger_device) {
      LOGF(ERROR) << "Failed to find trigger with id: " << iio_device->GetId();

      return handler;
    }
  }

  handler.reset(new SamplesHandler(
      std::move(ipc_task_runner), std::move(sample_task_runner), iio_device,
      trigger_device, min_freq, max_freq, std::move(on_sample_updated_callback),
      std::move(on_error_occurred_callback)));
  return handler;
}

SamplesHandler::~SamplesHandler() {
  if (requested_frequency_ > 0.0 &&
      !iio_device_->WriteDoubleAttribute(libmems::kSamplingFrequencyAttr, 0.0))
    LOGF(ERROR) << "Failed to set frequency";
}

void SamplesHandler::AddClient(ClientData* client_data) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&SamplesHandler::AddClientOnThread,
                                weak_factory_.GetWeakPtr(), client_data));
}

void SamplesHandler::RemoveClient(ClientData* client_data) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&SamplesHandler::RemoveClientOnThread,
                                weak_factory_.GetWeakPtr(), client_data));
}
void SamplesHandler::UpdateFrequency(
    ClientData* client_data,
    double frequency,
    cros::mojom::SensorDevice::SetFrequencyCallback callback) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&SamplesHandler::UpdateFrequencyOnThread,
                                weak_factory_.GetWeakPtr(), client_data,
                                frequency, std::move(callback)));
}
void SamplesHandler::UpdateChannelsEnabled(
    ClientData* client_data,
    const std::vector<int32_t>& iio_chn_indices,
    bool en,
    cros::mojom::SensorDevice::SetChannelsEnabledCallback callback) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SamplesHandler::UpdateChannelsEnabledOnThread,
                     weak_factory_.GetWeakPtr(), client_data,
                     std::move(iio_chn_indices), en, std::move(callback)));
}

void SamplesHandler::GetChannelsEnabled(
    ClientData* client_data,
    const std::vector<int32_t>& iio_chn_indices,
    cros::mojom::SensorDevice::GetChannelsEnabledCallback callback) {
  DCHECK_EQ(client_data->iio_device, iio_device_);

  sample_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&SamplesHandler::GetChannelsEnabledOnThread,
                     weak_factory_.GetWeakPtr(), client_data,
                     std::move(iio_chn_indices), std::move(callback)));
}

SamplesHandler::SamplesHandler(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> sample_task_runner,
    libmems::IioDevice* iio_device,
    double min_freq,
    double max_freq,
    OnSampleUpdatedCallback on_sample_updated_callback,
    OnErrorOccurredCallback on_error_occurred_callback)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      sample_task_runner_(std::move(sample_task_runner)),
      use_fifo_(true),
      iio_device_(iio_device),
      trigger_device_(nullptr),
      dev_min_frequency_(min_freq),
      dev_max_frequency_(max_freq),
      on_sample_updated_callback_(std::move(on_sample_updated_callback)),
      on_error_occurred_callback_(std::move(on_error_occurred_callback)) {
  DCHECK_GE(dev_max_frequency_, dev_min_frequency_);

  auto channels = iio_device_->GetAllChannels();
  for (size_t i = 0; i < channels.size(); ++i) {
    for (size_t j = 0; j < base::size(kNoBatchChannels); ++j) {
      if (strcmp(channels[i]->GetId(), kNoBatchChannels[j]) == 0) {
        no_batch_chn_indices.emplace(i);
        break;
      }
    }
  }
}

SamplesHandler::SamplesHandler(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> sample_task_runner,
    libmems::IioDevice* iio_device,
    libmems::IioDevice* trigger_device,
    double min_freq,
    double max_freq,
    OnSampleUpdatedCallback on_sample_updated_callback,
    OnErrorOccurredCallback on_error_occurred_callback)
    : ipc_task_runner_(std::move(ipc_task_runner)),
      sample_task_runner_(std::move(sample_task_runner)),
      use_fifo_(false),
      iio_device_(iio_device),
      trigger_device_(trigger_device),
      dev_min_frequency_(min_freq),
      dev_max_frequency_(max_freq),
      on_sample_updated_callback_(std::move(on_sample_updated_callback)),
      on_error_occurred_callback_(std::move(on_error_occurred_callback)) {
  DCHECK_GE(dev_max_frequency_, dev_min_frequency_);

  auto channels = iio_device_->GetAllChannels();
  for (size_t i = 0; i < channels.size(); ++i) {
    for (size_t j = 0; j < base::size(kNoBatchChannels); ++j) {
      if (strcmp(channels[i]->GetId(), kNoBatchChannels[j]) == 0) {
        no_batch_chn_indices.emplace(i);
        break;
      }
    }
  }
}

void SamplesHandler::SetSampleWatcherOnThread() {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  // Flush the old samples in EC FIFO.
  if (!iio_device_->WriteStringAttribute(kHWFifoFlushPath, "1\n"))
    LOGF(ERROR) << "Failed to flush the old samples in EC FIFO";

  auto fd = iio_device_->GetBufferFd();
  if (!fd.has_value()) {
    LOGF(ERROR) << "Failed to get fd";
    for (auto client : clients_map_) {
      ipc_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(on_error_occurred_callback_, client.first->id,
                         cros::mojom::ObserverErrorType::GET_FD_FAILED));
    }

    return;
  }

  watcher_ = base::FileDescriptorWatcher::WatchReadable(
      fd.value(),
      base::BindRepeating(&SamplesHandler::OnSampleAvailableWithoutBlocking,
                          weak_factory_.GetWeakPtr()));
}

void SamplesHandler::StopSampleWatcherOnThread() {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  watcher_.reset();
}

void SamplesHandler::AddActiveClientOnThread(ClientData* client_data) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);
  DCHECK_GE(client_data->frequency, kFrequencyEpsilon);
  DCHECK(!client_data->enabled_chn_indices.empty());
  DCHECK(inactive_clients_.find(client_data) == inactive_clients_.end());
  DCHECK(clients_map_.find(client_data) == clients_map_.end());

  clients_map_.emplace(client_data, SampleData{});
  clients_map_[client_data].sample_index = samples_cnt_;

  if (!watcher_.get())
    SetSampleWatcherOnThread();

  SetTimeoutTaskOnThread(client_data);

  if (AddFrequencyOnThread(client_data->frequency))
    return;

  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(on_error_occurred_callback_, client_data->id,
                     cros::mojom::ObserverErrorType::SET_FREQUENCY_IO_FAILED));
}
void SamplesHandler::AddClientOnThread(ClientData* client_data) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  if (inactive_clients_.find(client_data) != inactive_clients_.end() ||
      clients_map_.find(client_data) != clients_map_.end()) {
    // Shouldn't happen. Users should check observer exists or not to know
    // whether the client is already added.
    LOGF(ERROR) << "Failed to AddClient: Already added";
    ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(on_error_occurred_callback_, client_data->id,
                       cros::mojom::ObserverErrorType::ALREADY_STARTED));
    return;
  }

  bool active = true;

  client_data->frequency = FixFrequency(client_data->frequency);
  if (client_data->frequency == 0.0) {
    LOGF(ERROR) << "Added an inactive client: Invalid frequency.";
    ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(on_error_occurred_callback_, client_data->id,
                       cros::mojom::ObserverErrorType::FREQUENCY_INVALID));
    active = false;
  }
  if (client_data->enabled_chn_indices.empty()) {
    LOGF(ERROR) << "Added an inactive client: No enabled channels.";
    ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(on_error_occurred_callback_, client_data->id,
                       cros::mojom::ObserverErrorType::NO_ENABLED_CHANNELS));
    active = false;
  }

  if (!active) {
    inactive_clients_.emplace(client_data);
    return;
  }

  AddActiveClientOnThread(client_data);
}

void SamplesHandler::RemoveActiveClientOnThread(ClientData* client_data,
                                                double orig_freq) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);
  DCHECK_GE(orig_freq, kFrequencyEpsilon);
  DCHECK(!client_data->enabled_chn_indices.empty());
  DCHECK(clients_map_.find(client_data) != clients_map_.end());

  clients_map_.erase(client_data);
  if (clients_map_.empty())
    StopSampleWatcherOnThread();

  if (RemoveFrequencyOnThread(orig_freq))
    return;

  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(on_error_occurred_callback_, client_data->id,
                     cros::mojom::ObserverErrorType::SET_FREQUENCY_IO_FAILED));
}
void SamplesHandler::RemoveClientOnThread(ClientData* client_data) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  auto it = inactive_clients_.find(client_data);
  if (it != inactive_clients_.end()) {
    inactive_clients_.erase(it);
    return;
  }

  if (clients_map_.find(client_data) == clients_map_.end()) {
    LOGF(ERROR) << "Failed to RemoveClient: Client not found";
    return;
  }

  RemoveActiveClientOnThread(client_data, client_data->frequency);
}

double SamplesHandler::FixFrequency(double frequency) {
  if (frequency < kFrequencyEpsilon)
    return 0.0;

  if (frequency < dev_min_frequency_)
    return dev_min_frequency_;

  if (frequency > dev_max_frequency_)
    return dev_max_frequency_;

  return frequency;
}

void SamplesHandler::UpdateFrequencyOnThread(
    ClientData* client_data,
    double frequency,
    cros::mojom::SensorDevice::SetFrequencyCallback callback) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  frequency = FixFrequency(frequency);
  double orig_freq = client_data->frequency;
  client_data->frequency = frequency;
  ipc_task_runner_->PostTask(FROM_HERE,
                             base::BindOnce(std::move(callback), frequency));

  auto it = inactive_clients_.find(client_data);
  if (it != inactive_clients_.end()) {
    if (client_data->frequency > 0.0 &&
        !client_data->enabled_chn_indices.empty()) {
      // The client is now active.
      inactive_clients_.erase(it);
      AddActiveClientOnThread(client_data);
    }

    return;
  }

  if (clients_map_.find(client_data) == clients_map_.end())
    return;

  if (client_data->frequency == 0.0) {
    // The client is now inactive
    RemoveActiveClientOnThread(client_data, orig_freq);
    inactive_clients_.emplace(client_data);

    return;
  }

  // The client remains active
  if (AddFrequencyOnThread(client_data->frequency) &&
      RemoveFrequencyOnThread(orig_freq))
    return;

  // Failed to set device frequency
  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(on_error_occurred_callback_, client_data->id,
                     cros::mojom::ObserverErrorType::SET_FREQUENCY_IO_FAILED));
}

bool SamplesHandler::AddFrequencyOnThread(double frequency) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  frequencies_.emplace(frequency);
  double max_freq = *frequencies_.rbegin();
  DCHECK_GE(max_freq, requested_frequency_);
  return UpdateRequestedFrequencyOnThread(max_freq);
}
bool SamplesHandler::RemoveFrequencyOnThread(double frequency) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  auto it = frequencies_.find(frequency);
  DCHECK(it != frequencies_.end());
  frequencies_.erase(it);
  auto r_it = frequencies_.rbegin();
  double max_freq = (r_it == frequencies_.rend()) ? 0.0 : *r_it;
  DCHECK_LE(max_freq, requested_frequency_);
  return UpdateRequestedFrequencyOnThread(max_freq);
}

bool SamplesHandler::UpdateRequestedFrequencyOnThread(double frequency) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  if (frequency == requested_frequency_)
    return true;

  requested_frequency_ = frequency;
  if (!iio_device_->WriteDoubleAttribute(libmems::kSamplingFrequencyAttr,
                                         frequency)) {
    LOGF(ERROR) << "Failed to set frequency";
    if (use_fifo_)  // ignore this error if no fifo
      return false;
  }

  auto freq_opt =
      iio_device_->ReadDoubleAttribute(libmems::kSamplingFrequencyAttr);
  if (!freq_opt.has_value()) {
    LOGF(ERROR) << "Failed to get frequency";
    return false;
  }
  dev_frequency_ = freq_opt.value();

  if (use_fifo_) {
    if (dev_frequency_ < kFrequencyEpsilon)
      return true;

    if (!iio_device_->WriteDoubleAttribute(libmems::kHWFifoTimeoutAttr,
                                           1.0 / dev_frequency_)) {
      LOGF(ERROR) << "Failed to set fifo timeout";
      return false;
    }

    return true;
  }

  // no fifo
  DCHECK(trigger_device_);

  if (!trigger_device_->WriteDoubleAttribute(libmems::kSamplingFrequencyAttr,
                                             frequency)) {
    LOGF(ERROR) << "Failed to set trigger's frequency";
    return false;
  }

  return true;
}

void SamplesHandler::UpdateChannelsEnabledOnThread(
    ClientData* client_data,
    const std::vector<int32_t>& iio_chn_indices,
    bool en,
    cros::mojom::SensorDevice::SetChannelsEnabledCallback callback) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  std::vector<int32_t> failed_indices;

  if (en) {
    for (int32_t chn_index : iio_chn_indices) {
      auto chn = iio_device_->GetChannel(chn_index);
      if (!chn || !chn->IsEnabled()) {
        LOG(ERROR) << "Failed to enable chn with index: " << chn_index;
        failed_indices.push_back(chn_index);
        continue;
      }

      client_data->enabled_chn_indices.emplace(chn_index);
    }
  } else {
    for (int32_t chn_index : iio_chn_indices) {
      client_data->enabled_chn_indices.erase(chn_index);
      // remove cached chn's moving average
      clients_map_[client_data].chns.erase(chn_index);
    }
  }

  ipc_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(std::move(callback), std::move(failed_indices)));

  auto it = inactive_clients_.find(client_data);
  if (it != inactive_clients_.end()) {
    if (client_data->frequency > 0.0 &&
        !client_data->enabled_chn_indices.empty()) {
      // The client is now active.
      inactive_clients_.erase(it);
      AddActiveClientOnThread(client_data);
    }

    return;
  }

  if (clients_map_.find(client_data) == clients_map_.end())
    return;

  if (!client_data->enabled_chn_indices.empty()) {
    // The client remains active
    return;
  }

  RemoveActiveClientOnThread(client_data, client_data->frequency);
  inactive_clients_.emplace(client_data);
}

void SamplesHandler::GetChannelsEnabledOnThread(
    ClientData* client_data,
    const std::vector<int32_t>& iio_chn_indices,
    cros::mojom::SensorDevice::GetChannelsEnabledCallback callback) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK_EQ(client_data->iio_device, iio_device_);

  std::vector<bool> enabled;

  for (int32_t chn_index : iio_chn_indices) {
    enabled.push_back(client_data->enabled_chn_indices.find(chn_index) !=
                      client_data->enabled_chn_indices.end());
  }

  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), std::move(enabled)));
}

void SamplesHandler::SetTimeoutTaskOnThread(ClientData* client_data) {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());

  if (client_data->timeout == 0)
    return;

  sample_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SamplesHandler::SampleTimeout, weak_factory_.GetWeakPtr(),
                     client_data, clients_map_[client_data].sample_index),
      base::TimeDelta::FromMilliseconds(client_data->timeout));
}
void SamplesHandler::SampleTimeout(ClientData* client_data,
                                   uint64_t sample_index) {
  auto it = clients_map_.find(client_data);
  if (it == clients_map_.end() || it->second.sample_index != sample_index)
    return;

  ipc_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(on_error_occurred_callback_, client_data->id,
                                cros::mojom::ObserverErrorType::READ_TIMEOUT));
}

void SamplesHandler::OnSampleAvailableWithoutBlocking() {
  DCHECK(sample_task_runner_->BelongsToCurrentThread());
  DCHECK(num_read_failed_logs_ == 0 || num_read_failed_logs_recovery_ == 0);

  auto sample = iio_device_->ReadSample();
  if (!sample) {
    AddReadFailedLog();
    for (auto client : clients_map_) {
      ipc_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(on_error_occurred_callback_, client.first->id,
                         cros::mojom::ObserverErrorType::READ_FAILED));
    }

    return;
  }

  if (num_read_failed_logs_ == 0) {
    if (num_read_failed_logs_recovery_ > 0 &&
        ++num_read_failed_logs_recovery_ >= kNumReadFailedLogsRecovery) {
      LOGF(INFO) << "Resuming error logs";
      num_read_failed_logs_recovery_ = 0;
    }
  } else {
    --num_read_failed_logs_;
  }

  for (auto& client : clients_map_) {
    DCHECK(client.first->frequency >= kFrequencyEpsilon);
    DCHECK(!client.first->enabled_chn_indices.empty());

    int step =
        std::max(1, static_cast<int>(dev_frequency_ / client.first->frequency));

    // Update moving averages for channels
    for (int32_t chn_index : client.first->enabled_chn_indices) {
      if (no_batch_chn_indices.find(chn_index) != no_batch_chn_indices.end())
        continue;

      if (sample->find(chn_index) == sample->end()) {
        LOG(ERROR) << "Missing chn index: " << chn_index << " in sample";
        continue;
      }

      int size = samples_cnt_ - client.second.sample_index + 1;
      if (client.second.chns.find(chn_index) == client.second.chns.end() &&
          size != 1) {
        // A new enabled channel: fill up previous sample points with the
        // current value
        client.second.chns[chn_index] =
            sample.value()[chn_index] * (size * (size - 1) / 2);
      }

      client.second.chns[chn_index] += sample.value()[chn_index] * size;
    }

    if (client.second.sample_index + step - 1 <= samples_cnt_) {
      // Send a sample to the client
      int64_t size = samples_cnt_ - client.second.sample_index + 1;
      DCHECK_GE(size, 1);
      int64_t denom = ((size + 1) * size / 2);

      libmems::IioDevice::IioSample client_sample;
      for (int32_t chn_index : client.first->enabled_chn_indices) {
        if (sample->find(chn_index) == sample->end()) {
          LOG(ERROR) << "Missing chn: " << chn_index << " in sample";
          continue;
        }

        if (no_batch_chn_indices.find(chn_index) !=
            no_batch_chn_indices.end()) {
          // Use the current value directly
          client_sample[chn_index] = sample.value()[chn_index];
          continue;
        }

        if (client.second.chns.find(chn_index) == client.second.chns.end()) {
          LOG(ERROR) << "Missed chn index: " << chn_index
                     << " in moving averages";
          continue;
        }

        client_sample[chn_index] = client.second.chns[chn_index] / denom;
      }

      client.second.sample_index = samples_cnt_ + 1;
      client.second.chns.clear();

      ipc_task_runner_->PostTask(
          FROM_HERE,
          base::BindOnce(on_sample_updated_callback_, client.first->id,
                         std::move(client_sample)));
      SetTimeoutTaskOnThread(client.first);
    }
  }

  ++samples_cnt_;
}

void SamplesHandler::AddReadFailedLog() {
  if (num_read_failed_logs_recovery_ > 0) {
    if (++num_read_failed_logs_recovery_ >= kNumReadFailedLogsRecovery) {
      LOGF(INFO) << "Resuming error logs";
      num_read_failed_logs_recovery_ = 0;
    }

    return;
  }

  if (++num_read_failed_logs_ >= kNumReadFailedLogsBeforeGivingUp) {
    LOGF(ERROR) << "Too many read failed logs: Skipping logs until "
                << kNumReadFailedLogsRecovery << " reads are done";

    num_read_failed_logs_ = 0;
    num_read_failed_logs_recovery_ = 1;
    return;
  }

  LOGF(ERROR) << "Failed to read a sample";
}

}  // namespace iioservice
