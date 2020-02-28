// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_EC_EVENT_TEST_UTILS_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_EC_EVENT_TEST_UTILS_H_

#include <cstdint>
#include <string>

#include "diagnostics/wilco_dtc_supportd/telemetry/ec_event_service.h"

namespace diagnostics {

// Valid EcEvents
extern const EcEventService::EcEvent kEcEventNonWilcoCharger;
extern const EcEventService::EcEvent kEcEventLowPowerCharger;
extern const EcEventService::EcEvent kEcEventBatteryAuth;
extern const EcEventService::EcEvent kEcEventDockDisplay;
extern const EcEventService::EcEvent kEcEventDockThunderbolt;
extern const EcEventService::EcEvent kEcEventIncompatibleDock;
extern const EcEventService::EcEvent kEcEventDockError;

// Invalid EcEvents
extern const EcEventService::EcEvent kEcEventNonSysNotification;
extern const EcEventService::EcEvent kEcEventAcAdapterNoFlags;
extern const EcEventService::EcEvent kEcEventChargerNoFlags;
extern const EcEventService::EcEvent kEcEventUsbCNoFlags;
extern const EcEventService::EcEvent kEcEventNonWilcoChargerBadSubType;
extern const EcEventService::EcEvent kEcEventInvalidPayloadSize;

// Converts a |uint16_t| array to a |uint8_t| array using little-endian format.
// For example, data [0x0102, 0x1314, 0x2526] will be represented as
// [0x02, 0x01, 0x14, 0x13, 0x26, 0x25].
std::string ConvertDataInWordsToString(const uint16_t* data, uint16_t size);

// Returns a pre-initialized EcEvent whose reason matches the provided reason
EcEventService::EcEvent GetEcEventWithReason(
    EcEventService::EcEvent::Reason reason);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_EC_EVENT_TEST_UTILS_H_
