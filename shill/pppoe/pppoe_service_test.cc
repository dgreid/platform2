// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/pppoe/pppoe_service.h"

#include <map>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/memory/ref_counted.h>

#include "shill/dhcp/mock_dhcp_config.h"
#include "shill/dhcp/mock_dhcp_provider.h"
#include "shill/ethernet/mock_ethernet.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_external_task.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_process_manager.h"
#include "shill/ppp_device.h"
#include "shill/ppp_device_factory.h"
#include "shill/service.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"

using std::map;
using std::string;
using testing::_;
using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::StrEq;
using testing::WithoutArgs;

namespace shill {

class PPPoEServiceTest : public testing::Test {
 public:
  PPPoEServiceTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        ethernet_(new NiceMock<MockEthernet>(
            &manager_, "ethernet", "aabbccddeeff", 0)),
        device_info_(&manager_) {
    Service::SetNextSerialNumberForTesting(0);
    service_ =
        new PPPoEService(&manager_, ethernet_->weak_ptr_factory_.GetWeakPtr());
    manager_.set_mock_device_info(&device_info_);
    service_->process_manager_ = &process_manager_;
  }
  PPPoEServiceTest(const PPPoEServiceTest&) = delete;
  PPPoEServiceTest& operator=(const PPPoEServiceTest&) = delete;

  ~PPPoEServiceTest() override {
    Error error;
    service_->Disconnect(&error, __func__);
    dispatcher_.DispatchPendingEvents();
  }

 protected:
  void FakeConnectionSuccess() {
    EXPECT_CALL(*ethernet_, link_up()).WillRepeatedly(Return(true));
    EXPECT_CALL(process_manager_, StartProcess(_, _, _, _, _, _))
        .WillOnce(Return(0));
    Error error;
    service_->Connect(&error, "in test");
    EXPECT_TRUE(error.IsSuccess());
    service_->SetState(Service::kStateConnected);
  }

  bool IsAuthenticating() { return service_->authenticating_; }

  void OnPPPDied(pid_t pid, int exit) { service_->OnPPPDied(pid, exit); }

  int max_failure() { return service_->max_failure_; }

  const PPPDeviceRefPtr& device() { return service_->ppp_device_; }

  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockControl> control_interface_;
  NiceMock<MockProcessManager> process_manager_;
  NiceMock<MockManager> manager_;
  scoped_refptr<NiceMock<MockEthernet>> ethernet_;
  NiceMock<MockDeviceInfo> device_info_;

  scoped_refptr<PPPoEService> service_;
};

MATCHER_P(LinkNamed, name, "") {
  return arg->link_name() == name;
}

TEST_F(PPPoEServiceTest, LogName) {
  EXPECT_EQ("ppoe_0", service_->log_name());
}

TEST_F(PPPoEServiceTest, AuthenticationFailure) {
  EXPECT_CALL(*ethernet_, link_up()).WillRepeatedly(Return(true));
  FakeConnectionSuccess();
  map<string, string> empty_dict;
  service_->Notify(kPPPReasonAuthenticating, empty_dict);

  auto previous_state = service_->state();
  service_->Notify(kPPPReasonDisconnect, empty_dict);

  // First max_failure - 1 failures should not do anything; pppd will retry
  // the connection.
  for (int i = 1; i < max_failure(); ++i) {
    EXPECT_EQ(service_->state(), previous_state);
    service_->Notify(kPPPReasonDisconnect, empty_dict);
  }

  // Last auth failure should lead to the connection failing after pppd
  // terminates itself.
  OnPPPDied(0, 0);
  EXPECT_NE(service_->state(), previous_state);
  EXPECT_EQ(service_->state(), Service::kStateFailure);
  EXPECT_EQ(service_->failure(), Service::kFailurePPPAuth);
}

TEST_F(PPPoEServiceTest, DisconnectBeforeConnect) {
  EXPECT_CALL(*ethernet_, link_up()).WillRepeatedly(Return(true));
  FakeConnectionSuccess();
  map<string, string> empty_dict;
  service_->Notify(kPPPReasonAuthenticating, empty_dict);
  service_->Notify(kPPPReasonAuthenticated, empty_dict);

  constexpr int kExitCode = 10;
  service_->Notify(kPPPReasonDisconnect, empty_dict);
  OnPPPDied(0, kExitCode);
  EXPECT_EQ(service_->state(), Service::kStateFailure);
  EXPECT_EQ(service_->failure(), PPPDevice::ExitStatusToFailure(kExitCode));
}

TEST_F(PPPoEServiceTest, ConnectFailsWhenEthernetLinkDown) {
  EXPECT_CALL(*ethernet_, link_up()).WillRepeatedly(Return(false));

  Error error;
  service_->Connect(&error, "in test");
  EXPECT_FALSE(error.IsSuccess());
}

TEST_F(PPPoEServiceTest, OnPPPConnected) {
  static const char kLinkName[] = "ppp0";
  map<string, string> params = {{kPPPInterfaceName, kLinkName}};

  EXPECT_CALL(device_info_, GetIndex(StrEq(kLinkName))).WillOnce(Return(0));
  EXPECT_CALL(device_info_, RegisterDevice(_));
#ifndef DISABLE_DHCPV6
  // Lead to the creation of a mock rather than real DHCPConfig to avoid trying
  // to create a dhcpcd process.
  NiceMock<MockDHCPProvider> dhcp_provider;
  EXPECT_CALL(dhcp_provider, CreateIPv6Config(_, _))
      .WillOnce(
          Return(new NiceMock<MockDHCPConfig>(&control_interface_, kLinkName)));
  EXPECT_CALL(manager_, IsDHCPv6EnabledForDevice(StrEq(kLinkName)))
      .WillOnce(WithoutArgs(Invoke([this, &dhcp_provider]() {
        this->device()->set_dhcp_provider(&dhcp_provider);
        return true;
      })));
#endif  // DISABLE_DHCPV6
  EXPECT_CALL(manager_, OnInnerDevicesChanged());
  service_->OnPPPConnected(params);
  Mock::VerifyAndClearExpectations(&manager_);

  // Note that crbug.com/1030324 precludes the ability of VirtualDevices to be
  // enabled(). running() suffices here.
  EXPECT_TRUE(device()->running());
  EXPECT_EQ(device()->selected_service(), service_);
  ASSERT_NE(device()->ipconfig(), nullptr);
  EXPECT_FALSE(device()->ipconfig()->properties().blackhole_ipv6);
#ifndef DISABLE_DHCPV6
  EXPECT_NE(device()->dhcpv6_config(), nullptr);
#endif  // DISABLE_DHCPV6
}

TEST_F(PPPoEServiceTest, Connect) {
  static const char kLinkName[] = "ppp0";

  EXPECT_CALL(*ethernet_, link_up()).WillRepeatedly(Return(true));
  EXPECT_CALL(device_info_, GetIndex(StrEq(kLinkName))).WillOnce(Return(0));
  EXPECT_CALL(device_info_, RegisterDevice(LinkNamed(kLinkName)));

  FakeConnectionSuccess();
  map<string, string> empty_dict;
  service_->Notify(kPPPReasonAuthenticating, empty_dict);
  service_->Notify(kPPPReasonAuthenticated, empty_dict);

  map<string, string> connect_dict = {
      {kPPPInterfaceName, kLinkName},
  };
  service_->Notify(kPPPReasonConnect, connect_dict);
  EXPECT_EQ(service_->state(), Service::kStateOnline);
}

TEST_F(PPPoEServiceTest, Disconnect) {
  FakeConnectionSuccess();

  auto weak_ptr = service_->weak_ptr_factory_.GetWeakPtr();
  MockExternalTask* pppd =
      new MockExternalTask(&control_interface_, &process_manager_, weak_ptr,
                           base::Bind(&PPPoEService::OnPPPDied, weak_ptr));
  service_->pppd_.reset(pppd);

  PPPDevice* ppp_device =
      PPPDeviceFactory::GetInstance()->CreatePPPDevice(&manager_, "ppp0", 0);
  service_->ppp_device_ = ppp_device;
  ppp_device->SelectService(service_);

  EXPECT_CALL(*pppd, OnDelete());

  Error error;
  service_->Disconnect(&error, "in test");
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(ppp_device->selected_service(), nullptr);
}

TEST_F(PPPoEServiceTest, DisconnectDuringAssociation) {
  FakeConnectionSuccess();

  Error error;
  service_->Disconnect(&error, "in test");
  EXPECT_TRUE(error.IsSuccess());

  EXPECT_EQ(service_->state(), Service::kStateIdle);
}

}  // namespace shill
