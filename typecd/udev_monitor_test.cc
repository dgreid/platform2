// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/udev_monitor.h"

#include <brillo/udev/mock_udev.h>
#include <brillo/udev/mock_udev_enumerate.h>
#include <brillo/udev/mock_udev_list_entry.h>
#include <gtest/gtest.h>

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::StrEq;

namespace typecd {

namespace {

constexpr char kTypeCSubsystem[] = "typec";
constexpr char kFakePort0SysPath[] = "/sys/class/typec/port0";
constexpr char kFakePort0PartnerSysPath[] =
    "/sys/class/typec/port0/port0-partner";

// A really dumb observer to verify that UdevMonitor is invoking the right
// callbacks.
class TestObserver : public UdevMonitor::Observer {
 public:
  void OnPortAddedOrRemoved(const base::FilePath& path, bool added) override {
    if (added)
      num_ports_++;
    else
      num_ports_--;
  };

  void OnPartnerAddedOrRemoved(const base::FilePath& path,
                               bool added) override {
    if (added)
      num_partners_++;
    else
      num_partners_--;
  };

  int GetNumPorts() { return num_ports_; }
  int GetNumPartners() { return num_partners_; }

 private:
  int num_partners_;
  int num_ports_;
};

}  // namespace

class UdevMonitorTest : public ::testing::Test {};

TEST_F(UdevMonitorTest, TestBasic) {
  auto observer = std::make_unique<TestObserver>();

  auto udev_monitor = std::make_unique<UdevMonitor>();
  udev_monitor->AddObserver(observer.get());

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

  udev_monitor->SetUdev(std::move(udev));

  EXPECT_THAT(0, observer->GetNumPorts());

  ASSERT_TRUE(udev_monitor->ScanDevices());

  EXPECT_THAT(1, observer->GetNumPorts());
  EXPECT_THAT(1, observer->GetNumPartners());
}

}  // namespace typecd
