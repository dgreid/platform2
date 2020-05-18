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

}  // namespace

class UdevMonitorTest : public ::testing::Test {};

TEST_F(UdevMonitorTest, TestBasic) {
  auto udev_monitor = UdevMonitor();

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

  udev_monitor.SetUdev(std::move(udev));

  ASSERT_TRUE(udev_monitor.ScanDevices());
}

}  // namespace typecd
