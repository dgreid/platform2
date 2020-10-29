// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/rand_util.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <libmems/common_types.h>
#include <libmems/test_fakes.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "iioservice/daemon/samples_handler.h"
#include "iioservice/daemon/test_fakes.h"
#include "mojo/sensor.mojom.h"

namespace iioservice {

namespace {

constexpr double kMinFrequency = 1.25;
constexpr double kMaxFrequency = 40.0;

constexpr double kFooFrequency = 20.0;
constexpr int kNumFailures = 10;

double FixFrequency(double frequency) {
  if (frequency < libmems::kFrequencyEpsilon)
    return 0.0;

  if (frequency < kMinFrequency)
    return kMinFrequency;

  if (frequency > kMaxFrequency)
    return kMaxFrequency;

  return frequency;
}

class SamplesHandlerTestBase {
 protected:
  void OnSampleUpdatedCallback(mojo::ReceiverId id,
                               libmems::IioDevice::IioSample sample) {
    CHECK(
        task_environment_.GetMainThreadTaskRunner()->BelongsToCurrentThread());
    CHECK_LT(id, observers_.size());

    observers_[id]->OnSampleUpdated(std::move(sample));
  }
  void OnErrorOccurredCallback(mojo::ReceiverId id,
                               cros::mojom::ObserverErrorType type) {
    CHECK(
        task_environment_.GetMainThreadTaskRunner()->BelongsToCurrentThread());
    if (id >= observers_.size()) {
      CHECK_EQ(type, cros::mojom::ObserverErrorType::FREQUENCY_INVALID);
      return;
    }

    observers_[id]->OnErrorOccurred(type);
  }

  void SetUpBase() {
    device_ = std::make_unique<libmems::fakes::FakeIioDevice>(
        nullptr, fakes::kAccelDeviceName, fakes::kAccelDeviceId);
    EXPECT_TRUE(
        device_->WriteStringAttribute(libmems::kSamplingFrequencyAvailable,
                                      fakes::kFakeSamplingFrequencyAvailable));

    for (int i = 0; i < base::size(libmems::fakes::kFakeAccelChns); ++i) {
      device_->AddChannel(std::make_unique<libmems::fakes::FakeIioChannel>(
          libmems::fakes::kFakeAccelChns[i], true));
    }

    EXPECT_TRUE(
        device_->WriteDoubleAttribute(libmems::kSamplingFrequencyAttr, 0.0));

    handler_ = fakes::FakeSamplesHandler::CreateWithFifo(
        task_environment_.GetMainThreadTaskRunner(),
        task_environment_.GetMainThreadTaskRunner(), device_.get(),
        base::BindRepeating(&SamplesHandlerTestBase::OnSampleUpdatedCallback,
                            base::Unretained(this)),
        base::BindRepeating(&SamplesHandlerTestBase::OnErrorOccurredCallback,
                            base::Unretained(this)));
    EXPECT_TRUE(handler_);
  }

  void TearDownBase() {
    handler_.reset();
    observers_.clear();

    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(device_->ReadDoubleAttribute(libmems::kSamplingFrequencyAttr)
                  .value_or(-1),
              0.0);
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO};

  std::unique_ptr<libmems::fakes::FakeIioDevice> device_;

  fakes::FakeSamplesHandler::ScopedFakeSamplesHandler handler_ = {
      nullptr, SamplesHandler::SamplesHandlerDeleter};
  std::vector<ClientData> clients_data_;
  std::vector<std::unique_ptr<fakes::FakeSamplesObserver>> observers_;
};

class SamplesHandlerTest : public ::testing::Test,
                           public SamplesHandlerTestBase {
 protected:
  void SetUp() override { SetUpBase(); }

  void TearDown() override { TearDownBase(); }
};

// Add clients with only timestamp channel enabled, enable all other channels,
// and disable all channels except for accel_x. Enabled channels are checked
// after each modification.
TEST_F(SamplesHandlerTest, UpdateChannelsEnabled) {
  // No samples in this test
  device_->SetPauseCallbackAtKthSamples(0, base::BindOnce([]() {}));

  std::vector<double> freqs = {0.0, 10.0};
  clients_data_.resize(freqs.size());
  for (size_t i = 0; i < freqs.size(); ++i) {
    ClientData& client_data = clients_data_[i];

    client_data.id = i;
    client_data.iio_device = device_.get();
    // At least one channel enabled
    client_data.enabled_chn_indices.emplace(3);  // timestamp
    client_data.timeout = 0;
    client_data.frequency = freqs[i];

    handler_->AddClient(&client_data);

    handler_->UpdateChannelsEnabled(
        &client_data, std::vector<int32_t>{1, 0, 2}, true,
        base::BindOnce([](const std::vector<int32_t>& failed_indices) {
          EXPECT_TRUE(failed_indices.empty());
        }));

    handler_->GetChannelsEnabled(
        &client_data, std::vector<int32_t>{1, 0, 2, 3},
        base::BindOnce([](const std::vector<bool>& enabled) {
          EXPECT_EQ(enabled.size(), 4);
          EXPECT_TRUE(enabled[0] && enabled[1] && enabled[2] && enabled[3]);
        }));

    handler_->UpdateChannelsEnabled(
        &client_data, std::vector<int32_t>{2, 1, 3}, false,
        base::BindOnce(
            [](ClientData* client_data, int32_t chn_index,
               const std::vector<int32_t>& failed_indices) {
              EXPECT_EQ(client_data->enabled_chn_indices.size(), 1);
              EXPECT_EQ(*client_data->enabled_chn_indices.begin(), chn_index);
              EXPECT_TRUE(failed_indices.empty());
            },
            &client_data, 0));
  }

  // Remove clients
  for (auto& client_data : clients_data_)
    handler_->RemoveClient(&client_data);
}

TEST_F(SamplesHandlerTest, BadDeviceWithNoSamples) {
  device_->DisableFd();

  std::vector<double> freqs = {5.0, 0.0, 10.0, 100.0};
  clients_data_.resize(freqs.size());

  size_t fd_failed_cnt = 0;
  for (size_t i = 0; i < freqs.size(); ++i) {
    if (freqs[i] > 0.0)
      ++fd_failed_cnt;
  }

  for (size_t i = 0; i < freqs.size(); ++i) {
    ClientData& client_data = clients_data_[i];

    client_data.id = i;
    client_data.iio_device = device_.get();
    // At least one channel enabled
    client_data.enabled_chn_indices.emplace(3);  // timestamp
    client_data.frequency = freqs[i];
    client_data.timeout = 0;

    std::multiset<std::pair<int, cros::mojom::ObserverErrorType>> failures;
    if (freqs[i] == 0.0) {
      failures.insert(
          std::make_pair(0, cros::mojom::ObserverErrorType::FREQUENCY_INVALID));
    } else {
      for (size_t j = 0; j < fd_failed_cnt; ++j) {
        failures.insert(
            std::make_pair(0, cros::mojom::ObserverErrorType::GET_FD_FAILED));
      }
      --fd_failed_cnt;
    }

    // Don't care about frequencies as there would be no samples.
    auto fake_observer = fakes::FakeSamplesObserver::Create(
        device_.get(), std::move(failures), freqs[i], freqs[i], kFooFrequency,
        kFooFrequency);

    handler_->AddClient(&client_data);

    observers_.emplace_back(std::move(fake_observer));
  }

  // Wait until all observers receive all samples
  base::RunLoop().RunUntilIdle();

  for (const auto& observer : observers_)
    EXPECT_TRUE(observer->NoRemainingFailures());

  // Remove clients
  for (auto& client_data : clients_data_)
    handler_->RemoveClient(&client_data);
}

class SamplesHandlerTestWithParam
    : public ::testing::TestWithParam<std::vector<std::pair<double, double>>>,
      public SamplesHandlerTestBase {
 protected:
  void SetUp() override { SetUpBase(); }

  void TearDown() override { TearDownBase(); }
};

// Add clients with the first frequencies set, update clients with the second
// frequencies, and remove clients. Clients' frequencies and the sample
// handler's |max_frequency_| are checked after each modification.
TEST_P(SamplesHandlerTestWithParam, UpdateFrequency) {
  // No samples in this test
  device_->SetPauseCallbackAtKthSamples(0, base::BindOnce([]() {}));

  clients_data_.resize(GetParam().size());

  std::multiset<double> frequencies;

  // Add clients
  for (size_t i = 0; i < GetParam().size(); ++i) {
    ClientData& client_data = clients_data_[i];

    client_data.id = i;
    client_data.iio_device = device_.get();
    // At least one channel enabled
    client_data.enabled_chn_indices.emplace(3);  // timestamp
    client_data.timeout = 0;
    client_data.frequency = GetParam()[i].first;

    handler_->AddClient(&client_data);

    frequencies.emplace(FixFrequency(client_data.frequency));
    handler_->CheckRequestedFrequency(*frequencies.rbegin());
  }

  // Update clients' frequencies
  for (size_t i = 0; i < GetParam().size(); ++i) {
    ClientData& client_data = clients_data_[i];

    double new_freq = GetParam()[i].second;
    handler_->UpdateFrequency(
        &client_data, new_freq,
        base::BindOnce(
            [](ClientData* client_data, double fixed_new_freq,
               double result_freq) {
              EXPECT_EQ(client_data->frequency, fixed_new_freq);
              EXPECT_EQ(result_freq, fixed_new_freq);
            },
            &client_data, FixFrequency(new_freq)));

    auto it = frequencies.find(FixFrequency(GetParam()[i].first));
    EXPECT_TRUE(it != frequencies.end());
    frequencies.erase(it);
    frequencies.emplace(FixFrequency(new_freq));

    handler_->CheckRequestedFrequency(*frequencies.rbegin());
  }

  // Remove clients
  for (size_t i = 0; i < GetParam().size(); ++i) {
    ClientData& client_data = clients_data_[i];

    handler_->RemoveClient(&client_data);
    auto it = frequencies.find(FixFrequency(GetParam()[i].second));
    EXPECT_TRUE(it != frequencies.end());
    frequencies.erase(it);

    handler_->CheckRequestedFrequency(
        i == GetParam().size() - 1 ? 0.0 : *frequencies.rbegin());
  }
}

// Add all clients into the sample handler, read the first |kPauseIndex|
// samples and pause reading, update clients' frequencies and enable accel_y
// channel, and read the rest samples. All samples are checked when received by
// observers.
TEST_P(SamplesHandlerTestWithParam, ReadSamplesWithFrequency) {
  // Set the pause in the beginning to prevent reading samples before all
  // clients added.
  device_->SetPauseCallbackAtKthSamples(0, base::BindOnce([]() {}));

  std::multiset<std::pair<int, cros::mojom::ObserverErrorType>> rf_failures;
  for (int i = 0; i < kNumFailures; ++i) {
    int k = base::RandInt(0, base::size(libmems::fakes::kFakeAccelSamples) - 1);

    device_->AddFailedReadAtKthSample(k);
    rf_failures.insert(
        std::make_pair(k, cros::mojom::ObserverErrorType::READ_FAILED));
  }

  clients_data_.resize(GetParam().size());

  double max_freq = -1, max_freq2 = -1;
  for (size_t i = 0; i < GetParam().size(); ++i) {
    max_freq = std::max(max_freq, GetParam()[i].first);
    max_freq2 = std::max(max_freq2, GetParam()[i].second);
  }

  max_freq = FixFrequency(max_freq);
  max_freq2 = FixFrequency(max_freq2);

  for (size_t i = 0; i < GetParam().size(); ++i) {
    ClientData& client_data = clients_data_[i];

    client_data.id = i;
    client_data.iio_device = device_.get();
    client_data.enabled_chn_indices.emplace(0);  // accel_x
    client_data.enabled_chn_indices.emplace(2);  // accel_z
    client_data.enabled_chn_indices.emplace(3);  // timestamp
    client_data.frequency = GetParam()[i].first;

    auto failures = rf_failures;
    if (GetParam()[i].first == 0.0) {
      while (!failures.empty() && failures.begin()->first < fakes::kPauseIndex)
        failures.erase(failures.begin());

      failures.insert(
          std::make_pair(0, cros::mojom::ObserverErrorType::FREQUENCY_INVALID));
    }

    // The fake observer needs |max_freq| and |max_freq2| to calculate the
    // correct values of samples
    auto fake_observer = fakes::FakeSamplesObserver::Create(
        device_.get(), std::move(failures), FixFrequency(GetParam()[i].first),
        FixFrequency(GetParam()[i].second), max_freq, max_freq2);

    handler_->AddClient(&client_data);

    observers_.emplace_back(std::move(fake_observer));
  }

  base::RunLoop().RunUntilIdle();

  device_->SetPauseCallbackAtKthSamples(
      fakes::kPauseIndex,
      base::BindOnce(
          [](fakes::FakeSamplesHandler* handler,
             std::vector<ClientData>* clients_data,
             libmems::fakes::FakeIioDevice* device) {
            for (int i = 0; i < clients_data->size(); ++i) {
              ClientData& client_data = clients_data->at(i);

              // Update to the second frequency
              handler->UpdateFrequency(
                  &client_data, GetParam()[i].second,
                  base::BindOnce(
                      [](double fixed_new_freq, double result_freq) {
                        EXPECT_EQ(result_freq, fixed_new_freq);
                      },
                      FixFrequency(GetParam()[i].second)));

              // Enable accel_y
              handler->UpdateChannelsEnabled(
                  &client_data, std::vector<int32_t>{1}, true,
                  base::BindOnce(
                      [](const std::vector<int32_t>& failed_indices) {
                        EXPECT_TRUE(failed_indices.empty());
                      }));
            }

            handler->ResumeReading();  // Read the rest samples
          },
          handler_.get(), &clients_data_, device_.get()));

  handler_->ResumeReading();  // Start reading |kPauseIndex| samples

  // Wait until all observers receive all samples
  base::RunLoop().RunUntilIdle();

  for (const auto& observer : observers_)
    EXPECT_TRUE(observer->FinishedObserving());

  // Remove clients
  for (auto& client_data : clients_data_)
    handler_->RemoveClient(&client_data);
}

INSTANTIATE_TEST_SUITE_P(
    SamplesHandlerTestWithParamRun,
    SamplesHandlerTestWithParam,
    ::testing::Values(std::vector<std::pair<double, double>>(3, {10.0, 10.0}),
                      std::vector<std::pair<double, double>>{
                          {20.0, 50.0}, {10.0, 10.0}, {2.0, 3.0}},
                      std::vector<std::pair<double, double>>{
                          {10.0, 20.0}, {20.0, 30.0}, {0.0, 0.0}},
                      std::vector<std::pair<double, double>>{
                          {80.0, 50.0}, {10.0, 10.0}, {2.0, 3.0}},
                      std::vector<std::pair<double, double>>{
                          {10.0, 40.0}, {0.0, 20.0}, {2.0, 3.0}, {40.0, 10.0}},
                      std::vector<std::pair<double, double>>{
                          {2.0, 10.0}, {10.0, 30.0}, {80.0, 0.0}},
                      std::vector<std::pair<double, double>>{
                          {0.0, 10.0}, {10.0, 30.0}, {80.0, 60.0}},
                      std::vector<std::pair<double, double>>{
                          {2.0, 10.0}, {50.0, 30.0}, {80.0, 60.0}},
                      std::vector<std::pair<double, double>>{{20.0, 30.0},
                                                             {10.0, 10.0}}));

}  // namespace

}  // namespace iioservice
