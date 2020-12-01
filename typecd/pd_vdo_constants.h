// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_PD_VDO_CONSTANTS_H_
#define TYPECD_PD_VDO_CONSTANTS_H_

// Declare fields/values associated with Power Delivery (PD) discovery. These
// are used during various USB Type C mode entry checks.
namespace typecd {

// Modal operation bit field.
// USB PD spec rev 3.0, v 2.0.
// Table 6-29 ID Header VDO.
constexpr uint32_t kIDHeaderVDOModalOperationBitField = (1 << 26);

}  // namespace typecd

#endif  // TYPECD_PD_VDO_CONSTANTS_H_
