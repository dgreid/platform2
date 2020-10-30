// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_PD_VDO_CONSTANTS_H_
#define TYPECD_PD_VDO_CONSTANTS_H_

// Declare fields/values associated with Power Delivery (PD) discovery. These
// are used during various USB Type C mode entry checks.
namespace typecd {

// USB PD spec rev 3.0, v 2.0.
// Table 6-29 ID Header VDO.
// Modal operation bit field.
constexpr uint32_t kIDHeaderVDOModalOperationBitField = (1 << 26);
// Bit Masks for Cable type.
constexpr uint8_t kIDHeaderVDOProductTypeBitOffset = 27;
constexpr uint8_t kIDHeaderVDOProductTypeMask = 0x7;
constexpr uint8_t kIDHeaderVDOProductTypeCablePassive = 0x3;
constexpr uint8_t kIDHeaderVDOProductTypeCableActive = 0x4;

// Bit Masks for Product Type VDOs
// USB PD spec rev 3.0, v 2.0.
// Table 6-35 UFP VDO 1
constexpr uint32_t kDeviceCapabilityBitOffset = 24;
constexpr uint8_t kDeviceCapabilityMask = 0xF;
constexpr uint8_t kDeviceCapabilityUSB4 = 0x8;
constexpr uint32_t kUSBSpeed20 = 0x0;
constexpr uint32_t kUSBSpeedBitMask = 0x3;

// Bit Masks for Active Cable VDO1
// USB PD spec rev 3.0, v 2.0.
// Table 6-39 Active Cable VDO1
constexpr uint8_t kActiveCableVDO1VDOVersionOffset = 21;
constexpr uint8_t kActiveCableVDO1VDOVersionBitMask = 0x7;
constexpr uint8_t kActiveCableVDO1VDOVersion13 = 0x3;

//  Bit Masks for Active Cable VDO2
//  US PD spec rev 3.0, v 2.0.
//  Table 6-40 Active Cable VDO2
constexpr uint32_t kActiveCableVDO2USB4SupportedBitField = (1 << 8);

// Bit Masks for TBT3 Cables
// USB Type-C Cable & Connector spec release 2.0
// Table F-11 TBT3 Cable Discover Mode VDO Responses
constexpr uint8_t kTBT3CableDiscModeVDORoundedSupportOffset = 0x19;
constexpr uint8_t kTBT3CableDiscModeVDORoundedSupportMask = 0x3;
constexpr uint8_t kTBT3CableDiscModeVDO_3_4_Gen_Rounded_Non_Rounded = 0x1;

}  // namespace typecd

#endif  // TYPECD_PD_VDO_CONSTANTS_H_
