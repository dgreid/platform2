// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port.h"

#include <string>

#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "typecd/test_constants.h"
#include "typecd/test_utils.h"

namespace {
constexpr char kInvalidDataRole1[] = "xsadft [hasdr]";
constexpr char kInvalidDataRole2[] = "]asdf[ dsdd";
constexpr char kValidDataRole1[] = "device";
constexpr char kValidDataRole2[] = "[host] device";
constexpr char kValidDataRole3[] = "host [device]";
}  // namespace

namespace typecd {

class PortTest : public ::testing::Test {};

// Check that basic Port creation, partner addition/deletion works.
TEST_F(PortTest, TestBasicAdd) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);
  EXPECT_NE(nullptr, port);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  EXPECT_NE(nullptr, port->partner_);
  port->RemovePartner();
  EXPECT_EQ(nullptr, port->partner_);
}

// Check GetDataRole() for various sysfs values.
TEST_F(PortTest, TestGetDataRole) {
  // Set up fake sysfs directory for the port..
  base::FilePath temp_dir;
  ASSERT_TRUE(base::CreateNewTempDirectory("", &temp_dir));

  auto port_path = temp_dir.Append("port0");
  ASSERT_TRUE(base::CreateDirectory(port_path));

  auto data_role_path = port_path.Append("data_role");
  ASSERT_TRUE(base::WriteFile(data_role_path, kValidDataRole1,
                              strlen(kValidDataRole1)));

  // Create a port.
  auto port = std::make_unique<Port>(base::FilePath(port_path), 0);
  ASSERT_NE(nullptr, port);

  EXPECT_EQ("device", port->GetDataRole());

  ASSERT_TRUE(base::WriteFile(data_role_path, kValidDataRole2,
                              strlen(kValidDataRole2)));
  EXPECT_EQ("host", port->GetDataRole());

  ASSERT_TRUE(base::WriteFile(port_path.Append("data_role"), kValidDataRole3,
                              strlen(kValidDataRole3)));
  EXPECT_EQ("device", port->GetDataRole());

  ASSERT_TRUE(base::WriteFile(port_path.Append("data_role"), kInvalidDataRole1,
                              strlen(kInvalidDataRole1)));
  EXPECT_EQ("", port->GetDataRole());

  ASSERT_TRUE(base::WriteFile(port_path.Append("data_role"), kInvalidDataRole2,
                              strlen(kInvalidDataRole2)));
  EXPECT_EQ("", port->GetDataRole());
}

// Check that DP Alt Mode Entry checks work as expected for true cases.
TEST_F(PortTest, TestDPAltModeEntryCheckTrue) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // Set up fake sysfs paths for 2 alt modes.
  base::FilePath temp_dir;
  ASSERT_TRUE(base::CreateNewTempDirectory("", &temp_dir));

  // Set the number of alt modes supported.
  port->partner_->SetNumAltModes(2);

  // Add the DP alt mode.
  std::string mode0_dirname =
      base::StringPrintf("port%d-partner.%d", 0, kDPAltModeIndex);
  auto mode0_path = temp_dir.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPSVID, kDPVDO, kDPVDOIndex));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the TBT alt mode.
  std::string mode1_dirname =
      base::StringPrintf("port%d-partner.%d", 0, kTBTAltModeIndex);
  auto mode1_path = temp_dir.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, kTBTSVID, kTBTVDO, kTBTVDOIndex));
  port->AddRemovePartnerAltMode(mode1_path, true);

  EXPECT_TRUE(port->CanEnterDPAltMode());
}

// Check that DP Alt Mode Entry checks work as expected for false cases.
TEST_F(PortTest, TestDPAltModeEntryCheckFalse) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  port->partner_->SetNumAltModes(0);

  // Check the case where the partner doesn't support any alt modes.
  EXPECT_FALSE(port->CanEnterDPAltMode());

  port->partner_->SetNumAltModes(1);

  // Set up fake sysfs paths for 1 alt mode.
  base::FilePath temp_dir;
  ASSERT_TRUE(base::CreateNewTempDirectory("", &temp_dir));

  // Add the TBT alt mode.
  std::string mode_dirname =
      base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTSVID, kTBTVDO, kTBTVDOIndex));
  port->AddRemovePartnerAltMode(mode_path, true);

  EXPECT_FALSE(port->CanEnterDPAltMode());
}

}  // namespace typecd
