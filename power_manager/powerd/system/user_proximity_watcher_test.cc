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
#include "power_manager/powerd/system/udev_stub.h"
#include "power_manager/powerd/system/user_proximity_observer.h"
#include "power_manager/powerd/system/user_proximity_watcher.h"

namespace power_manager {
namespace system {

namespace {

class TestObserver : public UserProximityObserver, public ActionRecorder {
 public:
  explicit TestObserver(UserProximityWatcher* watcher,
                        TestMainLoopRunner* runner)
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
  UserProximityWatcher* watcher_;    // Not owned.
  TestMainLoopRunner* loop_runner_;  // Not owned.
};

class UserProximityWatcherTest : public testing::Test {
 public:
  UserProximityWatcherTest()
      : user_proximity_watcher_(std::make_unique<UserProximityWatcher>()) {
    user_proximity_watcher_->set_open_iio_events_func_for_testing(base::Bind(
        &UserProximityWatcherTest::OpenTestIioFd, base::Unretained(this)));
  }

  void Init(UserProximityWatcher::SensorType type, uint32_t roles) {
    switch (type) {
      case UserProximityWatcher::SensorType::SAR:
        prefs_.SetInt64(
            kSetCellularTransmitPowerForProximityPref,
            roles & UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);
        prefs_.SetInt64(
            kSetWifiTransmitPowerForProximityPref,
            roles & UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);
        break;
      case UserProximityWatcher::SensorType::ACTIVITY:
        prefs_.SetInt64(
            kSetCellularTransmitPowerForActivityProximityPref,
            roles & UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);
        prefs_.SetInt64(
            kSetWifiTransmitPowerForActivityProximityPref,
            roles & UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);
        break;
      default:
        ADD_FAILURE() << "Unknown sensor type";
        return;
    }
    CHECK(user_proximity_watcher_->Init(&prefs_, &udev_));
    observer_.reset(
        new TestObserver(user_proximity_watcher_.get(), &loop_runner_));
  }

  ~UserProximityWatcherTest() override {
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
    iio_event.device_info.subsystem = UserProximityWatcher::kIioUdevSubsystem;
    iio_event.device_info.devtype = UserProximityWatcher::kIioUdevDevice;
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
  std::unique_ptr<UserProximityWatcher> user_proximity_watcher_;
  TestMainLoopRunner loop_runner_;
  std::unique_ptr<TestObserver> observer_;
  int open_sensor_count_ = 0;
};

TEST_F(UserProximityWatcherTest, DetectUsableWifiDevice) {
  Init(UserProximityWatcher::SensorType::SAR,
       UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  AddDevice("/sys/mockproximity", "/dev/proximity-wifi-right");
  EXPECT_EQ(JoinActions("OnNewSensor(roles=0x1)", nullptr),
            observer_->GetActions());
  EXPECT_EQ(1, GetNumOpenedSensors());
}

TEST_F(UserProximityWatcherTest, DetectUsableLteDevice) {
  Init(UserProximityWatcher::SensorType::SAR,
       UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);

  AddDevice("/sys/mockproximity", "/dev/proximity-lte");
  EXPECT_EQ(JoinActions("OnNewSensor(roles=0x2)", nullptr),
            observer_->GetActions());
  EXPECT_EQ(1, GetNumOpenedSensors());
}

TEST_F(UserProximityWatcherTest, DetectNotUsableWifiDevice) {
  Init(UserProximityWatcher::SensorType::SAR,
       UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);

  AddDevice("/sys/mockproximity", "/dev/proximity-wifi-right");
  EXPECT_EQ(JoinActions(nullptr), observer_->GetActions());
  EXPECT_EQ(0, GetNumOpenedSensors());
}

TEST_F(UserProximityWatcherTest, DetectNotUsableLteDevice) {
  Init(UserProximityWatcher::SensorType::SAR,
       UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  AddDevice("/sys/mockproximity", "/dev/proximity-lte");
  EXPECT_EQ(JoinActions(nullptr), observer_->GetActions());
  EXPECT_EQ(0, GetNumOpenedSensors());
}

TEST_F(UserProximityWatcherTest, DetectUsableMixDevice) {
  Init(UserProximityWatcher::SensorType::SAR,
       UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  AddDevice("/sys/mockproximity", "/dev/proximity-wifi-lte");
  EXPECT_EQ(JoinActions("OnNewSensor(roles=0x1)", nullptr),
            observer_->GetActions());
  EXPECT_EQ(1, GetNumOpenedSensors());
}

TEST_F(UserProximityWatcherTest, ReceiveProximityInfo) {
  Init(UserProximityWatcher::SensorType::SAR,
       UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);

  AddDevice("/sys/mockproximity", "/dev/proximity-lte");
  observer_->GetActions();  // consume OnNewSensor
  SendEvent("/dev/proximity-lte", UserProximity::NEAR);
  EXPECT_EQ(JoinActions("OnProximityEvent(value=near)", nullptr),
            observer_->GetActions());
}

TEST_F(UserProximityWatcherTest, UnknownDevice) {
  Init(UserProximityWatcher::SensorType::SAR,
       UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  AddDevice("/sys/mockunknown", "/dev/unknown-wifi-right");
  EXPECT_EQ(JoinActions(nullptr), observer_->GetActions());
  EXPECT_EQ(0, GetNumOpenedSensors());
}

TEST_F(UserProximityWatcherTest, DetectUsableActivityDevice) {
  Init(UserProximityWatcher::SensorType::ACTIVITY,
       UserProximityObserver::SensorRole::SENSOR_ROLE_WIFI);

  AddDevice("/sys/cros-ec-activity.6.auto/MOCKSENSOR", "/dev/MOCKSENSOR");
  EXPECT_EQ(JoinActions("OnNewSensor(roles=0x1)", nullptr),
            observer_->GetActions());
  EXPECT_EQ(1, GetNumOpenedSensors());
}

TEST_F(UserProximityWatcherTest, DetectNotUsableActivityDevice) {
  Init(UserProximityWatcher::SensorType::ACTIVITY,
       UserProximityObserver::SensorRole::SENSOR_ROLE_NONE);

  AddDevice("/sys/cros-ec-activity.6.auto/MOCKSENSOR", "/dev/MOCKSENSOR");
  EXPECT_EQ(JoinActions(nullptr), observer_->GetActions());
  EXPECT_EQ(0, GetNumOpenedSensors());
}

TEST_F(UserProximityWatcherTest, ReceiveActivityProximityInfo) {
  Init(UserProximityWatcher::SensorType::ACTIVITY,
       UserProximityObserver::SensorRole::SENSOR_ROLE_LTE);

  AddDevice("/sys/cros-ec-activity.6.auto/MOCKSENSOR", "/dev/MOCKSENSOR");
  observer_->GetActions();  // consume OnNewSensor
  SendEvent("/dev/MOCKSENSOR", UserProximity::NEAR);
  EXPECT_EQ(JoinActions("OnProximityEvent(value=near)", nullptr),
            observer_->GetActions());
}

}  // namespace

}  // namespace system
}  // namespace power_manager
