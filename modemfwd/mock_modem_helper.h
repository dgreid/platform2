// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MOCK_MODEM_HELPER_H_
#define MODEMFWD_MOCK_MODEM_HELPER_H_

#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <gmock/gmock.h>

#include "modemfwd/modem_helper.h"

namespace modemfwd {

class MockModemHelper : public ModemHelper {
 public:
  MockModemHelper() = default;
  ~MockModemHelper() override = default;

  MOCK_METHOD(bool, GetFirmwareInfo, (FirmwareInfo*), (override));
  MOCK_METHOD(bool,
              FlashMainFirmware,
              (const base::FilePath&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              FlashCarrierFirmware,
              (const base::FilePath&, const std::string&),
              (override));
  MOCK_METHOD(bool, FlashModeCheck, (), (override));
  MOCK_METHOD(bool, Reboot, (), (override));
  MOCK_METHOD(bool,
              ClearAttachAPN,
              (const std::string& carrier_uuid),
              (override));
};

}  // namespace modemfwd

#endif  // MODEMFWD_MOCK_MODEM_HELPER_H_
