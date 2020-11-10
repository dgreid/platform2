// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_MOCK_USB_DEVICE_EVENT_OBSERVER_H_
#define MIST_MOCK_USB_DEVICE_EVENT_OBSERVER_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "mist/usb_device_event_observer.h"

namespace mist {

class MockUsbDeviceEventObserver : public UsbDeviceEventObserver {
 public:
  MockUsbDeviceEventObserver() = default;
  MockUsbDeviceEventObserver(const MockUsbDeviceEventObserver&) = delete;
  MockUsbDeviceEventObserver& operator=(const MockUsbDeviceEventObserver&) =
      delete;

  ~MockUsbDeviceEventObserver() override = default;

  MOCK_METHOD(void,
              OnUsbDeviceAdded,
              (const std::string&, uint8_t, uint8_t, uint16_t, uint16_t),
              (override));
  MOCK_METHOD(void, OnUsbDeviceRemoved, (const std::string&), (override));
};

}  // namespace mist

#endif  // MIST_MOCK_USB_DEVICE_EVENT_OBSERVER_H_
