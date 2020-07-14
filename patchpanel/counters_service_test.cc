// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/counters_service.h"

#include <memory>
#include <string>
#include <vector>

#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/fake_shill_client.h"

namespace patchpanel {
namespace {

using ::testing::Contains;
using ::testing::ElementsAreArray;

class MockProcessRunner : public MinijailedProcessRunner {
 public:
  MockProcessRunner() = default;
  ~MockProcessRunner() = default;

  MOCK_METHOD(int,
              iptables,
              (const std::string& table,
               const std::vector<std::string>& argv,
               bool log_failures,
               std::string* output),
              (override));
  MOCK_METHOD(int,
              ip6tables,
              (const std::string& table,
               const std::vector<std::string>& argv,
               bool log_failures,
               std::string* output),
              (override));
};

class CountersServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    fake_shill_client_ = shill_helper_.FakeClient();
    counters_svc_ =
        std::make_unique<CountersService>(fake_shill_client_.get(), &runner_);
  }

  FakeShillClientHelper shill_helper_;
  MockProcessRunner runner_;
  std::unique_ptr<FakeShillClient> fake_shill_client_;
  std::unique_ptr<CountersService> counters_svc_;
};

TEST_F(CountersServiceTest, OnNewDevice) {
  // Makes the check commands return 1 (not found).
  EXPECT_CALL(runner_, iptables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(1));
  EXPECT_CALL(runner_, ip6tables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(1));

  // The following commands are expected when eth0 comes up.
  const std::vector<std::vector<std::string>> expected_calls{
      {"-N", "rx_input_eth0", "-w"},
      {"-A", "INPUT", "-i", "eth0", "-j", "rx_input_eth0", "-w"},
      {"-A", "rx_input_eth0", "-w"},

      {"-N", "rx_fwd_eth0", "-w"},
      {"-A", "FORWARD", "-i", "eth0", "-j", "rx_fwd_eth0", "-w"},
      {"-A", "rx_fwd_eth0", "-w"},

      {"-N", "tx_postrt_eth0", "-w"},
      {"-A", "POSTROUTING", "-o", "eth0", "-m", "owner", "--socket-exists",
       "-j", "tx_postrt_eth0", "-w"},
      {"-A", "tx_postrt_eth0", "-w"},

      {"-N", "tx_fwd_eth0", "-w"},
      {"-A", "FORWARD", "-o", "eth0", "-j", "tx_fwd_eth0", "-w"},
      {"-A", "tx_fwd_eth0", "-w"},
  };

  for (const auto& rule : expected_calls) {
    EXPECT_CALL(runner_, iptables("mangle", ElementsAreArray(rule), _, _));
    EXPECT_CALL(runner_, ip6tables("mangle", ElementsAreArray(rule), _, _));
  }

  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("/device/eth0")};
  fake_shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty,
                                                  brillo::Any(devices));
}

TEST_F(CountersServiceTest, OnSameDeviceAppearAgain) {
  // Makes the check commands return 0 (we already have these rules).
  EXPECT_CALL(runner_, iptables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(runner_, ip6tables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(0));

  // Creating chains commands are expected but no more creating rules command
  // (with "-I" or "-A") should come.
  EXPECT_CALL(runner_, iptables(_, Contains("-N"), _, _)).Times(AnyNumber());
  EXPECT_CALL(runner_, ip6tables(_, Contains("-N"), _, _)).Times(AnyNumber());

  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("/device/eth0")};
  fake_shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty,
                                                  brillo::Any(devices));
}

}  // namespace
}  // namespace patchpanel
