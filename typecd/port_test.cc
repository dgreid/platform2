// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port.h"

#include <string>

#include <base/files/scoped_temp_dir.h>
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

class PortTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    temp_dir_ = scoped_temp_dir_.GetPath();
  }

 public:
  base::FilePath temp_dir_;
  base::ScopedTempDir scoped_temp_dir_;
};

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
  auto port_path = temp_dir_.Append("port0");
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

// Check that DP Alt Mode Entry checks work as expected for a true case:
TEST_F(PortTest, TestDPAltModeEntryCheckTrue) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // Set up fake sysfs paths for 1 alt mode.
  // Set the number of alt modes supported.
  port->partner_->SetNumAltModes(1);

  // Add the DP alt mode.
  std::string mode0_dirname =
      base::StringPrintf("port%d-partner.%d", 0, kDPAltModeIndex);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPSVID, kDPVDO_WD19TB,
                                kDPVDOIndex_WD19TB));
  port->AddRemovePartnerAltMode(mode0_path, true);

  EXPECT_TRUE(port->CanEnterDPAltMode());
}

// Check that DP Alt Mode Entry checks work as expected for a specific false
// case: The Startech dock DP VDO doesn't advertise DFP_D, so we *shouldn't*
// enter DP alternate mode, despite it supporting the DP SID.
TEST_F(PortTest, TestDPAltModeEntryCheckFalseWithDPSID) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));

  // Set up fake sysfs paths for 2 alt modes.
  // Set the number of alt modes supported.
  port->partner_->SetNumAltModes(2);

  // Add the DP alt mode.
  std::string mode0_dirname =
      base::StringPrintf("port%d-partner.%d", 0, kDPAltModeIndex);
  auto mode0_path = temp_dir_.Append(mode0_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode0_path, kDPSVID, kDPVDO, kDPVDOIndex));
  port->AddRemovePartnerAltMode(mode0_path, true);

  // Add the TBT alt mode.
  std::string mode1_dirname =
      base::StringPrintf("port%d-partner.%d", 0, kTBTAltModeIndex);
  auto mode1_path = temp_dir_.Append(mode1_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode1_path, kTBTSVID, kTBTVDO, kTBTVDOIndex));
  port->AddRemovePartnerAltMode(mode1_path, true);

  EXPECT_FALSE(port->CanEnterDPAltMode());
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
  // Add the TBT alt mode.
  std::string mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTSVID, kTBTVDO, kTBTVDOIndex));
  port->AddRemovePartnerAltMode(mode_path, true);

  EXPECT_FALSE(port->CanEnterDPAltMode());
}

// Check that TBT Compat Mode Entry checks work as expected for the following
// working case:
// - Startech.com TB3DK2DPW Alpine Ridge Dock.
// - StarTech Passive Cable 40 Gbps PD 2.0
TEST_F(PortTest, TestTBTCompatibilityModeEntryCheckTrueStartech) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Startech.com TB3DK2DPW Alpine Ridge Dock.
  port->partner_->SetPDRevision(kPDRevision20);
  port->partner_->SetIdHeaderVDO(0xd4008087);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0x0);
  port->partner_->SetProductTypeVDO1(0);
  port->partner_->SetProductTypeVDO2(0);
  port->partner_->SetProductTypeVDO3(0);

  port->partner_->SetNumAltModes(1);
  // Set up fake sysfs paths for 1 alt mode.
  // Add the TBT alt mode.
  std::string mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTSVID, kTBTVDO, 0));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // StarTech Passive Cable 40 Gbps PD 2.0
  port->cable_->SetPDRevision(kPDRevision20);
  port->cable_->SetIdHeaderVDO(0x1c0020c2);
  port->cable_->SetCertStatVDO(0x000000b6);
  port->cable_->SetProductVDO(0x00010310);
  port->cable_->SetProductTypeVDO1(0x11082052);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  EXPECT_TRUE(port->CanEnterTBTCompatibilityMode());
}

// Check that TBT Compat Mode Entry checks work as expected for the following
// non-working case:
// - Startech.com TB3DK2DPW Alpine Ridge Dock.
// - Nekteck USB 2.0 cable (5A).
TEST_F(PortTest, TestTBTCompatibilityModeEntryCheckFalseStartech) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Startech.com TB3DK2DPW Alpine Ridge Dock.
  port->partner_->SetPDRevision(kPDRevision20);
  port->partner_->SetIdHeaderVDO(0xd4008087);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0x0);
  port->partner_->SetProductTypeVDO1(0);
  port->partner_->SetProductTypeVDO2(0);
  port->partner_->SetProductTypeVDO3(0);

  port->partner_->SetNumAltModes(1);
  // Set up fake sysfs paths for 1 alt mode.
  // Add the TBT alt mode.
  std::string mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTSVID, kTBTVDO, 0));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // Nekteck USB 2.0 cable (5A).
  port->cable_->SetPDRevision(kPDRevision30);
  port->cable_->SetIdHeaderVDO(0x18002e98);
  port->cable_->SetCertStatVDO(0x00001533);
  port->cable_->SetProductVDO(0x00010200);
  port->cable_->SetProductTypeVDO1(0xc1082040);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  EXPECT_FALSE(port->CanEnterTBTCompatibilityMode());
}

// Check that TBT Compat Mode Entry checks work as expected for the following
// working case:
// - Dell WD19TB dock.
TEST_F(PortTest, TestTBTCompatibilityModeEntryCheckTrueWD19TB) {
  auto port = std::make_unique<Port>(base::FilePath(kFakePort0SysPath), 0);

  port->AddPartner(base::FilePath(kFakePort0PartnerSysPath));
  // PD ID VDOs for the Dell WD19TB Titan Ridge Dock.
  port->partner_->SetPDRevision(kPDRevision30);
  port->partner_->SetIdHeaderVDO(0x4c0041c3);
  port->partner_->SetCertStatVDO(0x0);
  port->partner_->SetProductVDO(0xb0700712);
  port->partner_->SetProductTypeVDO1(0x0);
  port->partner_->SetProductTypeVDO2(0x0);
  port->partner_->SetProductTypeVDO3(0x0);

  port->partner_->SetNumAltModes(4);
  // Set up fake sysfs paths for partner alt modes.
  // Add the TBT alt mode.
  std::string mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 0);
  auto mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kTBTSVID, kTBTVDO, kTBTVDOIndex));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the DP alt mode.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 1);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(CreateFakeAltMode(mode_path, kDPSVID, kDPVDO_WD19TB, 0));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the Dell alt mode 1.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 2);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(
      CreateFakeAltMode(mode_path, kDellSVID_WD19TB, kDell_WD19TB_VDO1, 0));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Add the Dell alt mode 2.
  mode_dirname = base::StringPrintf("port%d-partner.%d", 0, 3);
  mode_path = temp_dir_.Append(mode_dirname);
  ASSERT_TRUE(
      CreateFakeAltMode(mode_path, kDellSVID_WD19TB, kDell_WD19TB_VDO2, 1));
  port->AddRemovePartnerAltMode(mode_path, true);

  // Set up fake sysfs paths and add a cable.
  port->AddCable(base::FilePath(kFakePort0CableSysPath));

  // Dell's cable is captive.
  port->cable_->SetPDRevision(kPDRevision30);
  port->cable_->SetIdHeaderVDO(0x1c00413c);
  port->cable_->SetCertStatVDO(0x0);
  port->cable_->SetProductVDO(0xb052000);
  port->cable_->SetProductTypeVDO1(0x110c2042);
  port->cable_->SetProductTypeVDO2(0x0);
  port->cable_->SetProductTypeVDO3(0x0);

  EXPECT_TRUE(port->CanEnterTBTCompatibilityMode());
}

}  // namespace typecd
