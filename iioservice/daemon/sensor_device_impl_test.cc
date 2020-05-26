// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <set>
#include <utility>

#include <base/threading/thread.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include <libmems/common_types.h>
#include <libmems/test_fakes.h>

#include "iioservice/daemon/sensor_device_impl.h"
#include "iioservice/daemon/test_fakes.h"
#include "mojo/sensor.mojom.h"

namespace iioservice {

namespace {

constexpr char kDeviceAttrName[] = "FakeDeviceAttr";
constexpr char kDeviceAttrValue[] = "FakeDeviceAttrValue";

constexpr char kChnAttrName[] = "FakeChnAttr";
constexpr char kChnAttrValue[] = "FakeChnValue";

constexpr char kFakeDeviceName[] = "FakeDevice";
constexpr int kFakeDeviceId = 2;
constexpr char kFakeChannelId[] = "FakeChannel";
constexpr int kFakeChannelRawValue = 2020;

class SensorDeviceImplTest : public ::testing::Test {
 public:
  SensorDeviceImplTest()
      : ipc_thread_(std::make_unique<base::Thread>("IPCThread")),
        remote_thread_(std::make_unique<base::Thread>("PtrThread")) {}

  void SetTimeoutOnThread() {
    CHECK(remote_thread_->task_runner()->BelongsToCurrentThread());

    remote_->SetTimeout(0);
  }

  void GetAttributeOnThread() {
    CHECK(remote_thread_->task_runner()->BelongsToCurrentThread());

    remote_->GetAttribute(
        kDeviceAttrName,
        base::BindOnce([](const base::Optional<std::string>& value) {
          EXPECT_TRUE(value.has_value());
          EXPECT_EQ(value.value().compare(kDeviceAttrValue), 0);
        }));
  }

  void SetFrequencyOnThread() {
    CHECK(remote_thread_->task_runner()->BelongsToCurrentThread());

    remote_->SetFrequency(libmems::fakes::kFakeSamplingFrequency,
                          base::BindOnce([](double result_freq) {
                            EXPECT_EQ(result_freq,
                                      libmems::fakes::kFakeSamplingFrequency);
                          }));
  }

  void ReadSamplesOnThread(base::RepeatingClosure closure) {
    CHECK(remote_thread_->task_runner()->BelongsToCurrentThread());

    CHECK(remote_thread_->task_runner()->BelongsToCurrentThread());

    double frequency = libmems::fakes::kFakeSamplingFrequency;
    remote_->SetFrequency(frequency, base::BindOnce([](double result_freq) {
                            EXPECT_EQ(result_freq,
                                      libmems::fakes::kFakeSamplingFrequency);
                          }));

    remote_->SetChannelsEnabled(
        std::vector<int32_t>{0, 2, 3}, true,
        base::BindOnce([](const std::vector<int32_t>& failed_indices) {
          EXPECT_TRUE(failed_indices.empty());
        }));

    // No pause: setting pause_index_ to the size of fake data
    fake_observer_ = fakes::FakeSamplesObserver::Create(
        remote_thread_->task_runner(), std::move(closure), device_,
        std::multiset<std::pair<int, cros::mojom::ObserverErrorType>>(),
        frequency, frequency, frequency, frequency,
        base::size(libmems::fakes::kFakeAccelSamples));

    remote_->StartReadingSamples(fake_observer_->GetRemote());
  }

  void SetChannelsOnThread() {
    CHECK(remote_thread_->task_runner()->BelongsToCurrentThread());

    remote_->GetAllChannelIds(
        base::BindOnce([](const std::vector<std::string>& chn_ids) {
          EXPECT_EQ(chn_ids.size(), base::size(libmems::fakes::kFakeAccelChns));
          for (int i = 0; i < chn_ids.size(); ++i)
            EXPECT_EQ(chn_ids[i], libmems::fakes::kFakeAccelChns[i]);
        }));

    std::vector<int32_t> indices = {0, 2};
    remote_->SetChannelsEnabled(
        indices, true,
        base::BindOnce([](const std::vector<int32_t>& failed_indices) {
          EXPECT_TRUE(failed_indices.empty());
        }));

    indices.clear();
    for (int i = 0; i < base::size(libmems::fakes::kFakeAccelChns); ++i)
      indices.push_back(i);

    remote_->GetChannelsEnabled(
        indices, base::BindOnce([](const std::vector<bool>& enabled) {
          EXPECT_EQ(enabled.size(), base::size(libmems::fakes::kFakeAccelChns));
          for (int i = 0; i < enabled.size(); ++i)
            EXPECT_EQ(enabled[i], i % 2 == 0);
        }));
  }

  void GetChannelsAttributesOnThread() {
    CHECK(remote_thread_->task_runner()->BelongsToCurrentThread());

    std::vector<int32_t> indices;
    for (int i = 0; i < base::size(libmems::fakes::kFakeAccelChns); ++i)
      indices.push_back(i);

    remote_->GetChannelsAttributes(
        indices, kChnAttrName,
        base::BindOnce(
            [](const std::vector<base::Optional<std::string>>& values) {
              EXPECT_EQ(values.size(),
                        base::size(libmems::fakes::kFakeAccelChns));
              for (int i = 0; i < values.size(); ++i) {
                if (i % 2 == 0) {
                  EXPECT_TRUE(values[i].has_value());
                  EXPECT_EQ(values[i].value().compare(kChnAttrValue), 0);
                }
              }
            }));
  }

 protected:
  void SetUp() override {
    context_ = std::make_unique<libmems::fakes::FakeIioContext>();

    auto device = std::make_unique<libmems::fakes::FakeIioDevice>(
        nullptr, fakes::kAccelDeviceName, fakes::kAccelDeviceId);
    EXPECT_TRUE(device->WriteStringAttribute(
        kSamplingFrequencyAvailable, fakes::kFakeSamplingFrequencyAvailable));
    EXPECT_TRUE(device->WriteDoubleAttribute(libmems::kHWFifoTimeoutAttr, 0.0));
    EXPECT_TRUE(
        device->WriteStringAttribute(kDeviceAttrName, kDeviceAttrValue));

    auto chn_accel_x = std::make_unique<libmems::fakes::FakeIioChannel>(
        libmems::fakes::kFakeAccelChns[0], true);
    auto chn_accel_y = std::make_unique<libmems::fakes::FakeIioChannel>(
        libmems::fakes::kFakeAccelChns[1], true);
    auto chn_accel_z = std::make_unique<libmems::fakes::FakeIioChannel>(
        libmems::fakes::kFakeAccelChns[2], true);
    auto chn_timestamp = std::make_unique<libmems::fakes::FakeIioChannel>(
        libmems::fakes::kFakeAccelChns[3], true);

    chn_accel_x->WriteStringAttribute(kChnAttrName, kChnAttrValue);
    chn_accel_z->WriteStringAttribute(kChnAttrName, kChnAttrValue);

    device->AddChannel(std::move(chn_accel_x));
    device->AddChannel(std::move(chn_accel_y));
    device->AddChannel(std::move(chn_accel_z));
    device->AddChannel(std::move(chn_timestamp));

    device_ = device.get();
    context_->AddDevice(std::move(device));

    auto fake_device = std::make_unique<libmems::fakes::FakeIioDevice>(
        nullptr, kFakeDeviceName, kFakeDeviceId);
    EXPECT_TRUE(fake_device->WriteStringAttribute(
        kSamplingFrequencyAvailable, fakes::kFakeSamplingFrequencyAvailable));
    EXPECT_TRUE(
        fake_device->WriteDoubleAttribute(libmems::kHWFifoTimeoutAttr, 0.0));

    auto fake_chn =
        std::make_unique<libmems::fakes::FakeIioChannel>(kFakeChannelId, true);
    fake_chn->WriteNumberAttribute(libmems::kRawAttr, kFakeChannelRawValue);
    fake_device->AddChannel(std::move(fake_chn));

    fake_device_ = fake_device.get();
    context_->AddDevice(std::move(fake_device));

    message_loop_ = std::make_unique<base::MessageLoopForIO>();

    EXPECT_TRUE(ipc_thread_->Start());
    EXPECT_TRUE(remote_thread_->Start());

    ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
        ipc_thread_->task_runner(),
        mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
  }

  void TearDown() override {
    remote_thread_->task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&SensorDeviceImplTest::ResetRemote,
                                  base::Unretained(this)));
    remote_thread_->Stop();

    sensor_device_.reset();
    ipc_support_.reset();
    ipc_thread_->Stop();
    message_loop_.reset();
  }

  void SetupDevice() {
    base::RunLoop run_loop;
    ipc_thread_->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&SensorDeviceImplTest::CreateDeviceOnThread,
                       base::Unretained(this), run_loop.QuitClosure()));

    run_loop.Run();
  }

  void CreateDeviceOnThread(base::RepeatingClosure closure) {
    CHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

    sensor_device_ =
        SensorDeviceImpl::Create(ipc_thread_->task_runner(), context_.get());
    EXPECT_TRUE(sensor_device_);

    remote_thread_->task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&SensorDeviceImplTest::BindRemote,
                                  base::Unretained(this), std::move(closure)));
  }

  void BindRemote(base::RepeatingClosure closure) {
    CHECK(remote_thread_->task_runner()->BelongsToCurrentThread());

    sensor_device_->AddReceiver(
        fakes::kAccelDeviceId,
        remote_.BindNewPipeAndPassReceiver(remote_thread_->task_runner()));
    EXPECT_TRUE(remote_);
    closure.Run();
  }

  void ResetRemote() {
    CHECK(remote_thread_->task_runner()->BelongsToCurrentThread());

    remote_.reset();
  }

  std::unique_ptr<libmems::fakes::FakeIioContext> context_;
  libmems::fakes::FakeIioDevice* device_;
  libmems::fakes::FakeIioDevice* fake_device_;

  std::unique_ptr<base::MessageLoopForIO> message_loop_;

  std::unique_ptr<base::Thread> ipc_thread_;
  std::unique_ptr<base::Thread> remote_thread_;

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  SensorDeviceImpl::ScopedSensorDeviceImpl sensor_device_ = {
      nullptr, SensorDeviceImpl::SensorDeviceImplDeleter};

  mojo::Remote<cros::mojom::SensorDevice> remote_;
  fakes::FakeSamplesObserver::ScopedFakeSamplesObserver fake_observer_ = {
      nullptr, fakes::FakeSamplesObserver::ObserverDeleter};
};

TEST_F(SensorDeviceImplTest, SetTimeout) {
  SetupDevice();

  remote_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SensorDeviceImplTest::SetTimeoutOnThread,
                                base::Unretained(this)));
}

TEST_F(SensorDeviceImplTest, GetAttribute) {
  SetupDevice();

  remote_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SensorDeviceImplTest::GetAttributeOnThread,
                                base::Unretained(this)));
}

TEST_F(SensorDeviceImplTest, SetFrequency) {
  SetupDevice();

  remote_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SensorDeviceImplTest::SetFrequencyOnThread,
                                base::Unretained(this)));
}

TEST_F(SensorDeviceImplTest, ReadSamples) {
  SetupDevice();

  base::RunLoop run_loop;
  remote_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SensorDeviceImplTest::ReadSamplesOnThread,
                     base::Unretained(this), run_loop.QuitClosure()));
  run_loop.Run();
}

TEST_F(SensorDeviceImplTest, SetChannels) {
  SetupDevice();

  remote_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SensorDeviceImplTest::SetChannelsOnThread,
                                base::Unretained(this)));
}

TEST_F(SensorDeviceImplTest, GetChannelsAttributes) {
  SetupDevice();

  remote_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SensorDeviceImplTest::GetChannelsAttributesOnThread,
                     base::Unretained(this)));
}

}  // namespace

}  // namespace iioservice
