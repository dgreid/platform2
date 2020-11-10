// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NET_MOCK_RTNL_HANDLER_H_
#define SHILL_NET_MOCK_RTNL_HANDLER_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/net/rtnl_handler.h"

namespace shill {

class MockRTNLHandler : public RTNLHandler {
 public:
  MockRTNLHandler() = default;
  MockRTNLHandler(const MockRTNLHandler&) = delete;
  MockRTNLHandler& operator=(const MockRTNLHandler&) = delete;

  ~MockRTNLHandler() override = default;

  MOCK_METHOD(void, Start, (uint32_t), (override));
  MOCK_METHOD(void, AddListener, (RTNLListener*), (override));
  MOCK_METHOD(void, RemoveListener, (RTNLListener*), (override));
  MOCK_METHOD(void,
              SetInterfaceFlags,
              (int, unsigned int, unsigned int),
              (override));
  MOCK_METHOD(void, SetInterfaceMTU, (int, unsigned int), (override));
  MOCK_METHOD(void,
              SetInterfaceMac,
              (int, const ByteString&, ResponseCallback),
              (override));
  MOCK_METHOD(bool,
              AddInterfaceAddress,
              (int, const IPAddress&, const IPAddress&, const IPAddress&),
              (override));
  MOCK_METHOD(bool,
              RemoveInterfaceAddress,
              (int, const IPAddress&),
              (override));
  MOCK_METHOD(bool, RemoveInterface, (int), (override));
  MOCK_METHOD(void, RequestDump, (uint32_t), (override));
  MOCK_METHOD(int, GetInterfaceIndex, (const std::string&), (override));
  MOCK_METHOD(bool, DoSendMessage, (RTNLMessage*, uint32_t*));
  bool SendMessage(std::unique_ptr<RTNLMessage> message,
                   uint32_t* seq) override {
    return DoSendMessage(message.get(), seq);
  }
};

}  // namespace shill

#endif  // SHILL_NET_MOCK_RTNL_HANDLER_H_
