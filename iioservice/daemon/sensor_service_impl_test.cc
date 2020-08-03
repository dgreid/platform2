// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include <libmems/test_fakes.h>

#include "iioservice/daemon/sensor_service_impl.h"

namespace iioservice {

namespace {

constexpr char kFakeAccelName[] = "FakeAccel";
constexpr int kFakeAccelId = 1;
constexpr char kFakeAccelChnName[] = "accel_a";

constexpr char kFakeGyroName[] = "FakeGyro";
constexpr int kFakeGyroId = 2;
constexpr char kFakeGyroChnName[] = "anglvel_a";

class SensorServiceImplTest : public ::testing::Test {
 public:
  void GetDeviceIdsOnThread() {
    CHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

    sensor_service_->GetDeviceIds(
        cros::mojom::DeviceType::ACCEL,
        base::BindOnce([](const std::vector<int32_t>& iio_device_ids) {
          EXPECT_EQ(iio_device_ids.size(), 1);
          EXPECT_EQ(iio_device_ids[0], kFakeAccelId);
          LOG(INFO) << "done";
        }));
  }

  void GetAllDeviceIdsOnThread() {
    CHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

    sensor_service_->GetAllDeviceIds(base::BindOnce(
        [](const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
               iio_device_ids_types) {
          EXPECT_EQ(iio_device_ids_types.size(), 2);
          auto it_accel = iio_device_ids_types.find(kFakeAccelId);
          EXPECT_TRUE(it_accel != iio_device_ids_types.end());
          EXPECT_EQ(it_accel->second.size(), 1);
          EXPECT_EQ(it_accel->second[0], cros::mojom::DeviceType::ACCEL);

          auto it_gyro = iio_device_ids_types.find(kFakeGyroId);
          EXPECT_TRUE(it_gyro != iio_device_ids_types.end());
          EXPECT_EQ(it_gyro->second.size(), 1);
          EXPECT_EQ(it_gyro->second[0], cros::mojom::DeviceType::ANGLVEL);
          LOG(INFO) << "done";
        }));
  }

 protected:
  void SetUp() override {
    std::unique_ptr<libmems::fakes::FakeIioContext> context =
        std::make_unique<libmems::fakes::FakeIioContext>();

    auto accel = std::make_unique<libmems::fakes::FakeIioDevice>(
        nullptr, kFakeAccelName, kFakeAccelId);
    auto gyro = std::make_unique<libmems::fakes::FakeIioDevice>(
        nullptr, kFakeGyroName, kFakeGyroId);

    accel->AddChannel(std::make_unique<libmems::fakes::FakeIioChannel>(
        kFakeAccelChnName, true));
    gyro->AddChannel(std::make_unique<libmems::fakes::FakeIioChannel>(
        kFakeGyroChnName, true));

    context->AddDevice(std::move(accel));
    context->AddDevice(std::move(gyro));

    ipc_thread_ = std::make_unique<base::Thread>("IPCThread");
    EXPECT_TRUE(ipc_thread_->StartWithOptions(
        base::Thread::Options(base::MessageLoop::TYPE_IO, 0)));

    ipc_thread_->task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&SensorServiceImplTest::CreateServiceOnThread,
                                  base::Unretained(this), std::move(context)));
  }

  void TearDown() override {
    ipc_thread_->task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&SensorServiceImplTest::ResetServiceOnThread,
                                  base::Unretained(this)));
    ipc_thread_->Stop();
  }

  void CreateServiceOnThread(
      std::unique_ptr<libmems::fakes::FakeIioContext> context) {
    CHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

    sensor_service_ = SensorServiceImpl::Create(ipc_thread_->task_runner(),
                                                std::move(context));
    EXPECT_TRUE(sensor_service_);
  }

  void ResetServiceOnThread() {
    CHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

    sensor_service_.reset();
  }

  std::unique_ptr<base::Thread> ipc_thread_;

  SensorServiceImpl::ScopedSensorServiceImpl sensor_service_ = {
      nullptr, SensorServiceImpl::SensorServiceImplDeleter};
};

TEST_F(SensorServiceImplTest, GetDeviceIds) {
  ipc_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SensorServiceImplTest::GetDeviceIdsOnThread,
                                base::Unretained(this)));
}

TEST_F(SensorServiceImplTest, GetAllDeviceIds) {
  ipc_thread_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SensorServiceImplTest::GetAllDeviceIdsOnThread,
                                base::Unretained(this)));
}

class SensorServiceImplTestDeviceTypesWithParam
    : public ::testing::TestWithParam<
          std::pair<std::vector<std::string>,
                    std::vector<cros::mojom::DeviceType>>> {
 public:
  void GetAllDeviceIdsOnThread() {
    CHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

    sensor_service_->GetAllDeviceIds(base::BindOnce(
        [](const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
               iio_device_ids_types) {
          EXPECT_EQ(iio_device_ids_types.size(), 1);
          auto it = iio_device_ids_types.find(kFakeAccelId);
          EXPECT_TRUE(it != iio_device_ids_types.end());
          EXPECT_EQ(it->second.size(), GetParam().second.size());
          for (size_t i = 0; i < it->second.size(); ++i)
            EXPECT_EQ(it->second[i], GetParam().second[i]);
        }));
  }

 protected:
  void SetUp() override {
    std::unique_ptr<libmems::fakes::FakeIioContext> context =
        std::make_unique<libmems::fakes::FakeIioContext>();

    auto device = std::make_unique<libmems::fakes::FakeIioDevice>(
        nullptr, kFakeAccelName, kFakeAccelId);

    for (auto chn_id : GetParam().first) {
      device->AddChannel(
          std::make_unique<libmems::fakes::FakeIioChannel>(chn_id, true));
    }

    context->AddDevice(std::move(device));

    ipc_thread_ = std::make_unique<base::Thread>("IPCThread");
    EXPECT_TRUE(ipc_thread_->StartWithOptions(
        base::Thread::Options(base::MessageLoop::TYPE_IO, 0)));

    ipc_thread_->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &SensorServiceImplTestDeviceTypesWithParam::CreateServiceOnThread,
            base::Unretained(this), std::move(context)));
  }

  void TearDown() override {
    ipc_thread_->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(
            &SensorServiceImplTestDeviceTypesWithParam::ResetServiceOnThread,
            base::Unretained(this)));

    ipc_thread_->Stop();
  }

  void CreateServiceOnThread(
      std::unique_ptr<libmems::fakes::FakeIioContext> context) {
    CHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

    sensor_service_ = SensorServiceImpl::Create(ipc_thread_->task_runner(),
                                                std::move(context));
    EXPECT_TRUE(sensor_service_);
  }

  void ResetServiceOnThread() {
    CHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

    sensor_service_.reset();
  }

  std::unique_ptr<base::Thread> ipc_thread_;

  SensorServiceImpl::ScopedSensorServiceImpl sensor_service_ = {
      nullptr, SensorServiceImpl::SensorServiceImplDeleter};
};

TEST_P(SensorServiceImplTestDeviceTypesWithParam, DeviceTypes) {
  ipc_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &SensorServiceImplTestDeviceTypesWithParam::GetAllDeviceIdsOnThread,
          base::Unretained(this)));
}

INSTANTIATE_TEST_SUITE_P(
    SensorServiceImplTestDeviceTypesWithParamRun,
    SensorServiceImplTestDeviceTypesWithParam,
    ::testing::Values(std::pair<std::vector<std::string>,
                                std::vector<cros::mojom::DeviceType>>(
                          {"accel_x"}, {cros::mojom::DeviceType::ACCEL}),
                      std::pair<std::vector<std::string>,
                                std::vector<cros::mojom::DeviceType>>(
                          {"anglvel_y"}, {cros::mojom::DeviceType::ANGLVEL}),
                      std::pair<std::vector<std::string>,
                                std::vector<cros::mojom::DeviceType>>(
                          {"illuminance"}, {cros::mojom::DeviceType::LIGHT}),
                      std::pair<std::vector<std::string>,
                                std::vector<cros::mojom::DeviceType>>(
                          {"count"}, {cros::mojom::DeviceType::COUNT}),
                      std::pair<std::vector<std::string>,
                                std::vector<cros::mojom::DeviceType>>(
                          {"magn_z"}, {cros::mojom::DeviceType::MAGN}),
                      std::pair<std::vector<std::string>,
                                std::vector<cros::mojom::DeviceType>>(
                          {"angl"}, {cros::mojom::DeviceType::ANGL}),
                      std::pair<std::vector<std::string>,
                                std::vector<cros::mojom::DeviceType>>(
                          {"pressure"}, {cros::mojom::DeviceType::BARO}),
                      std::pair<std::vector<std::string>,
                                std::vector<cros::mojom::DeviceType>>(
                          {"accel_x", "accel_y", "anglvel_z", "abc"},
                          {cros::mojom::DeviceType::ACCEL,
                           cros::mojom::DeviceType::ANGLVEL}),
                      std::pair<std::vector<std::string>,
                                std::vector<cros::mojom::DeviceType>>(
                          {"accel", "anglvel", "illuminance_x", "count_y",
                           "magn", "angl_z", "pressure_x"},
                          {})));

}  // namespace

}  // namespace iioservice
