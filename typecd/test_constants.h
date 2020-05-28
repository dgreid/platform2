// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_TEST_CONSTANTS_H_
#define TYPECD_TEST_CONSTANTS_H_

namespace typecd {

constexpr char kFakePort0SysPath[] = "/sys/class/typec/port0";
constexpr char kFakePort0PartnerSysPath[] =
    "/sys/class/typec/port0/port0-partner";

}  // namespace typecd

#endif  // TYPECD_TEST_CONSTANTS_H_
