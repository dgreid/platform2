// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/device_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/network/fake_process_runner.h"
#include "arc/network/fake_shill_client.h"
#include "arc/network/mock_datapath.h"
#include "arc/network/net_util.h"

namespace arc_networkd {

namespace {

class MockDeviceManager : public DeviceManager {
 public:
  MockDeviceManager(ShillClient* shill_client,
                    AddressManager* addr_mgr,
                    Datapath* datapath,
                    TrafficForwarder* forwarder)
      : DeviceManager(shill_client, addr_mgr, datapath, forwarder) {}
  ~MockDeviceManager() = default;

  MOCK_CONST_METHOD1(IsMulticastInterface, bool(const std::string& ifname));
};

class FakeAddressManager : public AddressManager {
 public:
  FakeAddressManager()
      : AddressManager({
            AddressManager::Guest::ARC,
            AddressManager::Guest::ARC_NET,
            AddressManager::Guest::CONTAINER,
            AddressManager::Guest::VM_ARC,
            AddressManager::Guest::VM_TERMINA,
        }) {}

  MacAddress GenerateMacAddress() override {
    static const MacAddress kAddr = {0xf7, 0x69, 0xe5, 0xc4, 0x1f, 0x74};
    return kAddr;
  }
};

class FakeTrafficForwarder : public TrafficForwarder {
 public:
  FakeTrafficForwarder() = default;
  void StartForwarding(
      const std::string&, const std::string&, uint32_t, bool, bool) override {}
  void StopForwarding(const std::string&,
                      const std::string&,
                      bool,
                      bool) override {}
  bool ForwardsLegacyIPv6() const override { return false; }
};

class DeviceManagerTest : public testing::Test {
 protected:
  DeviceManagerTest() = default;

  void SetUp() override {
    fpr_ = std::make_unique<FakeProcessRunner>();
    fpr_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(fpr_.get());
  }

  void TearDown() override {
    // dtor of base::FileDescriptorWatcher::Controller in ShillClient will
    // transfer the ownership of watcher_ to a delete task on MessageLoop.
    // Therefore we need to wait for MessageLoop to finish all the tasks to
    // fully release memory.
    base::RunLoop().RunUntilIdle();
  }

  std::unique_ptr<MockDeviceManager> NewManager() {
    shill_helper_ = std::make_unique<FakeShillClientHelper>();
    shill_client_ = shill_helper_->FakeClient();

    auto mgr = std::make_unique<MockDeviceManager>(
        shill_client_.get(), &addr_mgr_, datapath_.get(), &forwarder_);
    return mgr;
  }

  std::unique_ptr<FakeShillClient> shill_client_;

 private:
  std::unique_ptr<FakeShillClientHelper> shill_helper_;
  FakeAddressManager addr_mgr_;
  FakeTrafficForwarder forwarder_;

  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<FakeProcessRunner> fpr_;

  // This is required because of the RT netlink handler.
  base::MessageLoopForIO msg_loop_;
  base::FileDescriptorWatcher watcher_{&msg_loop_};
};

}  // namespace

TEST_F(DeviceManagerTest, MakeEthernetDevices) {
  auto mgr = NewManager();
  auto eth0 = mgr->MakeDevice("eth0");
  const auto& cfg = eth0->config();
  EXPECT_EQ(cfg.host_ifname(), "arc_eth0");
  EXPECT_EQ(cfg.guest_ifname(), "eth0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 9));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 10));
  EXPECT_TRUE(eth0->options().ipv6_enabled);
  EXPECT_FALSE(eth0->options().is_android);
  EXPECT_FALSE(eth0->options().use_default_interface);
  EXPECT_FALSE(eth0->options().is_sticky);

  auto usb0 = mgr->MakeDevice("usb0");
  const auto& cfgu = usb0->config();
  EXPECT_EQ(cfgu.host_ifname(), "arc_usb0");
  EXPECT_EQ(cfgu.guest_ifname(), "usb0");
  EXPECT_EQ(cfgu.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 13));
  EXPECT_EQ(cfgu.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 14));
  EXPECT_TRUE(usb0->options().ipv6_enabled);
  EXPECT_FALSE(usb0->options().is_android);
  EXPECT_FALSE(usb0->options().use_default_interface);
  EXPECT_FALSE(usb0->options().is_sticky);
}

TEST_F(DeviceManagerTest, MakeWifiDevices) {
  auto mgr = NewManager();
  auto wlan0 = mgr->MakeDevice("wlan0");
  const auto& cfg = wlan0->config();
  EXPECT_EQ(cfg.host_ifname(), "arc_wlan0");
  EXPECT_EQ(cfg.guest_ifname(), "wlan0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 9));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 10));
  EXPECT_TRUE(wlan0->options().ipv6_enabled);
  EXPECT_FALSE(wlan0->options().is_android);
  EXPECT_FALSE(wlan0->options().use_default_interface);
  EXPECT_FALSE(wlan0->options().is_sticky);

  auto mlan0 = mgr->MakeDevice("mlan0");
  const auto& cfgm = mlan0->config();
  EXPECT_EQ(cfgm.host_ifname(), "arc_mlan0");
  EXPECT_EQ(cfgm.guest_ifname(), "mlan0");
  EXPECT_EQ(cfgm.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 13));
  EXPECT_EQ(cfgm.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 14));
  EXPECT_TRUE(mlan0->options().ipv6_enabled);
  EXPECT_FALSE(mlan0->options().is_android);
  EXPECT_FALSE(mlan0->options().use_default_interface);
  EXPECT_FALSE(mlan0->options().is_sticky);
}

TEST_F(DeviceManagerTest, MakeCellularDevice) {
  auto mgr = NewManager();
  auto wwan0 = mgr->MakeDevice("wwan0");
  const auto& cfg = wwan0->config();
  EXPECT_EQ(cfg.host_ifname(), "arc_wwan0");
  EXPECT_EQ(cfg.guest_ifname(), "wwan0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 9));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 10));
  EXPECT_FALSE(wwan0->options().ipv6_enabled);
  EXPECT_FALSE(wwan0->options().is_android);
  EXPECT_FALSE(wwan0->options().use_default_interface);
  EXPECT_FALSE(wwan0->options().is_sticky);
}

TEST_F(DeviceManagerTest, MakeDevice_Android) {
  auto mgr = NewManager();
  auto arc0 = mgr->MakeDevice(kAndroidDevice);
  const auto& cfg = arc0->config();
  EXPECT_EQ(cfg.host_ifname(), "arcbr0");
  EXPECT_EQ(cfg.guest_ifname(), "arc0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 1));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 2));
  EXPECT_FALSE(arc0->options().ipv6_enabled);
  EXPECT_TRUE(arc0->options().is_android);
  EXPECT_FALSE(arc0->options().use_default_interface);
  EXPECT_TRUE(arc0->options().is_sticky);
}

TEST_F(DeviceManagerTest, MakeDevice_LegacyAndroid) {
  auto mgr = NewManager();
  auto arc0 = mgr->MakeDevice(kAndroidLegacyDevice);
  const auto& cfg = arc0->config();
  EXPECT_EQ(cfg.host_ifname(), "arcbr0");
  EXPECT_EQ(cfg.guest_ifname(), "arc0");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 1));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 2));
  EXPECT_TRUE(arc0->options().ipv6_enabled);
  EXPECT_TRUE(arc0->options().is_android);
  EXPECT_TRUE(arc0->options().use_default_interface);
  EXPECT_TRUE(arc0->options().is_sticky);
}

TEST_F(DeviceManagerTest, MakeDevice_AndroidVm) {
  auto mgr = NewManager();
  auto arc0 = mgr->MakeDevice(kAndroidVmDevice);
  const auto& cfg = arc0->config();
  EXPECT_EQ(cfg.host_ifname(), "arc_br1");
  EXPECT_EQ(cfg.guest_ifname(), "arc1");
  EXPECT_EQ(cfg.host_ipv4_addr(), Ipv4Addr(100, 115, 92, 5));
  EXPECT_EQ(cfg.guest_ipv4_addr(), Ipv4Addr(100, 115, 92, 6));
  EXPECT_TRUE(arc0->options().ipv6_enabled);
  EXPECT_TRUE(arc0->options().is_android);
  EXPECT_TRUE(arc0->options().use_default_interface);
  EXPECT_TRUE(arc0->options().is_sticky);
}

TEST_F(DeviceManagerTest, MakeDevice_NoMoreSubnets) {
  auto mgr = NewManager();
  std::vector<std::unique_ptr<Device>> devices;
  for (int i = 0; i < 4; ++i) {
    auto dev = mgr->MakeDevice(base::StringPrintf("%d", i));
    EXPECT_TRUE(dev);
    devices.emplace_back(std::move(dev));
  }
  EXPECT_FALSE(mgr->MakeDevice("x"));
}

TEST_F(DeviceManagerTest, AddNewDevices) {
  auto mgr = NewManager();
  mgr->OnGuestStart(GuestMessage::ARC);
  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("eth0"),
                                           dbus::ObjectPath("wlan0")};
  auto value = brillo::Any(devices);
  shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  EXPECT_TRUE(mgr->Exists("eth0"));
  EXPECT_TRUE(mgr->Exists("wlan0"));
}

TEST_F(DeviceManagerTest, PreviousDevicesRemoved) {
  auto mgr = NewManager();
  {
    std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("eth0"),
                                             dbus::ObjectPath("wlan0")};
    auto value = brillo::Any(devices);
    shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
    EXPECT_TRUE(mgr->Exists("eth0"));
    EXPECT_TRUE(mgr->Exists("wlan0"));
  }
  {
    std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("eth0"),
                                             dbus::ObjectPath("eth1")};
    auto value = brillo::Any(devices);
    shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
    EXPECT_TRUE(mgr->Exists("eth0"));
    EXPECT_TRUE(mgr->Exists("eth1"));
    EXPECT_FALSE(mgr->Exists("wlan0"));
  }
}

TEST_F(DeviceManagerTest, MakeDeviceNonAndroid_CheckMulticast) {
  auto mgr = NewManager();

  EXPECT_CALL(*mgr, IsMulticastInterface("dev0")).WillOnce(Return(true));
  auto dev0 = mgr->MakeDevice("dev0");
  EXPECT_TRUE(dev0->options().fwd_multicast);
  dev0.reset();

  EXPECT_CALL(*mgr, IsMulticastInterface("dev1")).WillOnce(Return(false));
  auto dev1 = mgr->MakeDevice("dev1");
  EXPECT_FALSE(dev1->options().fwd_multicast);
  dev1.reset();
}

TEST_F(DeviceManagerTest, MakeDeviceAndroid_CheckMulticast) {
  auto mgr = NewManager();
  EXPECT_CALL(*mgr, IsMulticastInterface(_)).Times(0);

  auto arc0 = mgr->MakeDevice(kAndroidDevice);
  EXPECT_FALSE(arc0->options().fwd_multicast);
  arc0.reset();

  auto android = mgr->MakeDevice(kAndroidLegacyDevice);
  EXPECT_TRUE(android->options().fwd_multicast);
  android.reset();

  auto arcvm = mgr->MakeDevice(kAndroidVmDevice);
  EXPECT_TRUE(arcvm->options().fwd_multicast);
  arcvm.reset();
}

TEST_F(DeviceManagerTest, StickyDevicesNotRemovedByShill) {
  auto mgr = NewManager();
  mgr->Add("arc0");
  EXPECT_TRUE(mgr->Exists("arc0"));

  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("eth0")};
  auto value = brillo::Any(devices);
  shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty, value);
  EXPECT_TRUE(mgr->Exists("eth0"));
  EXPECT_TRUE(mgr->Exists("arc0"));
}

TEST_F(DeviceManagerTest, StickyDevicesCanBeRemovedManually) {
  auto mgr = NewManager();
  mgr->Add("arc0");
  EXPECT_TRUE(mgr->Exists("arc0"));

  mgr->Remove("arc0");
  EXPECT_FALSE(mgr->Exists("arc0"));
}

}  // namespace arc_networkd
