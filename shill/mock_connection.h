// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_CONNECTION_H_
#define SHILL_MOCK_CONNECTION_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/connection.h"

namespace shill {

class MockConnection : public Connection {
 public:
  explicit MockConnection(const DeviceInfo* device_info);
  MockConnection(const MockConnection&) = delete;
  MockConnection& operator=(const MockConnection&) = delete;

  ~MockConnection() override;

  MOCK_METHOD(void, UpdateFromIPConfig, (const IPConfigRefPtr&), (override));
  MOCK_METHOD(bool, IsDefault, (), (const, override));
  MOCK_METHOD(void, SetPriority, (uint32_t, bool), (override));
  MOCK_METHOD(void, SetUseDNS, (bool), (override));
  MOCK_METHOD(const RpcIdentifier&,
              ipconfig_rpc_identifier,
              (),
              (const, override));
  MOCK_METHOD(const std::string&, interface_name, (), (const, override));
  MOCK_METHOD(const std::vector<std::string>&,
              dns_servers,
              (),
              (const, override));
  MOCK_METHOD(const IPAddress&, local, (), (const, override));
  MOCK_METHOD(const IPAddress&, gateway, (), (const, override));
  MOCK_METHOD(Technology, technology, (), (const, override));
  MOCK_METHOD(std::string&, tethering, (), (const, override));
  MOCK_METHOD(void,
              UpdateDNSServers,
              (const std::vector<std::string>&),
              (override));
  MOCK_METHOD(bool, IsIPv6, (), (override));
  MOCK_METHOD(std::string, GetSubnetName, (), (const, override));
  MOCK_METHOD(void,
              AddInputInterfaceToRoutingTable,
              (const std::string&),
              (override));
  MOCK_METHOD(void,
              RemoveInputInterfaceFromRoutingTable,
              (const std::string&),
              (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_CONNECTION_H_
