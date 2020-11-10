// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "power_manager/common/action_recorder.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/test_main_loop_runner.h"
#include "power_manager/powerd/system/sar_watcher.h"
#include "power_manager/powerd/system/udev_stub.h"
#include "power_manager/powerd/system/user_proximity_observer.h"

namespace power_manager {
namespace system {

namespace {

class TestObserver : public UserProximityObserver, public ActionRecorder {
 public:
  explicit TestObserver(SarWatcher* watcher, TestMainLoopRunner* runner)
      : watcher_(watcher), loop_runner_(runner) {
    watcher_->AddObserver(this);
  }
  TestObserver(const TestObserver&) = delete;
  TestObserver& operator=(const TestObserver&) = delete;

  ~TestObserver() override { watcher_->RemoveObserver(this); }

  // UserProximityObserver implementation:
  void OnNewSensor(int id, uint32_t roles) override {
    const auto action = base::StringPrintf("OnNewSensor(roles=0x%x)", roles);
    AppendAction(action);
  }
  void OnProximityEvent(int id, UserProximity value) override {
    const auto action = base::StringPrintf(
        "OnProximityEvent(value=%s)", UserProximityToString(value).c_str());
    AppendAction(action);
    loop_runner_->StopLoop();
  }

 private:
  SarWatcher* watcher_;              // Not owned.
  TestMainLoopRunner* loop_runner_;  // Not owned.
};

class SarWatcherTest : public testing::Test {
 public:
  SarWatcherTest() : sar_watcher_(std::make_unique<SarWatcher>()) {
    sar_watcher_->set_open_iio_events_func_for_testing(
        base::Bind(&SarWatcherTest::OpenTestIioFd, base::Unretained(this)));
  }

  void Init(uint32_t roles) {
    prefs_.SetInt64(kSetCellularTransmitPowerForProximityPref,
                    roles & UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);
    prefs_.SetInt64(
        kSetWifiTransmitPowerForProximityPref,
        roles & UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

    CHECK(sar_watcher_->Init(&prefs_, &udev_));
    observer_.reset(new TestObserver(sar_watcher_.get(), &loop_runner_));
  }

  ~SarWatcherTest() override {
    for (const auto& fd : fds_) {
      close(fd.second.first);
      close(fd.second.second);
    }
  }

  int GetNumOpenedSensors() const { return open_sensor_count_; }

  // Returns the "read" file descriptor.
  int OpenTestIioFd(const base::FilePath& file) {
    const std::string path(file.value());
    auto iter = fds_.find(path);
    if (iter != fds_.end())
      return iter->second.first;
    int fd[2];
    if (pipe2(fd, O_DIRECT | O_NONBLOCK) == -1)
      return -1;
    ++open_sensor_count_;
    fds_.emplace(path, std::make_pair(fd[0], fd[1]));
    return fd[0];
  }

  // Returns the "write" file descriptor.
  int GetWriteIioFd(std::string file) {
    auto iter = fds_.find(file);
    if (iter != fds_.end())
      return iter->second.second;
    return -1;
  }

 protected:
  void AddDevice(const std::string& syspath, const std::string& devlink) {
    UdevEvent iio_event;
    iio_event.action = UdevEvent::Action::ADD;
    iio_event.device_info.subsystem = SarWatcher::kIioUdevSubsystem;
    iio_event.device_info.devtype = SarWatcher::kIioUdevDevice;
    iio_event.device_info.sysname = "MOCKSENSOR";
    iio_event.device_info.syspath = syspath;
    udev_.AddSubsystemDevice(iio_event.device_info.subsystem,
                             iio_event.device_info, {devlink});
    udev_.NotifySubsystemObservers(iio_event);
  }

  void SendEvent(const std::string& devlink, UserProximity proximity) {
    int fd = GetWriteIioFd(devlink);
    if (fd == -1) {
      ADD_FAILURE() << devlink << " does not have a write fd";
      return;
    }
    uint8_t buf[16] = {0};
    buf[6] = (proximity == UserProximity::NEAR ? 2 : 1);
    if (sizeof(buf) != write(fd, &buf[0], sizeof(buf)))
      ADD_FAILURE() << "full buffer not written";
    loop_runner_.StartLoop(base::TimeDelta::FromSeconds(30));
  }

  std::unordered_map<std::string, std::pair<int, int>> fds_;
  FakePrefs prefs_;
  UdevStub udev_;
  std::unique_ptr<SarWatcher> sar_watcher_;
  TestMainLoopRunner loop_runner_;
  std::unique_ptr<TestObserver> observer_;
  int open_sensor_count_ = 0;
};

TEST_F(SarWatcherTest, DetectUsableWifiDevice) {
  Init(UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  AddDevice("/sys/mockproximity", "/dev/proximity-wifi-right");
  EXPECT_EQ(JoinActions("OnNewSensor(roles=0x1)", nullptr),
            observer_->GetActions());
  EXPECT_EQ(1, GetNumOpenedSensors());
}

TEST_F(SarWatcherTest, DetectUsableLteDevice) {
  Init(UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);

  AddDevice("/sys/mockproximity", "/dev/proximity-lte");
  EXPECT_EQ(JoinActions("OnNewSensor(roles=0x2)", nullptr),
            observer_->GetActions());
  EXPECT_EQ(1, GetNumOpenedSensors());
}

TEST_F(SarWatcherTest, DetectNotUsableWifiDevice) {
  Init(UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);

  AddDevice("/sys/mockproximity", "/dev/proximity-wifi-right");
  EXPECT_EQ(JoinActions(nullptr), observer_->GetActions());
  EXPECT_EQ(0, GetNumOpenedSensors());
}

TEST_F(SarWatcherTest, DetectNotUsableLteDevice) {
  Init(UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  AddDevice("/sys/mockproximity", "/dev/proximity-lte");
  EXPECT_EQ(JoinActions(nullptr), observer_->GetActions());
  EXPECT_EQ(0, GetNumOpenedSensors());
}

TEST_F(SarWatcherTest, DetectUsableMixDevice) {
  Init(UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  AddDevice("/sys/mockproximity", "/dev/proximity-wifi-lte");
  EXPECT_EQ(JoinActions("OnNewSensor(roles=0x1)", nullptr),
            observer_->GetActions());
  EXPECT_EQ(1, GetNumOpenedSensors());
}

TEST_F(SarWatcherTest, ReceiveProximityInfo) {
  Init(UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);

  AddDevice("/sys/mockproximity", "/dev/proximity-lte");
  observer_->GetActions();  // consume OnNewSensor
  SendEvent("/dev/proximity-lte", UserProximity::NEAR);
  EXPECT_EQ(JoinActions("OnProximityEvent(value=near)", nullptr),
            observer_->GetActions());
}

}  // namespace

}  // namespace system
}  // namespace power_manager
