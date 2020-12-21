// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "typecd/pd_vdo_constants.h"

namespace {

constexpr char kDataRoleDRPRegex[] = R"(.*\[(\w+)\].*)";
constexpr uint16_t kDPAltModeSID = 0xff01;
constexpr uint16_t kTBTAltModeVID = 0x8087;

// DP altmode VDO capabilities.
// NOTE: We only include the bit fields we are interested in.
constexpr uint32_t kDPModeSnk = 0x1;

}  // namespace

namespace typecd {

Port::Port(const base::FilePath& syspath, int port_num)
    : syspath_(syspath), port_num_(port_num) {
  LOG(INFO) << "Port " << port_num_ << " enumerated.";
}

void Port::AddCable(const base::FilePath& path) {
  if (cable_) {
    LOG(WARNING) << "Cable already exists for port " << port_num_;
    return;
  }
  cable_ = std::make_unique<Cable>(path);

  LOG(INFO) << "Cable enumerated for port " << port_num_;
}

void Port::RemoveCable() {
  if (!cable_) {
    LOG(WARNING) << "No partner present for port " << port_num_;
    return;
  }
  cable_.reset();

  LOG(INFO) << "Cable removed for port " << port_num_;
}

void Port::AddCablePlug(const base::FilePath& syspath) {
  if (!cable_) {
    LOG(WARNING) << "No cable present for port " << port_num_;
    return;
  }

  cable_->RegisterCablePlug(syspath);
}

void Port::AddPartner(const base::FilePath& path) {
  if (partner_) {
    LOG(WARNING) << "Partner already exists for port " << port_num_;
    return;
  }
  partner_ = std::make_unique<Partner>(path);

  LOG(INFO) << "Partner enumerated for port " << port_num_;
}

void Port::RemovePartner() {
  if (!partner_) {
    LOG(WARNING) << "No partner present for port " << port_num_;
    return;
  }
  partner_.reset();

  LOG(INFO) << "Partner removed for port " << port_num_;
}

void Port::AddRemovePartnerAltMode(const base::FilePath& path, bool added) {
  if (!partner_) {
    LOG(WARNING) << "Trying to add alt mode for non-existent partner on port "
                 << port_num_;
    return;
  }

  if (added) {
    if (!partner_->AddAltMode(path))
      LOG(ERROR) << "Failed to add alt mode for port " << port_num_
                 << " at path " << path;
  } else {
    partner_->RemoveAltMode(path);
  }
}

void Port::AddCableAltMode(const base::FilePath& path) {
  if (!cable_) {
    LOG(WARNING) << "Trying to add alt mode for non-existent cable on port "
                 << port_num_;
    return;
  }

  if (!cable_->AddAltMode(path)) {
    LOG(ERROR) << "Failed to add SOP' alt mode for port " << port_num_
               << " at path " << path;
  }
}

void Port::PartnerChanged() {
  if (!partner_) {
    LOG(WARNING) << "Trying to update a non-existent partner on port "
                 << port_num_;
    return;
  }

  partner_->UpdatePDInfoFromSysfs();
}

std::string Port::GetDataRole() {
  std::string data_role;
  std::string sysfs_str;
  auto path = syspath_.Append("data_role");

  if (!base::ReadFileToString(path, &sysfs_str)) {
    LOG(ERROR) << "Couldn't read sysfs path " << path;
    goto end;
  }

  // First check for a dual role port, in which case the current role is in
  // box-brackets. For example: [host] device
  if (!RE2::FullMatch(sysfs_str, kDataRoleDRPRegex, &data_role)) {
    LOG(INFO)
        << "Couldn't determine role, assuming DRP(Dual Role Port) for port "
        << port_num_;
  }

  if (data_role == "")
    data_role = sysfs_str;

  base::TrimWhitespaceASCII(data_role, base::TRIM_ALL, &data_role);

  if (data_role != "host" && data_role != "device")
    data_role = "";

end:
  return data_role;
}

bool Port::CanEnterDPAltMode() {
  for (int i = 0; i < partner_->GetNumAltModes(); i++) {
    auto alt_mode = partner_->GetAltMode(i);
    // Only enter DP if:
    // - The DP SID is found.
    // - The DP altmode VDO says it is DFP_D capable.
    if (!alt_mode || alt_mode->GetSVID() != kDPAltModeSID)
      continue;
    if (alt_mode->GetVDO() & kDPModeSnk)
      return true;
  }

  return false;
}

// Mode entry check for TBT compatibility mode.
// Ref:
//   USB Type-C Connector Spec, release 2.0
//   Figure F-1.
bool Port::CanEnterTBTCompatibilityMode() {
  if (!cable_) {
    LOG(ERROR) << "No cable object registered, can't enter TBT Compat mode.";
    return false;
  }

  // Check if the Cable meets TBT3 speed requirements.
  // NOTE: Since we aren't configuring the TBT3 entry speed, we don't
  // need to check for the existence of TBT3 alt mode in the SOP' discovery.
  if (!cable_->TBT3PDIdentityCheck())
    return false;

  // Check if the partner supports Modal Operation
  // Ref:
  //   USB PD spec, rev 3.0, v2.0.
  //   Table 6-29
  auto partner_idh = partner_->GetIdHeaderVDO();
  if (!(partner_idh & kIDHeaderVDOModalOperationBitField)) {
    return false;
  }

  // Check if the partner supports TBT compatibility mode.
  if (!IsPartnerAltModePresent(kTBTAltModeVID)) {
    LOG(INFO) << "TBT Compat mode not supported by partner.";
    return false;
  }

  return true;
}

// Follow the USB4 entry checks as per:
// Figure 5-1: USB4 Discovery and Entry Flow Model
// USB Type-C Cable & Connector Spec Rel 2.0.
bool Port::CanEnterUSB4() {
  if (!partner_) {
    LOG(ERROR) << "Attempting USB4 entry without a registered partner on port: "
               << port_num_;
    return false;
  }

  if (!cable_) {
    LOG(ERROR) << "Attempting USB4 entry without a registered cable on port: "
               << port_num_;
    return false;
  }

  // Partner doesn't support USB4.
  auto partner_cap =
      (partner_->GetProductTypeVDO1() >> kDeviceCapabilityBitOffset) &
      kDeviceCapabilityMask;
  if (!(partner_cap & kDeviceCapabilityUSB4))
    return false;

  // Cable checks.
  auto cable_type =
      (cable_->GetIdHeaderVDO() >> kIDHeaderVDOProductTypeBitOffset) &
      kIDHeaderVDOProductTypeMask;
  if (cable_type == kIDHeaderVDOProductTypeCableActive) {
    auto vdo_version =
        (cable_->GetProductTypeVDO1() >> kActiveCableVDO1VDOVersionOffset) &
        kActiveCableVDO1VDOVersionBitMask;

    // For VDO version == 1.3, check if Active Cable VDO2 supports USB4.
    // NOTE: The meaning of this field is inverted; the bit field being set
    // means USB4 is *not* supported.
    if (vdo_version == kActiveCableVDO1VDOVersion13)
      return !(cable_->GetProductTypeVDO2() &
               kActiveCableVDO2USB4SupportedBitField);

    // For VDO version != 1.3, don't enable USB4 if the cable:
    // - doesn't support modal operation, or
    // - doesn't have an Intel SVID Alt mode, or
    // - doesn't have rounded support.
    if (!(cable_->GetIdHeaderVDO() & kIDHeaderVDOModalOperationBitField))
      return false;

    if (!IsCableAltModePresent(kTBTAltModeVID))
      return false;

    // Go through cable alt modes and check for rounded support in the TBT VDO.
    auto num_altmodes = cable_->GetNumAltModes();
    for (int i = 0; i < num_altmodes; i++) {
      AltMode* altmode = cable_->GetAltMode(i);
      if (!altmode || altmode->GetSVID() != kTBTAltModeVID)
        continue;
      auto rounded_support =
          altmode->GetVDO() >> kTBT3CableDiscModeVDORoundedSupportOffset &
          kTBT3CableDiscModeVDORoundedSupportMask;
      if (rounded_support == kTBT3CableDiscModeVDO_3_4_Gen_Rounded_Non_Rounded)
        return true;
    }

    return false;
  } else if (cable_type == kIDHeaderVDOProductTypeCablePassive) {
    // Apart from USB2.0, USB4 is supported for all other speeds.
    auto speed = cable_->GetProductTypeVDO1() & kUSBSpeedBitMask;
    return speed != kUSBSpeed20;
  }

  LOG(ERROR) << "Invalid cable type: " << cable_type
             << ", USB4 entry aborted on port " << port_num_;

  return false;
}

bool Port::IsPartnerAltModePresent(uint16_t altmode_sid) {
  auto num_alt_modes = partner_->GetNumAltModes();
  for (int i = 0; i < num_alt_modes; i++) {
    AltMode* alt_mode = partner_->GetAltMode(i);
    if (!alt_mode)
      continue;
    if (alt_mode->GetSVID() == altmode_sid)
      return true;
  }

  return false;
}

bool Port::IsPartnerDiscoveryComplete() {
  if (!partner_) {
    LOG(INFO)
        << "Trying to check discovery complete for a non-existent partner.";
    return false;
  }

  return partner_->DiscoveryComplete();
}

bool Port::IsCableAltModePresent(uint16_t altmode_sid) {
  auto num_alt_modes = cable_->GetNumAltModes();
  for (int i = 0; i < num_alt_modes; i++) {
    AltMode* alt_mode = cable_->GetAltMode(i);
    if (!alt_mode)
      continue;
    if (alt_mode->GetSVID() == altmode_sid)
      return true;
  }

  return false;
}

}  // namespace typecd
