// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/arc_service.h"

#include <utility>
#include <vector>

#include <base/bind.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/network/device_manager.h"
#include "arc/network/fake_process_runner.h"
#include "arc/network/fake_shill_client.h"
#include "arc/network/mock_datapath.h"
#include "arc/network/net_util.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;

namespace arc_networkd {
namespace {
constexpr pid_t kTestPID = -2;
constexpr uint32_t kTestCID = 2;
constexpr uint32_t kArcHostIP = Ipv4Addr(100, 115, 92, 1);
constexpr uint32_t kArcGuestIP = Ipv4Addr(100, 115, 92, 2);

static AddressManager addr_mgr({
    AddressManager::Guest::ARC,
    AddressManager::Guest::ARC_NET,
});

class MockDeviceManager : public DeviceManagerBase {
 public:
  MockDeviceManager() = default;
  ~MockDeviceManager() = default;

  MOCK_METHOD2(RegisterDeviceAddedHandler,
               void(GuestMessage::GuestType, const DeviceHandler&));
  MOCK_METHOD2(RegisterDeviceRemovedHandler,
               void(GuestMessage::GuestType, const DeviceHandler&));
  MOCK_METHOD2(RegisterDefaultInterfaceChangedHandler,
               void(GuestMessage::GuestType,
                    const ShillClient::DefaultInterfaceChangeHandler&));
  MOCK_METHOD2(RegisterDeviceIPv6AddressFoundHandler,
               void(GuestMessage::GuestType, const DeviceHandler&));
  MOCK_METHOD1(UnregisterAllGuestHandlers, void(GuestMessage::GuestType));
  MOCK_METHOD1(OnGuestStart, void(GuestMessage::GuestType));
  MOCK_METHOD1(OnGuestStop, void(GuestMessage::GuestType));
  MOCK_METHOD1(ProcessDevices, void(const DeviceHandler&));
  MOCK_CONST_METHOD1(Exists, bool(const std::string& name));
  MOCK_CONST_METHOD1(FindByHostInterface, Device*(const std::string& ifname));
  MOCK_CONST_METHOD1(FindByGuestInterface, Device*(const std::string& ifname));
  MOCK_CONST_METHOD0(DefaultInterface, const std::string&());
  MOCK_METHOD1(Add, bool(const std::string&));
  MOCK_METHOD2(AddWithContext,
               bool(const std::string&, std::unique_ptr<Device::Context>));
  MOCK_METHOD1(Remove, bool(const std::string&));
  MOCK_METHOD1(StartForwarding, void(const Device&));
  MOCK_METHOD1(StopForwarding, void(const Device&));
  MOCK_CONST_METHOD0(addr_mgr, AddressManager*());
};

std::unique_ptr<Device> MakeDevice(const std::string& name,
                                   const std::string& host,
                                   const std::string& guest,
                                   bool is_arc = true) {
  Device::Options opt{
      .use_default_interface = (name == kAndroidLegacyDevice),
      .is_android = (name == kAndroidDevice) || (name == kAndroidLegacyDevice),
  };
  auto subnet = addr_mgr.AllocateIPv4Subnet(
      opt.is_android ? AddressManager::Guest::ARC
                     : AddressManager::Guest::ARC_NET);
  auto addr0 = subnet->AllocateAtOffset(0);
  auto addr1 = subnet->AllocateAtOffset(1);
  auto cfg = std::make_unique<Device::Config>(
      host, guest, addr_mgr.GenerateMacAddress(), std::move(subnet),
      std::move(addr0), std::move(addr1));
  return std::make_unique<Device>(
      name, std::move(cfg), opt,
      is_arc ? GuestMessage::ARC : GuestMessage::UNKNOWN_GUEST);
}

}  // namespace

class ArcServiceTest : public testing::Test {
 public:
  ArcServiceTest()
      : testing::Test(),
        addr_mgr_({
            AddressManager::Guest::ARC,
            AddressManager::Guest::ARC_NET,
        }) {}

 protected:
  void SetUp() override {
    runner_ = std::make_unique<FakeProcessRunner>();
    runner_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(runner_.get());
    shill_client_ = shill_helper_.Client();
  }

  std::unique_ptr<ArcService> NewService(bool arc_legacy = false) {
    EXPECT_CALL(dev_mgr_, RegisterDeviceAddedHandler(_, _));
    EXPECT_CALL(dev_mgr_, RegisterDeviceRemovedHandler(_, _));
    EXPECT_CALL(dev_mgr_, RegisterDefaultInterfaceChangedHandler(_, _));
    EXPECT_CALL(dev_mgr_, UnregisterAllGuestHandlers(_));

    arc_networkd::test::guest =
        arc_legacy ? GuestMessage::ARC_LEGACY : GuestMessage::ARC;
    return std::make_unique<ArcService>(shill_client_.get(), &dev_mgr_,
                                        datapath_.get());
  }

  FakeShillClientHelper shill_helper_;
  std::unique_ptr<ShillClient> shill_client_;
  AddressManager addr_mgr_;
  MockDeviceManager dev_mgr_;
  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<FakeProcessRunner> runner_;
};

TEST_F(ArcServiceTest, VerifyOnDeviceAddedDatapathForLegacyAndroid) {
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddLegacyIPv4DNAT(StrEq("100.115.92.2")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddOutboundIPv4(StrEq("arcbr0")))
      .WillOnce(Return(true));

  auto dev = MakeDevice(kAndroidLegacyDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  NewService(true)->OnDeviceAdded(dev.get());
}

TEST_F(ArcServiceTest,
       VerifyOnDeviceAddedDoesNothingLegacyAndroidOtherInterface) {
  EXPECT_CALL(*datapath_, AddBridge(_, _, _)).Times(0);
  EXPECT_CALL(*datapath_, AddLegacyIPv4DNAT(_)).Times(0);
  EXPECT_CALL(*datapath_, AddOutboundIPv4(_)).Times(0);

  // In ARC N, only the legacy interface is added.
  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  NewService(true)->OnDeviceAdded(dev.get());
}

TEST_F(ArcServiceTest, VerifyOnDeviceAddedDoesNothingNonArcDevice) {
  EXPECT_CALL(*datapath_, AddBridge(_, _, _)).Times(0);
  EXPECT_CALL(*datapath_, AddLegacyIPv4DNAT(_)).Times(0);
  EXPECT_CALL(*datapath_, AddOutboundIPv4(_)).Times(0);

  auto dev = MakeDevice("foo0", "bar0", "baz0", false);
  ASSERT_TRUE(dev);
  NewService()->OnDeviceAdded(dev.get());
}

TEST_F(ArcServiceTest, VerifyOnDeviceRemovedDatapathForLegacyAndroid) {
  EXPECT_CALL(*datapath_, RemoveOutboundIPv4(StrEq("arcbr0")));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("veth_arc0")));

  auto dev = MakeDevice(kAndroidLegacyDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  NewService()->OnDeviceRemoved(dev.get());
}

TEST_F(ArcServiceTest, VerifyOnDeviceAddedDatapathForAndroid) {
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));

  auto dev = MakeDevice(kAndroidDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  NewService()->OnDeviceAdded(dev.get());
}

TEST_F(ArcServiceTest, VerifyOnDeviceRemovedDatapathForAndroid) {
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("veth_arc0")));

  auto dev = MakeDevice(kAndroidDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  NewService()->OnDeviceRemoved(dev.get());
}

TEST_F(ArcServiceTest, VerifyOnDeviceAddedDatapath) {
  EXPECT_CALL(*datapath_,
              AddBridge(StrEq("arc_eth0"), Ipv4Addr(100, 115, 92, 9), 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(StrEq("eth0"), StrEq("100.115.92.10")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddOutboundIPv4(StrEq("arc_eth0")))
      .WillOnce(Return(true));

  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  NewService()->OnDeviceAdded(dev.get());
}

TEST_F(ArcServiceTest, VerifyOnDeviceRemovedDatapath) {
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0")));

  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  NewService()->OnDeviceRemoved(dev.get());
}

// ContainerImpl

class ContainerImplTest : public testing::Test {
 public:
  ContainerImplTest()
      : testing::Test(),
        addr_mgr_({
            AddressManager::Guest::ARC,
            AddressManager::Guest::ARC_NET,
        }) {}

 protected:
  void SetUp() override {
    runner_ = std::make_unique<FakeProcessRunner>();
    runner_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(runner_.get());
  }

  std::unique_ptr<ArcService::ContainerImpl> Impl(bool arc_legacy = false) {
    auto impl = std::make_unique<ArcService::ContainerImpl>(
        &dev_mgr_, datapath_.get(),
        arc_legacy ? GuestMessage::ARC_LEGACY : GuestMessage::ARC);
    impl->Start(kTestPID);
    return impl;
  }

  AddressManager addr_mgr_;
  MockDeviceManager dev_mgr_;
  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<FakeProcessRunner> runner_;
};

TEST_F(ContainerImplTest, OnStartDevice_LegacyAndroid) {
  EXPECT_CALL(*datapath_,
              AddVirtualInterfacePair(StrEq("veth_arc0"), StrEq("arc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConfigureInterface(StrEq("arc0"), _, kArcGuestIP, 30, true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, ToggleInterface(StrEq("veth_arc0"), true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("veth_arc0")))
      .WillOnce(Return(true));

  auto dev = MakeDevice(kAndroidLegacyDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  EXPECT_TRUE(Impl(true)->OnStartDevice(dev.get()));
  runner_->VerifyWriteSentinel(kTestPID);
}

TEST_F(ContainerImplTest, OnStartDevice_FailsToCreateInterface_LegacyAndroid) {
  EXPECT_CALL(*datapath_,
              AddVirtualInterfacePair(StrEq("veth_arc0"), StrEq("arc0")))
      .WillOnce(Return(false));
  EXPECT_CALL(*datapath_, ConfigureInterface(_, _, _, _, _, _)).Times(0);
  auto dev = MakeDevice(kAndroidLegacyDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  EXPECT_FALSE(Impl(true)->OnStartDevice(dev.get()));
}

TEST_F(ContainerImplTest,
       OnStartDevice_FailsToConfigureInterface_LegacyAndroid) {
  EXPECT_CALL(*datapath_,
              AddVirtualInterfacePair(StrEq("veth_arc0"), StrEq("arc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConfigureInterface(StrEq("arc0"), _, kArcGuestIP, 30, true, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*datapath_, ToggleInterface(StrEq("veth_arc0"), true)).Times(0);
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("arc0")));
  EXPECT_CALL(*datapath_, RemoveBridge(_)).Times(0);

  auto dev = MakeDevice(kAndroidLegacyDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  EXPECT_FALSE(Impl(true)->OnStartDevice(dev.get()));
}

TEST_F(ContainerImplTest, OnStartDevice_Other) {
  EXPECT_CALL(*datapath_,
              AddVirtualInterfacePair(StrEq("veth_eth0"), StrEq("eth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConfigureInterface(StrEq("eth0"), _, Ipv4Addr(100, 115, 92, 10),
                                 30, true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, ToggleInterface(StrEq("veth_eth0"), true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("veth_eth0")))
      .WillOnce(Return(true));

  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  Impl()->OnStartDevice(dev.get());
}

// Verifies the veth interface is not removed.
TEST_F(ContainerImplTest, OnStopDevice_LegacyAndroid) {
  EXPECT_CALL(*datapath_, RemoveInterface(_)).Times(0);
  auto dev = MakeDevice(kAndroidLegacyDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  Impl(true)->OnStopDevice(dev.get());
}

// Verifies the veth interface is not removed.
TEST_F(ContainerImplTest, OnStopDevice_Android) {
  EXPECT_CALL(*datapath_, RemoveInterface(_)).Times(0);
  auto dev = MakeDevice(kAndroidDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  Impl()->OnStopDevice(dev.get());
}

TEST_F(ContainerImplTest, OnStopDevice_Other) {
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("veth_eth0")));
  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  Impl()->OnStopDevice(dev.get());
}

TEST_F(ContainerImplTest, OnDefaultInterfaceChanged_LegacyAndroid) {
  auto dev = MakeDevice(kAndroidLegacyDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  EXPECT_CALL(*datapath_, RemoveLegacyIPv4InboundDNAT);
  EXPECT_CALL(dev_mgr_, FindByGuestInterface(StrEq("arc0")))
      .WillOnce(Return(dev.get()));
  EXPECT_CALL(*datapath_, AddLegacyIPv4InboundDNAT("wlan0"))
      .WillOnce(Return(true));
  Impl(true)->OnDefaultInterfaceChanged("wlan0", "");
}

TEST_F(ContainerImplTest, OnDefaultInterfaceChanged_LegacyAndroidNoIfname) {
  auto dev = MakeDevice(kAndroidLegacyDevice, "arcbr0", "arc0");
  ASSERT_TRUE(dev);
  EXPECT_CALL(*datapath_, RemoveLegacyIPv4InboundDNAT);
  EXPECT_CALL(dev_mgr_, FindByGuestInterface(StrEq("arc0")))
      .WillOnce(Return(dev.get()));
  Impl(true)->OnDefaultInterfaceChanged("", "");
}

TEST_F(ContainerImplTest, OnDefaultInterfaceChanged_Other) {
  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  // NOTE: It doesn't matter what device is returned here as long as it's
  // non-null.
  EXPECT_CALL(dev_mgr_, FindByGuestInterface(StrEq("wlan0")))
      .WillOnce(Return(dev.get()));
  Impl()->OnDefaultInterfaceChanged("wlan0", "");
}

// Nothing happens in this case since it's only concerned about (re)connecting
// to a network.
TEST_F(ContainerImplTest, OnDefaultInterfaceChanged_OtherNoIfname) {
  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  Impl()->OnDefaultInterfaceChanged("", "");
}

// VM Impl

class VmImplTest : public testing::Test {
 public:
  VmImplTest()
      : testing::Test(),
        addr_mgr_({
            AddressManager::Guest::ARC,
            AddressManager::Guest::ARC_NET,
            AddressManager::Guest::VM_ARC,
        }) {}

 protected:
  void SetUp() override {
    runner_ = std::make_unique<FakeProcessRunner>();
    runner_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(runner_.get());
  }

  std::unique_ptr<ArcService::VmImpl> Impl() {
    auto impl =
        std::make_unique<ArcService::VmImpl>(&dev_mgr_, datapath_.get());
    impl->Start(kTestCID);
    return impl;
  }

  AddressManager addr_mgr_;
  MockDeviceManager dev_mgr_;
  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<FakeProcessRunner> runner_;
};

TEST_F(VmImplTest, OnStartDevice) {
  // For now, ARCVM uses the legacy device since it behaves similarly.
  auto dev = MakeDevice(kAndroidLegacyDevice, "arc_br1", "arc1");
  ASSERT_TRUE(dev);
  auto context = std::make_unique<ArcService::Context>();
  auto* ctx = context.get();
  dev->set_context(std::move(context));
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), nullptr, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_br1"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  static const std::string eth0 = "eth0";
  EXPECT_CALL(dev_mgr_, DefaultInterface()).WillOnce(ReturnRef(eth0));
  // OnDefaultInterfaceChanged
  EXPECT_CALL(*datapath_, RemoveLegacyIPv4InboundDNAT());
  EXPECT_CALL(dev_mgr_, FindByGuestInterface(StrEq("arc1")))
      .WillRepeatedly(Return(dev.get()));
  EXPECT_CALL(*datapath_, AddLegacyIPv4InboundDNAT(StrEq("eth0")));
  EXPECT_TRUE(Impl()->OnStartDevice(dev.get()));
  EXPECT_EQ(ctx->TAP(), "vmtap0");
}

TEST_F(VmImplTest, OnStartDevice_NoContext) {
  // For now, ARCVM uses the legacy device since it behaves similarly.
  auto dev = MakeDevice(kAndroidLegacyDevice, "arc_br1", "arc1");
  ASSERT_TRUE(dev);
  EXPECT_FALSE(Impl()->OnStartDevice(dev.get()));
}

TEST_F(VmImplTest, OnStartDevice_OtherDevice) {
  // For now, ARCVM uses the legacy device since it behaves similarly.
  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  EXPECT_FALSE(Impl()->OnStartDevice(dev.get()));
}

TEST_F(VmImplTest, OnStopDevice) {
  // For now, ARCVM uses the legacy device since it behaves similarly.
  auto dev = MakeDevice(kAndroidLegacyDevice, "arc_br1", "arc1");
  ASSERT_TRUE(dev);
  auto context = std::make_unique<ArcService::Context>();
  auto* ctx = context.get();
  ctx->SetTAP("vmtap0");
  dev->set_context(std::move(context));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap0")));
  Impl()->OnStopDevice(dev.get());
}

TEST_F(VmImplTest, OnStopDevice_OtherDevice) {
  EXPECT_CALL(*datapath_, RemoveInterface(_)).Times(0);
  // For now, ARCVM uses the legacy device since it behaves similarly.
  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  Impl()->OnStopDevice(dev.get());
}

}  // namespace arc_networkd
