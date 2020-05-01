// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dns_server_tester.h"

#include <memory>
#include <string>

#include <base/bind.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_dns_client.h"
#include "shill/mock_dns_client_factory.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/net/mock_time.h"

using base::Bind;
using base::Callback;
using base::Unretained;
using std::string;
using std::vector;
using testing::_;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {
const char kInterfaceName[] = "int0";
const char kDnsServer0[] = "8.8.8.8";
const char kDnsServer1[] = "8.8.4.4";
const char* const kDnsServers[] = {kDnsServer0, kDnsServer1};
}  // namespace

class DnsServerTesterTest : public Test {
 public:
  DnsServerTesterTest()
      : manager_(&control_, nullptr, nullptr),
        device_info_(new NiceMock<MockDeviceInfo>(&manager_)),
        connection_(new StrictMock<MockConnection>(device_info_.get())),
        interface_name_(kInterfaceName),
        dns_servers_(kDnsServers, kDnsServers + 2) {}

  void SetUp() override {
    EXPECT_CALL(*connection_, interface_name())
        .WillRepeatedly(ReturnRef(interface_name_));
    dns_server_tester_.reset(
        new DnsServerTester(connection_.get(), &dispatcher_, dns_servers_,
                            false, callback_target_.result_callback()));
  }

 protected:
  class CallbackTarget {
   public:
    CallbackTarget()
        : result_callback_(
              Bind(&CallbackTarget::ResultCallback, Unretained(this))) {}

    MOCK_METHOD(void, ResultCallback, (const DnsServerTester::Status));

    Callback<void(const DnsServerTester::Status)>& result_callback() {
      return result_callback_;
    }

   private:
    Callback<void(const DnsServerTester::Status)> result_callback_;
  };

  DnsServerTester* dns_server_tester() { return dns_server_tester_.get(); }
  MockEventDispatcher& dispatcher() { return dispatcher_; }
  CallbackTarget& callback_target() { return callback_target_; }

  void ExpectReset() {
    EXPECT_TRUE(callback_target_.result_callback() ==
                dns_server_tester_->dns_result_callback_);
  }

 private:
  StrictMock<MockEventDispatcher> dispatcher_;
  MockControl control_;
  MockManager manager_;
  std::unique_ptr<MockDeviceInfo> device_info_;
  scoped_refptr<MockConnection> connection_;
  CallbackTarget callback_target_;
  const string interface_name_;
  vector<string> dns_servers_;
  std::unique_ptr<DnsServerTester> dns_server_tester_;
};

TEST_F(DnsServerTesterTest, Constructor) {
  ExpectReset();
}

TEST_F(DnsServerTesterTest, StartAttempt) {
  // Start attempt with no delay.
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 0));
  dns_server_tester()->StartAttempt(0);

  // Start attempt with delay.
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, 100));
  dns_server_tester()->StartAttempt(100);
}

TEST_F(DnsServerTesterTest, StartAttemptTask) {
  // Setup mock DNS test client.
  MockDnsClient* dns_test_client = new MockDnsClient();
  dns_server_tester()->dns_test_client_.reset(dns_test_client);

  // DNS test task started successfully.
  EXPECT_CALL(*dns_test_client, Start(_, _)).WillOnce(Return(true));
  EXPECT_CALL(callback_target(), ResultCallback(_)).Times(0);
  dns_server_tester()->StartAttemptTask();
  Mock::VerifyAndClearExpectations(dns_test_client);

  // DNS test task failed to start.
  EXPECT_CALL(*dns_test_client, Start(_, _)).WillOnce(Return(false));
  EXPECT_CALL(callback_target(),
              ResultCallback(DnsServerTester::kStatusFailure))
      .Times(1);
  dns_server_tester()->StartAttemptTask();
  Mock::VerifyAndClearExpectations(dns_test_client);
}

TEST_F(DnsServerTesterTest, AttemptCompleted) {
  // DNS test attempt succeed with retry_until_success_ not set.
  dns_server_tester()->retry_until_success_ = false;
  EXPECT_CALL(callback_target(),
              ResultCallback(DnsServerTester::kStatusSuccess))
      .Times(1);
  dns_server_tester()->CompleteAttempt(DnsServerTester::kStatusSuccess);

  // DNS test attempt succeed with retry_until_success_ being set.
  dns_server_tester()->retry_until_success_ = true;
  EXPECT_CALL(callback_target(),
              ResultCallback(DnsServerTester::kStatusSuccess))
      .Times(1);
  dns_server_tester()->CompleteAttempt(DnsServerTester::kStatusSuccess);

  // DNS test attempt failed with retry_until_success_ not set.
  dns_server_tester()->retry_until_success_ = false;
  EXPECT_CALL(callback_target(),
              ResultCallback(DnsServerTester::kStatusFailure))
      .Times(1);
  dns_server_tester()->CompleteAttempt(DnsServerTester::kStatusFailure);

  // DNS test attempt failed with retry_until_success_ being set.
  dns_server_tester()->retry_until_success_ = true;
  EXPECT_CALL(callback_target(), ResultCallback(_)).Times(0);
  EXPECT_CALL(dispatcher(), PostDelayedTask(_, _, _)).Times(1);
  dns_server_tester()->CompleteAttempt(DnsServerTester::kStatusFailure);
}

TEST_F(DnsServerTesterTest, StopAttempt) {
  // Setup mock DNS test client.
  MockDnsClient* dns_test_client = new MockDnsClient();
  dns_server_tester()->dns_test_client_.reset(dns_test_client);

  // DNS test task started successfully.
  EXPECT_CALL(*dns_test_client, Start(_, _)).WillOnce(Return(true));
  EXPECT_CALL(callback_target(), ResultCallback(_)).Times(0);
  dns_server_tester()->StartAttemptTask();
  Mock::VerifyAndClearExpectations(dns_test_client);

  // Stop the DNS test attempt.
  EXPECT_CALL(*dns_test_client, Stop()).Times(1);
  EXPECT_CALL(callback_target(), ResultCallback(_)).Times(0);
  dns_server_tester()->StopAttempt();
  Mock::VerifyAndClearExpectations(dns_test_client);
}

}  // namespace shill
