// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MOCK_MODEM_H_
#define MODEMFWD_MOCK_MODEM_H_

#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <gmock/gmock.h>

#include "modemfwd/modem.h"

namespace modemfwd {

class MockModem : public Modem {
 public:
  MockModem() = default;
  ~MockModem() override = default;

  MOCK_METHOD(std::string, GetDeviceId, (), (const, override));
  MOCK_METHOD(std::string, GetEquipmentId, (), (const, override));
  MOCK_METHOD(std::string, GetCarrierId, (), (const, override));
  MOCK_METHOD(std::string, GetMainFirmwareVersion, (), (const, override));
  MOCK_METHOD(std::string, GetCarrierFirmwareId, (), (const, override));
  MOCK_METHOD(std::string, GetCarrierFirmwareVersion, (), (const, override));

  MOCK_METHOD(bool, SetInhibited, (bool), (override));

  MOCK_METHOD(bool,
              FlashMainFirmware,
              (const base::FilePath&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              FlashCarrierFirmware,
              (const base::FilePath&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              ClearAttachAPN,
              (const std::string& carrier_uuid),
              (override));
};

}  // namespace modemfwd

#endif  // MODEMFWD_MOCK_MODEM_H_
