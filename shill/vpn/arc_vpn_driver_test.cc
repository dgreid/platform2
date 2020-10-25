// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/arc_vpn_driver.h"

#include <memory>

#include <base/bind.h>
#include <base/memory/ptr_util.h>
#include <gtest/gtest.h>

#include "shill/fake_store.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_virtual_device.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/mock_vpn_provider.h"
#include "shill/vpn/mock_vpn_service.h"

using testing::_;
using testing::NiceMock;

namespace shill {

namespace {

const char kInterfaceName[] = "arcbr0";
const int kInterfaceIndex = 123;
const char kStorageId[] = "dummystorage";

}  // namespace

class ArcVpnDriverTest : public testing::Test {
 public:
  ArcVpnDriverTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        device_info_(&manager_),
        device_(new MockVirtualDevice(
            &manager_, kInterfaceName, kInterfaceIndex, Technology::kVPN)),
        store_(),
        driver_(new ArcVpnDriver(&manager_, nullptr)),
        service_(new MockVPNService(&manager_, base::WrapUnique(driver_))) {}

  ~ArcVpnDriverTest() override = default;

  void SetUp() override {
    manager_.vpn_provider_ = std::make_unique<MockVPNProvider>();
    manager_.vpn_provider_->manager_ = &manager_;
    manager_.vpn_provider_->arc_device_ = device_;
    manager_.user_traffic_uids_.push_back(1000);
    manager_.UpdateProviderMapping();
  }

  void TearDown() override {
    manager_.vpn_provider_->arc_device_ = nullptr;
    manager_.vpn_provider_.reset();
    driver_->set_service(nullptr);
    device_ = nullptr;
  }

  void LoadPropertiesFromStore(bool tunnel_chrome) {
    const std::string kProviderHostValue = "arcvpn";
    const std::string kProviderTypeValue = "arcvpn";

    store_.SetString(kStorageId, kProviderHostProperty, kProviderHostValue);
    store_.SetString(kStorageId, kProviderTypeProperty, kProviderTypeValue);
    store_.SetString(kStorageId, kArcVpnTunnelChromeProperty,
                     tunnel_chrome ? "true" : "false");
    driver_->Load(&store_, kStorageId);
  }

 protected:
  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  NiceMock<MockDeviceInfo> device_info_;
  scoped_refptr<MockVirtualDevice> device_;
  FakeStore store_;
  ArcVpnDriver* driver_;  // Owned by |service_|
  scoped_refptr<MockVPNService> service_;
};

TEST_F(ArcVpnDriverTest, ConnectAsync) {
  LoadPropertiesFromStore(true);
  EXPECT_CALL(*service_,
              OnDriverEvent(VPNService::kEventConnectionSuccess, _, _))
      .Times(1);
  driver_->ConnectAsync(service_->GetCallback());
  dispatcher_.task_environment().RunUntilIdle();
}

TEST_F(ArcVpnDriverTest, GetIPProperties) {
  auto ip_properties = driver_->GetIPProperties();
  EXPECT_TRUE(ip_properties.blackhole_ipv6);
  EXPECT_FALSE(ip_properties.default_route);
}

}  // namespace shill
