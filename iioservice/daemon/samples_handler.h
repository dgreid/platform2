// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_SAMPLES_HANDLER_H_
#define IIOSERVICE_DAEMON_SAMPLES_HANDLER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/weak_ptr.h>
#include <libmems/iio_context.h>
#include <libmems/iio_device.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "iioservice/daemon/common_types.h"
#include "mojo/sensor.mojom.h"

namespace iioservice {

// A SamplesHandler is a handler of an IioDevice's samples. The user should add,
// remove, and update clients with frequencies and channels, and this handler
// will dispatch samples with clients' desired frequencies and channels when
// samples are received from the kernel.
// The user can provide the same |sample_task_runner| to all SamplesHandler as
// there is no blocking function in SamplesHandler and the thread would not be
// heavily loaded.
class SamplesHandler {
 public:
  static void SamplesHandlerDeleter(SamplesHandler* handler);
  using ScopedSamplesHandler =
      std::unique_ptr<SamplesHandler, decltype(&SamplesHandlerDeleter)>;

  using OnSampleUpdatedCallback = base::RepeatingCallback<void(
      mojo::ReceiverId, libmems::IioDevice::IioSample)>;
  using OnErrorOccurredCallback = base::RepeatingCallback<void(
      mojo::ReceiverId, cros::mojom::ObserverErrorType)>;

  static bool DisableBufferAndEnableChannels(libmems::IioDevice* iio_device);
  static ScopedSamplesHandler Create(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> sample_task_runner,
      libmems::IioDevice* iio_device,
      OnSampleUpdatedCallback on_sample_updated_callback,
      OnErrorOccurredCallback on_error_occurred_callback);

  virtual ~SamplesHandler();

  // It's the user's responsibility to maintain |client_data| before being
  // removed or this class being destructed.
  // |client_data.iio_device| should be the same as |iio_device_|.
  void AddClient(ClientData* client_data);
  void RemoveClient(ClientData* client_data);

  void UpdateFrequency(
      ClientData* client_data,
      double frequency,
      cros::mojom::SensorDevice::SetFrequencyCallback callback);
  void UpdateChannelsEnabled(
      ClientData* client_data,
      const std::vector<int32_t>& iio_chn_indices,
      bool en,
      cros::mojom::SensorDevice::SetChannelsEnabledCallback callback);

  void GetChannelsEnabled(
      ClientData* client_data,
      const std::vector<int32_t>& iio_chn_indices,
      cros::mojom::SensorDevice::GetChannelsEnabledCallback callback);

 protected:
  struct SampleData {
    // The starting index of the next sample.
    uint64_t sample_index = 0;
    // Moving averages of channels except for channels that have no batch mode
    std::map<int32_t, int64_t> chns;
  };

  static const uint32_t kNumReadFailedLogsBeforeGivingUp = 100;
  static const uint32_t kNumReadFailedLogsRecovery = 10000;

  // use fifo
  SamplesHandler(scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
                 scoped_refptr<base::SingleThreadTaskRunner> sample_task_runner,
                 libmems::IioDevice* iio_device,
                 double min_freq,
                 double max_freq,
                 OnSampleUpdatedCallback on_sample_updated_callback,
                 OnErrorOccurredCallback on_error_occurred_callback);

  void SetSampleWatcherOnThread();
  void StartAcceptingSamples(
      base::FileDescriptorWatcher::Controller* ignored_watcher);
  void StopSampleWatcherOnThread();

  double FixFrequency(double frequency);

  void AddActiveClientOnThread(ClientData* client_data);
  void AddClientOnThread(ClientData* client_data);

  void RemoveActiveClientOnThread(ClientData* client_data, double orig_freq);
  void RemoveClientOnThread(ClientData* client_data);

  void UpdateFrequencyOnThread(
      ClientData* client_data,
      double frequency,
      cros::mojom::SensorDevice::SetFrequencyCallback callback);

  bool AddFrequencyOnThread(double frequency);
  bool RemoveFrequencyOnThread(double frequency);

  bool UpdateRequestedFrequencyOnThread(double frequency);

  void UpdateChannelsEnabledOnThread(
      ClientData* client_data,
      const std::vector<int32_t>& iio_chn_indices,
      bool en,
      cros::mojom::SensorDevice::SetChannelsEnabledCallback callback);

  void GetChannelsEnabledOnThread(
      ClientData* client_data,
      const std::vector<int32_t>& iio_chn_indices,
      cros::mojom::SensorDevice::GetChannelsEnabledCallback callback);

  void SetTimeoutTaskOnThread(ClientData* client_data);
  void SampleTimeout(ClientData* client_data, uint64_t sample_index);

  void OnSampleAvailableWithoutBlocking();
  void AddReadFailedLog();

  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> sample_task_runner_;
  libmems::IioDevice* iio_device_;

  // Clients that either have invalid frequency or no enabled channels.
  std::set<ClientData*> inactive_clients_;
  // First is the active client, second is its data.
  std::map<ClientData*, SampleData> clients_map_;

  // Requested frequencies from clients.
  std::multiset<double> frequencies_;
  // Max frequency among |frequencies_|.
  double requested_frequency_ = 0.0;
  // The real device frequency. Given the kernel is requesting upsampling,
  // |dev_frequency_| >= |requested_frequency_|.
  double dev_frequency_ = 0.0;

  double dev_min_frequency_ = 0.0;
  double dev_max_frequency_ = 0.0;

  // The next coming sample's id. 0-based.
  // Shouldn't overflow as timestamp will overflow first.
  uint64_t samples_cnt_ = 0;

  uint32_t num_read_failed_logs_ = 0;
  uint32_t num_read_failed_logs_recovery_ = 0;

  base::RepeatingCallback<void(mojo::ReceiverId, libmems::IioDevice::IioSample)>
      on_sample_updated_callback_;
  base::RepeatingCallback<void(mojo::ReceiverId,
                               cros::mojom::ObserverErrorType)>
      on_error_occurred_callback_;

  std::set<int32_t> no_batch_chn_indices;

  base::FileDescriptorWatcher::Controller* ignored_watcher_ = nullptr;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

 private:
  base::WeakPtrFactory<SamplesHandler> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_SAMPLES_HANDLER_H_
