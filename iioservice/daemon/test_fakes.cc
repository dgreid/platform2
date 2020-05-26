// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/test_fakes.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>

#include <libmems/common_types.h>
#include <libmems/test_fakes.h>

#include "iioservice/include/constants.h"

namespace iioservice {

namespace fakes {

// static
FakeSamplesHandler::ScopedFakeSamplesHandler FakeSamplesHandler::CreateWithFifo(
    scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    libmems::fakes::FakeIioDevice* fake_iio_device,
    OnSampleUpdatedCallback on_sample_updated_callback,
    OnErrorOccurredCallback on_error_occurred_callback) {
  ScopedFakeSamplesHandler handler(nullptr, SamplesHandlerDeleter);
  double min_freq, max_freq;
  if (!GetDevMinMaxFrequency(fake_iio_device, &min_freq, &max_freq))
    return handler;

  handler.reset(new FakeSamplesHandler(
      std::move(ipc_task_runner), std::move(task_runner), fake_iio_device,
      min_freq, max_freq, std::move(on_sample_updated_callback),
      std::move(on_error_occurred_callback)));
  return handler;
}

void FakeSamplesHandler::ResumeReading() {
  sample_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&FakeSamplesHandler::ResumeReadingOnThread,
                                weak_factory_.GetWeakPtr()));
}

void FakeSamplesHandler::CheckRequestedFrequency(double max_freq) {
  sample_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&FakeSamplesHandler::CheckRequestedFrequencyOnThread,
                     weak_factory_.GetWeakPtr(), max_freq));
}

FakeSamplesHandler::FakeSamplesHandler(
    scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    libmems::fakes::FakeIioDevice* fake_iio_device,
    double min_freq,
    double max_freq,
    OnSampleUpdatedCallback on_sample_updated_callback,
    OnErrorOccurredCallback on_error_occurred_callback)
    : SamplesHandler(std::move(ipc_task_runner),
                     std::move(task_runner),
                     fake_iio_device,
                     min_freq,
                     max_freq,
                     std::move(on_sample_updated_callback),
                     std::move(on_error_occurred_callback)),
      fake_iio_device_(fake_iio_device) {}

void FakeSamplesHandler::ResumeReadingOnThread() {
  CHECK(sample_task_runner_->BelongsToCurrentThread());

  fake_iio_device_->ResumeReadingSamples();
}

void FakeSamplesHandler::CheckRequestedFrequencyOnThread(double max_freq) {
  CHECK(sample_task_runner_->BelongsToCurrentThread());

  CHECK_EQ(max_freq, requested_frequency_);
}

// static
void FakeSamplesObserver::ObserverDeleter(FakeSamplesObserver* observer) {
  if (observer == nullptr)
    return;

  if (!observer->ipc_task_runner_->BelongsToCurrentThread()) {
    observer->ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&FakeSamplesObserver::ObserverDeleter, observer));
    return;
  }

  delete observer;
}

// static
FakeSamplesObserver::ScopedFakeSamplesObserver FakeSamplesObserver::Create(
    scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
    base::Closure quit_closure,
    libmems::IioDevice* device,
    std::multiset<std::pair<int, cros::mojom::ObserverErrorType>> failures,
    double frequency,
    double frequency2,
    double dev_frequency,
    double dev_frequency2,
    int pause_index) {
  ScopedFakeSamplesObserver handler(
      new FakeSamplesObserver(std::move(ipc_task_runner),
                              std::move(quit_closure), device,
                              std::move(failures), frequency, frequency2,
                              dev_frequency, dev_frequency2, pause_index),
      ObserverDeleter);

  return handler;
}

void FakeSamplesObserver::OnSampleUpdated(
    const base::flat_map<int32_t, int64_t>& sample) {
  CHECK(ipc_task_runner_->BelongsToCurrentThread());
  CHECK(failures_.empty() || failures_.begin()->first > sample_index_);

  int step = GetStep();
  CHECK_GE(step, 1);

  CHECK_GT(base::size(libmems::fakes::kFakeAccelSamples),
           sample_index_ + step - 1);

  if (device_->GetId() == kAccelDeviceId) {
    for (int chnIndex = 0;
         chnIndex < base::size(libmems::fakes::kFakeAccelChns); ++chnIndex) {
      auto it = sample.find(chnIndex);

      // channel: accel_y isn't enabled before |pause_index_|
      if (sample_index_ + step - 1 < pause_index_ && chnIndex == 1) {
        CHECK(it == sample.end());
        continue;
      }

      CHECK(it != sample.end());

      CHECK_EQ(it->second,
               libmems::fakes::kFakeAccelSamples[sample_index_ + step - 1]
                                                [chnIndex]);
    }
  } else {
    auto channels = device_->GetAllChannels();
    for (size_t i = 0; i < channels.size(); ++i) {
      auto raw_value = channels[i]->ReadNumberAttribute(libmems::kRawAttr);
      if (!raw_value.has_value())
        continue;

      auto it = sample.find(i);
      CHECK(it != sample.end());
      CHECK_EQ(raw_value.value(), it->second);
    }
  }

  sample_index_ += step;

  if ((frequency2_ == 0.0 && sample_index_ + step - 1 >= pause_index_) ||
      sample_index_ + step - 1 >= base::size(libmems::fakes::kFakeAccelSamples))
    quit_closure_.Run();
}

void FakeSamplesObserver::OnErrorOccurred(cros::mojom::ObserverErrorType type) {
  CHECK(ipc_task_runner_->BelongsToCurrentThread());

  CHECK(!failures_.empty());
  CHECK_LE(failures_.begin()->first, sample_index_ + GetStep());
  CHECK_EQ(failures_.begin()->second, type);

  failures_.erase(failures_.begin());

  if (type == cros::mojom::ObserverErrorType::FREQUENCY_INVALID) {
    CHECK_EQ(sample_index_, 0);
    if (frequency2_ == 0.0) {
      sample_index_ = base::size(libmems::fakes::kFakeAccelSamples);
      quit_closure_.Run();

      return;
    }

    while (!failures_.empty() && failures_.begin()->first < pause_index_) {
      if (failures_.begin()->second ==
          cros::mojom::ObserverErrorType::READ_TIMEOUT)
        quit_closure_.Run();

      failures_.erase(failures_.begin());
    }

    sample_index_ = pause_index_;

    return;
  }

  if (type == cros::mojom::ObserverErrorType::READ_TIMEOUT)
    quit_closure_.Run();
}

FakeSamplesObserver::FakeSamplesObserver(
    scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
    base::Closure quit_closure,
    libmems::IioDevice* device,
    std::multiset<std::pair<int, cros::mojom::ObserverErrorType>> failures,
    double frequency,
    double frequency2,
    double dev_frequency,
    double dev_frequency2,
    int pause_index)
    : ipc_task_runner_(ipc_task_runner),
      quit_closure_(quit_closure),
      device_(device),
      failures_(std::move(failures)),
      frequency_(frequency),
      frequency2_(frequency2),
      dev_frequency_(dev_frequency),
      dev_frequency2_(dev_frequency2),
      pause_index_(pause_index),
      receiver_(this) {
  CHECK_GE(frequency_, 0.0);
  CHECK_GE(frequency2_, 0.0);
  CHECK_GE(dev_frequency_, kFrequencyEpsilon);
  CHECK_GE(dev_frequency2_, kFrequencyEpsilon);
}

int FakeSamplesObserver::GetStep() {
  CHECK_GE(dev_frequency_, kFrequencyEpsilon);

  int step = base::size(libmems::fakes::kFakeAccelSamples);
  if (frequency_ >= kFrequencyEpsilon)
    step = dev_frequency_ / frequency_;

  if (sample_index_ + step - 1 < pause_index_)
    return step;

  if (frequency2_ < kFrequencyEpsilon)
    return base::size(libmems::fakes::kFakeAccelSamples);

  int step2 = dev_frequency2_ / frequency2_;

  return std::max(pause_index_ - sample_index_ + 1, step2);
}

}  // namespace fakes

}  // namespace iioservice
