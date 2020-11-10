// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_MOCK_COMMAND_TRANSCEIVER_H_
#define TRUNKS_MOCK_COMMAND_TRANSCEIVER_H_

#include <string>

#include <base/callback.h>
#include <base/macros.h>
#include <gmock/gmock.h>

#include "trunks/command_transceiver.h"

namespace trunks {

class MockCommandTransceiver : public CommandTransceiver {
 public:
  MockCommandTransceiver();
  MockCommandTransceiver(const MockCommandTransceiver&) = delete;
  MockCommandTransceiver& operator=(const MockCommandTransceiver&) = delete;

  ~MockCommandTransceiver() override;

  MOCK_METHOD2(SendCommand, void(const std::string&, const ResponseCallback&));
  MOCK_METHOD1(SendCommandAndWait, std::string(const std::string&));
};

}  // namespace trunks

#endif  // TRUNKS_MOCK_COMMAND_TRANSCEIVER_H_
