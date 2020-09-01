// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sys/eventfd.h>

#include <memory>
#include <utility>

#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <brillo/udev/mock_udev_monitor.h>
#include <libmems/iio_device_impl.h>

#include "iioservice/daemon/udev_watcher.h"

using testing::ByMove;
using testing::Return;
using testing::StrEq;

namespace iioservice {

namespace {

constexpr char kSubsystemString[] = "iio";
constexpr char kDeviceTypeString[] = "iio_device";

constexpr int kFakeDeviceId = 1;
constexpr char kFakeDeviceIdInString[] = "iio:device1";

class MockObserver : public UdevWatcher::Observer {
 public:
  MOCK_METHOD(void, OnDeviceAdded, (int), (override));
};

class UdevWatcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    udev_uniq_.reset(new brillo::MockUdev());
    udev_ = udev_uniq_.get();

    udev_monitor_uniq_.reset(new brillo::MockUdevMonitor());
    udev_monitor_ = udev_monitor_uniq_.get();

    udev_device_uniq_.reset(new brillo::MockUdevDevice());
    udev_device_ = udev_device_uniq_.get();
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO};

  brillo::MockUdev* udev_;
  std::unique_ptr<brillo::MockUdev> udev_uniq_;

  brillo::MockUdevMonitor* udev_monitor_;
  std::unique_ptr<brillo::MockUdevMonitor> udev_monitor_uniq_;

  brillo::MockUdevDevice* udev_device_;
  std::unique_ptr<brillo::MockUdevDevice> udev_device_uniq_;

  MockObserver observer_;
  std::unique_ptr<UdevWatcher> udev_watcher_;
};

TEST_F(UdevWatcherTest, SetUdev) {
  EXPECT_CALL(*udev_, CreateMonitorFromNetlink(StrEq("udev")))
      .WillOnce(Return(ByMove(std::move(udev_monitor_uniq_))));
  EXPECT_CALL(*udev_monitor_,
              FilterAddMatchSubsystemDeviceType(StrEq(kSubsystemString),
                                                StrEq(kDeviceTypeString)))
      .WillOnce(Return(true));
  EXPECT_CALL(*udev_monitor_, EnableReceiving()).WillOnce(Return(true));

  int fd = eventfd(0, 0);
  EXPECT_GE(fd, 0);

  EXPECT_CALL(*udev_monitor_, GetFileDescriptor()).WillOnce(Return(fd));

  udev_watcher_ = UdevWatcher::Create(&observer_, std::move(udev_uniq_));

  EXPECT_CALL(*udev_monitor_, ReceiveDevice())
      .WillOnce(Return(ByMove(std::move(udev_device_uniq_))));

  EXPECT_CALL(*udev_device_, GetAction()).WillOnce([&fd]() {
    int64_t val = 1;
    EXPECT_EQ(read(fd, &val, sizeof(uint64_t)), sizeof(uint64_t));
    return "add";
  });

  EXPECT_CALL(*udev_device_, GetSysName())
      .WillOnce(Return(kFakeDeviceIdInString));

  uint64_t val = 1;
  EXPECT_EQ(write(fd, &val, sizeof(uint64_t)), sizeof(uint64_t));

  EXPECT_CALL(observer_, OnDeviceAdded(kFakeDeviceId)).Times(1);

  // Wait until |MockObserver::OnReadable| is called.
  base::RunLoop().RunUntilIdle();
}

}  // namespace

}  // namespace iioservice
