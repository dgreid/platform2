// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_TEST_CONSTANTS_H_
#define TYPECD_TEST_CONSTANTS_H_

namespace typecd {

// Some SVIDs and VDOs we can use to populate the sysfs directories
const int kDPAltModeIndex = 0;
const uint16_t kDPSVID = 0xff01;
const uint32_t kDPVDO = 0x1c46;
const uint32_t kDPVDOIndex = 0;
const int kTBTAltModeIndex = 1;
const uint16_t kTBTSVID = 0x8087;
const uint32_t kTBTVDO = 0x1;
const uint32_t kTBTVDOIndex = 0;

constexpr char kFakePort0SysPath[] = "/sys/class/typec/port0";
constexpr char kFakePort0PartnerSysPath[] =
    "/sys/class/typec/port0/port0-partner";
constexpr char kFakePort0CableSysPath[] = "/sys/class/typec/port0/port0-cable";

}  // namespace typecd

#endif  // TYPECD_TEST_CONSTANTS_H_
