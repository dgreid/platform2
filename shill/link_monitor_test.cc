// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/link_monitor.h"

#include <base/bind.h>
#include <base/stl_util.h>
#include <gtest/gtest.h>

#include "shill/logging.h"
#include "shill/mock_active_link_monitor.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_log.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_passive_link_monitor.h"
#include "shill/net/byte_string.h"
#include "shill/net/mock_time.h"
#include "shill/test_event_dispatcher.h"

using base::Bind;
using base::Unretained;
using testing::_;
using testing::DoAll;
using testing::HasSubstr;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgPointee;
using testing::StrictMock;
using testing::Test;

namespace shill {

namespace {
const uint8_t kGatewayMacAddress[] = {0, 1, 2, 3, 4, 5};
}  // namespace

class LinkMonitorObserver {
 public:
  LinkMonitorObserver()
      : failure_callback_(
            Bind(&LinkMonitorObserver::OnFailureCallback, Unretained(this))),
        gateway_change_callback_(Bind(
            &LinkMonitorObserver::OnGatewayChangeCallback, Unretained(this))) {}
  LinkMonitorObserver(const LinkMonitorObserver&) = delete;
  LinkMonitorObserver& operator=(const LinkMonitorObserver&) = delete;

  virtual ~LinkMonitorObserver() = default;

  MOCK_METHOD(void, OnFailureCallback, ());
  MOCK_METHOD(void, OnGatewayChangeCallback, ());

  const LinkMonitor::FailureCallback failure_callback() const {
    return failure_callback_;
  }

  const LinkMonitor::GatewayChangeCallback gateway_change_callback() const {
    return gateway_change_callback_;
  }

 private:
  LinkMonitor::FailureCallback failure_callback_;
  LinkMonitor::GatewayChangeCallback gateway_change_callback_;
};

class LinkMonitorTest : public Test {
 public:
  LinkMonitorTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        device_info_(&manager_),
        connection_(new StrictMock<MockConnection>(&device_info_)),
        active_link_monitor_(new MockActiveLinkMonitor()),
        passive_link_monitor_(new MockPassiveLinkMonitor()),
        monitor_(connection_,
                 &dispatcher_,
                 &metrics_,
                 &device_info_,
                 observer_.failure_callback(),
                 observer_.gateway_change_callback()) {}
  ~LinkMonitorTest() override = default;

  void SetUp() override {
    monitor_.active_link_monitor_.reset(active_link_monitor_);
    monitor_.passive_link_monitor_.reset(passive_link_monitor_);
    monitor_.time_ = &time_;

    time_val_.tv_sec = 0;
    time_val_.tv_usec = 0;
    EXPECT_CALL(time_, GetTimeMonotonic(_))
        .WillRepeatedly(DoAll(SetArgPointee<0>(time_val_), Return(0)));
    EXPECT_CALL(*connection_, technology())
        .WillRepeatedly(Return(Technology::kEthernet));
  }

  void AdvanceTime(int time_ms) {
    struct timeval adv_time = {static_cast<time_t>(time_ms / 1000),
                               static_cast<time_t>((time_ms % 1000) * 1000)};
    timeradd(&time_val_, &adv_time, &time_val_);
    EXPECT_CALL(time_, GetTimeMonotonic(_))
        .WillRepeatedly(DoAll(SetArgPointee<0>(time_val_), Return(0)));
  }

  void SetGatewayMacAddress(const ByteString& gateway_mac_address) {
    monitor_.gateway_mac_address_ = gateway_mac_address;
  }

  void VerifyGatewayMacAddress(const ByteString& gateway_mac_address) {
    EXPECT_TRUE(monitor_.gateway_mac_address_.Equals(gateway_mac_address));
  }

  void TriggerActiveLinkMonitorFailure(Metrics::LinkMonitorFailure failure,
                                       int broadcast_failure_count,
                                       int unicast_failure_count) {
    monitor_.OnActiveLinkMonitorFailure(failure, broadcast_failure_count,
                                        unicast_failure_count);
  }

  void TriggerActiveLinkMonitorSuccess() {
    monitor_.OnActiveLinkMonitorSuccess();
  }

  void TriggerPassiveLinkMonitorResultCallback(bool status) {
    monitor_.OnPassiveLinkMonitorResultCallback(status);
  }

 protected:
  EventDispatcherForTest dispatcher_;
  StrictMock<MockMetrics> metrics_;
  MockControl control_;
  MockManager manager_;
  NiceMock<MockDeviceInfo> device_info_;
  scoped_refptr<MockConnection> connection_;
  MockTime time_;
  struct timeval time_val_;
  MockActiveLinkMonitor* active_link_monitor_;
  MockPassiveLinkMonitor* passive_link_monitor_;
  LinkMonitorObserver observer_;
  LinkMonitor monitor_;
};

MATCHER_P(IsMacAddress, mac_address, "") {
  return mac_address.Equals(arg);
}

TEST_F(LinkMonitorTest, Start) {
  EXPECT_CALL(*active_link_monitor_,
              Start(ActiveLinkMonitor::kDefaultTestPeriodMilliseconds))
      .WillOnce(Return(false));
  EXPECT_FALSE(monitor_.Start());
  Mock::VerifyAndClearExpectations(active_link_monitor_);

  EXPECT_CALL(*active_link_monitor_,
              Start(ActiveLinkMonitor::kDefaultTestPeriodMilliseconds))
      .WillOnce(Return(true));
  EXPECT_TRUE(monitor_.Start());
  Mock::VerifyAndClearExpectations(active_link_monitor_);
}

TEST_F(LinkMonitorTest, OnAfterResume) {
  ByteString gateway_mac(kGatewayMacAddress, base::size(kGatewayMacAddress));
  const bool kGatewayUnicastArpSupport = true;
  SetGatewayMacAddress(gateway_mac);
  // Verify gateway settings persist when link monitor is restarted, and
  // active link monitor is started with fast test period.
  EXPECT_CALL(*active_link_monitor_, Stop()).Times(1);
  EXPECT_CALL(*passive_link_monitor_, Stop()).Times(1);
  EXPECT_CALL(*active_link_monitor_, gateway_supports_unicast_arp())
      .WillOnce(Return(kGatewayUnicastArpSupport));
  EXPECT_CALL(*active_link_monitor_,
              set_gateway_mac_address(IsMacAddress(gateway_mac)));
  EXPECT_CALL(*active_link_monitor_,
              set_gateway_supports_unicast_arp(kGatewayUnicastArpSupport));
  EXPECT_CALL(*active_link_monitor_,
              Start(ActiveLinkMonitor::kFastTestPeriodMilliseconds));
  monitor_.OnAfterResume();
  VerifyGatewayMacAddress(gateway_mac);
  Mock::VerifyAndClearExpectations(active_link_monitor_);
  Mock::VerifyAndClearExpectations(passive_link_monitor_);
}

TEST_F(LinkMonitorTest, OnActiveLinkMonitorFailure) {
  // Start link monitor.
  EXPECT_CALL(*active_link_monitor_,
              Start(ActiveLinkMonitor::kDefaultTestPeriodMilliseconds))
      .WillOnce(Return(true));
  EXPECT_TRUE(monitor_.Start());
  Mock::VerifyAndClearExpectations(active_link_monitor_);

  const int kBroadcastFailureCount = 5;
  const int kUnicastFailureCount = 3;
  const int kElapsedTimeMilliseconds = 5000;

  // Active monitor failed after 5 seconds.
  EXPECT_CALL(observer_, OnFailureCallback()).Times(1);
  EXPECT_CALL(metrics_,
              SendEnumToUMA(HasSubstr("LinkMonitorFailure"),
                            Metrics::kLinkMonitorFailureThresholdReached, _));
  EXPECT_CALL(metrics_, SendToUMA(HasSubstr("LinkMonitorSecondsToFailure"),
                                  kElapsedTimeMilliseconds / 1000, _, _, _));
  EXPECT_CALL(metrics_, SendToUMA(HasSubstr("BroadcastErrorsAtFailure"),
                                  kBroadcastFailureCount, _, _, _));
  EXPECT_CALL(metrics_, SendToUMA(HasSubstr("UnicastErrorsAtFailure"),
                                  kUnicastFailureCount, _, _, _));
  AdvanceTime(kElapsedTimeMilliseconds);
  TriggerActiveLinkMonitorFailure(Metrics::kLinkMonitorFailureThresholdReached,
                                  kBroadcastFailureCount, kUnicastFailureCount);
}

TEST_F(LinkMonitorTest, OnActiveLinkMonitorSuccess) {
  ByteString gateway_mac(kGatewayMacAddress, base::size(kGatewayMacAddress));
  EXPECT_CALL(*active_link_monitor_, gateway_mac_address())
      .WillRepeatedly(ReturnRef(gateway_mac));

  // Active link monitor succeed for the first time, gateway MAC address will be
  // updated.
  EXPECT_CALL(observer_, OnGatewayChangeCallback()).Times(1);
  EXPECT_CALL(*passive_link_monitor_,
              Start(PassiveLinkMonitor::kDefaultMonitorCycles))
      .Times(1);
  TriggerActiveLinkMonitorSuccess();
  VerifyGatewayMacAddress(gateway_mac);
  Mock::VerifyAndClearExpectations(&observer_);
  Mock::VerifyAndClearExpectations(passive_link_monitor_);

  // Active link monitor succeed again, gateway MAC address not changed.
  EXPECT_CALL(observer_, OnGatewayChangeCallback()).Times(0);
  EXPECT_CALL(*passive_link_monitor_,
              Start(PassiveLinkMonitor::kDefaultMonitorCycles))
      .Times(1);
  TriggerActiveLinkMonitorSuccess();
  VerifyGatewayMacAddress(gateway_mac);
  Mock::VerifyAndClearExpectations(&observer_);
  Mock::VerifyAndClearExpectations(passive_link_monitor_);
}

TEST_F(LinkMonitorTest, OnPassiveLinkMonitorResultCallback) {
  // Active link monitor should start regardless of the result of the passive
  // link monitor.

  EXPECT_CALL(*active_link_monitor_,
              Start(ActiveLinkMonitor::kDefaultTestPeriodMilliseconds));
  TriggerPassiveLinkMonitorResultCallback(true);
  Mock::VerifyAndClearExpectations(active_link_monitor_);

  EXPECT_CALL(*active_link_monitor_,
              Start(ActiveLinkMonitor::kDefaultTestPeriodMilliseconds));
  TriggerPassiveLinkMonitorResultCallback(false);
  Mock::VerifyAndClearExpectations(active_link_monitor_);
}

}  // namespace shill
