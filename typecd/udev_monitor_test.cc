// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/udev_monitor.h"

#include <base/files/file_util.h>
#include <base/test/task_environment.h>
#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_device.h>
#include <brillo/udev/mock_udev_enumerate.h>
#include <brillo/udev/mock_udev_list_entry.h>
#include <brillo/udev/mock_udev_monitor.h>
#include <brillo/unittest_utils.h>
#include <gtest/gtest.h>

#include "typecd/test_constants.h"

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::StrEq;

namespace typecd {

namespace {

constexpr char kInvalidPortSysPath[] = "/sys/class/typec/a-yz";

// A really dumb observer to verify that UdevMonitor is invoking the right
// callbacks.
class TestObserver : public UdevMonitor::Observer {
 public:
  void OnPortAddedOrRemoved(const base::FilePath& path,
                            int port_num,
                            bool added) override {
    if (added)
      num_ports_++;
    else
      num_ports_--;
  };

  void OnPartnerAddedOrRemoved(const base::FilePath& path,
                               int port_num,
                               bool added) override {
    if (added)
      num_partners_++;
    else
      num_partners_--;
  };

  void OnPartnerAltModeAddedOrRemoved(const base::FilePath& path,
                                      int port_num,
                                      bool added) override{};

  void OnCableAddedOrRemoved(const base::FilePath& path,
                             int port_num,
                             bool added) override {
    if (added)
      num_cables_++;
    else
      num_cables_--;
  };

  void OnCableAltModeAdded(const base::FilePath& path, int port_num) override {
    num_cable_altmodes_++;
  };

  int GetNumPorts() { return num_ports_; }
  int GetNumPartners() { return num_partners_; }
  int GetNumCables() { return num_cables_; }
  int GetNumCableAltModes() { return num_cable_altmodes_; }

 private:
  int num_partners_;
  int num_ports_;
  int num_cables_;
  int num_cable_altmodes_;
};

}  // namespace

class UdevMonitorTest : public ::testing::Test {
 public:
  UdevMonitorTest()
      : task_environment_(
            base::test::TaskEnvironment::MainThreadType::IO,
            base::test::TaskEnvironment::ThreadPoolExecutionMode::ASYNC) {}

 protected:
  void SetUp() override {
    observer_ = std::make_unique<TestObserver>();

    monitor_ = std::make_unique<UdevMonitor>();
    monitor_->AddObserver(observer_.get());
  }

  void TearDown() override {
    monitor_.reset();
    observer_.reset();
  }

  // Add a task environment to keep the FileDescriptorWatcher code happy.
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<TestObserver> observer_;
  std::unique_ptr<UdevMonitor> monitor_;
};

TEST_F(UdevMonitorTest, TestBasic) {
  // Create the Mock Udev objects and function invocation expectations.
  auto list_entry2 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry2, GetName())
      .WillOnce(Return(kFakePort0PartnerSysPath));
  EXPECT_CALL(*list_entry2, GetNext()).WillOnce(Return(ByMove(nullptr)));

  auto list_entry1 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry1, GetName()).WillOnce(Return(kFakePort0SysPath));
  EXPECT_CALL(*list_entry1, GetNext())
      .WillOnce(Return(ByMove(std::move(list_entry2))));

  auto enumerate = std::make_unique<brillo::MockUdevEnumerate>();
  EXPECT_CALL(*enumerate, AddMatchSubsystem(StrEq(kTypeCSubsystem)))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, ScanDevices()).WillOnce(Return(true));
  EXPECT_CALL(*enumerate, GetListEntry())
      .WillOnce(Return(ByMove(std::move(list_entry1))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateEnumerate())
      .WillOnce(Return(ByMove(std::move(enumerate))));

  monitor_->SetUdev(std::move(udev));

  EXPECT_THAT(0, observer_->GetNumPorts());

  ASSERT_TRUE(monitor_->ScanDevices());

  EXPECT_THAT(1, observer_->GetNumPorts());
  EXPECT_THAT(1, observer_->GetNumPartners());
}

// Check that a port and partner can be detected after init. Also check whether
// a subsequent partner removal is detected correctly.
TEST_F(UdevMonitorTest, TestHotplug) {
  // Create a socket-pair; to help poke the udev monitoring logic.
  auto fds = std::make_unique<brillo::ScopedSocketPair>();

  // Fake the calls for port add.
  auto device_port = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_port, GetSysPath()).WillOnce(Return(kFakePort0SysPath));
  EXPECT_CALL(*device_port, GetAction()).WillOnce(Return("add"));

  // Fake the calls for partner add.
  auto device_partner_add = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_partner_add, GetSysPath())
      .WillOnce(Return(kFakePort0PartnerSysPath));
  EXPECT_CALL(*device_partner_add, GetAction()).WillOnce(Return("add"));

  // Fake the calls for partner remove.
  auto device_partner_remove = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_partner_remove, GetSysPath())
      .WillOnce(Return(kFakePort0PartnerSysPath));
  EXPECT_CALL(*device_partner_remove, GetAction()).WillOnce(Return("remove"));

  // Fake the calls for cable add.
  auto device_cable_add = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_cable_add, GetSysPath())
      .WillOnce(Return(kFakePort0CableSysPath));
  EXPECT_CALL(*device_cable_add, GetAction()).WillOnce(Return("add"));

  // Create the Mock Udev objects and function invocation expectations.
  auto monitor = std::make_unique<brillo::MockUdevMonitor>();
  EXPECT_CALL(*monitor, FilterAddMatchSubsystemDeviceType(
                            StrEq(kTypeCSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, EnableReceiving()).WillOnce(Return(true));
  EXPECT_CALL(*monitor, GetFileDescriptor()).WillOnce(Return(fds->left));
  EXPECT_CALL(*monitor, ReceiveDevice())
      .WillOnce(Return(ByMove(std::move(device_port))))
      .WillOnce(Return(ByMove(std::move(device_partner_add))))
      .WillOnce(Return(ByMove(std::move(device_partner_remove))))
      .WillOnce(Return(ByMove(std::move(device_cable_add))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateMonitorFromNetlink(StrEq(kUdevMonitorName)))
      .WillOnce(Return(ByMove(std::move(monitor))));

  monitor_->SetUdev(std::move(udev));

  EXPECT_THAT(0, observer_->GetNumPorts());
  EXPECT_THAT(0, observer_->GetNumCables());

  // Skip initial scanning, since we are only interested in testing hotplug.
  ASSERT_TRUE(monitor_->BeginMonitoring());

  // It's too tedious to poke the socket pair to actually trigger the
  // FileDescriptorWatcher without it running repeatedly.
  //
  // Instead we manually call HandleUdevEvent. Effectively this equivalent to
  // triggering the event handler using the FileDescriptorWatcher.
  monitor_->HandleUdevEvent();
  EXPECT_THAT(1, observer_->GetNumPorts());
  monitor_->HandleUdevEvent();
  EXPECT_THAT(1, observer_->GetNumPartners());
  monitor_->HandleUdevEvent();
  EXPECT_THAT(0, observer_->GetNumPartners());
  monitor_->HandleUdevEvent();
  EXPECT_THAT(1, observer_->GetNumCables());
}

// Test that the udev handler correctly handles invalid port sysfs paths.
TEST_F(UdevMonitorTest, TestInvalidPortSyspath) {
  // Create a socket-pair; to help poke the udev monitoring logic.
  auto fds = std::make_unique<brillo::ScopedSocketPair>();

  // Fake the calls for port add.
  auto device_port = std::make_unique<brillo::MockUdevDevice>();
  EXPECT_CALL(*device_port, GetSysPath()).WillOnce(Return(kInvalidPortSysPath));
  EXPECT_CALL(*device_port, GetAction()).WillOnce(Return("add"));

  // Create the Mock Udev objects and function invocation expectations.
  auto monitor = std::make_unique<brillo::MockUdevMonitor>();
  EXPECT_CALL(*monitor, FilterAddMatchSubsystemDeviceType(
                            StrEq(kTypeCSubsystem), nullptr))
      .WillOnce(Return(true));
  EXPECT_CALL(*monitor, EnableReceiving()).WillOnce(Return(true));
  EXPECT_CALL(*monitor, GetFileDescriptor()).WillOnce(Return(fds->left));
  EXPECT_CALL(*monitor, ReceiveDevice())
      .WillOnce(Return(ByMove(std::move(device_port))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateMonitorFromNetlink(StrEq(kUdevMonitorName)))
      .WillOnce(Return(ByMove(std::move(monitor))));

  monitor_->SetUdev(std::move(udev));

  // Skip initial scanning, since we are only interested in testing hotplug.
  ASSERT_TRUE(monitor_->BeginMonitoring());

  // Manually call HandleUdevEvent. Effectively this equivalent to triggering
  // the event handler using the FileDescriptorWatcher.
  monitor_->HandleUdevEvent();
  EXPECT_THAT(0, observer_->GetNumPorts());
}

// Test that the monitor can detect cable creation and SOP' alternate mode
// addition. Also checks that an SOP'' alternate mode addition is ignored.
TEST_F(UdevMonitorTest, TestCableAndAltModeAddition) {
  // Create the Mock Udev objects and function invocation expectations.

  // Unsupported SOP'' alternate mode.
  auto list_entry3 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry3, GetName())
      .WillOnce(Return(kFakePort0SOPDoublePrimeAltModeSysPath));
  EXPECT_CALL(*list_entry3, GetNext())
      .WillOnce(Return(ByMove(std::move(nullptr))));

  // SOP' alternate mode.
  auto list_entry2 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry2, GetName())
      .WillOnce(Return(kFakePort0SOPPrimeAltModeSysPath));
  EXPECT_CALL(*list_entry2, GetNext())
      .WillOnce(Return(ByMove(std::move(list_entry3))));

  // Cable.
  auto list_entry1 = std::make_unique<brillo::MockUdevListEntry>();
  EXPECT_CALL(*list_entry1, GetName()).WillOnce(Return(kFakePort0CableSysPath));
  EXPECT_CALL(*list_entry1, GetNext())
      .WillOnce(Return(ByMove(std::move(list_entry2))));

  auto enumerate = std::make_unique<brillo::MockUdevEnumerate>();
  EXPECT_CALL(*enumerate, AddMatchSubsystem(StrEq(kTypeCSubsystem)))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, ScanDevices()).WillOnce(Return(true));
  EXPECT_CALL(*enumerate, GetListEntry())
      .WillOnce(Return(ByMove(std::move(list_entry1))));

  auto udev = std::make_unique<brillo::MockUdev>();
  EXPECT_CALL(*udev, CreateEnumerate())
      .WillOnce(Return(ByMove(std::move(enumerate))));

  monitor_->SetUdev(std::move(udev));

  ASSERT_TRUE(monitor_->ScanDevices());

  EXPECT_THAT(1, observer_->GetNumCables());
  EXPECT_THAT(1, observer_->GetNumCableAltModes());
}

}  // namespace typecd
