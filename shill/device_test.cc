// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/device.h"

#include <ctype.h>
#include <sys/socket.h>
#include <linux/if.h>  // NOLINT - Needs typedefs from sys/socket.h.

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/macros.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/patchpanel/dbus/fake_client.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/dhcp/dhcp_provider.h"
#include "shill/dhcp/mock_dhcp_config.h"
#include "shill/dhcp/mock_dhcp_properties.h"
#include "shill/dhcp/mock_dhcp_provider.h"
#include "shill/dns_server_tester.h"
#include "shill/event_dispatcher.h"
#include "shill/fake_store.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_device_info.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_ipconfig.h"
#include "shill/mock_link_monitor.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_portal_detector.h"
#include "shill/mock_service.h"
#include "shill/mock_traffic_monitor.h"
#include "shill/net/mock_rtnl_handler.h"
#include "shill/net/mock_time.h"
#include "shill/net/ndisc.h"
#include "shill/portal_detector.h"
#include "shill/routing_table.h"
#include "shill/static_ip_parameters.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"
#include "shill/tethering.h"
#include "shill/traffic_monitor.h"

using base::Callback;
using std::string;
using std::vector;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Ref;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;

namespace shill {

class TestDevice : public Device {
 public:
  TestDevice(Manager* manager,
             const std::string& link_name,
             const std::string& address,
             int interface_index,
             Technology technology)
      : Device(manager, link_name, address, interface_index, technology) {
    ON_CALL(*this, SetIPFlag(_, _, _))
        .WillByDefault(Invoke(this, &TestDevice::DeviceSetIPFlag));
    ON_CALL(*this, IsTrafficMonitorEnabled())
        .WillByDefault(
            Invoke(this, &TestDevice::DeviceIsTrafficMonitorEnabled));
    ON_CALL(*this, StartDNSTest(_, _, _))
        .WillByDefault(Invoke(this, &TestDevice::DeviceStartDNSTest));
    ON_CALL(*this, ShouldBringNetworkInterfaceDownAfterDisabled())
        .WillByDefault(Invoke(
            this,
            &TestDevice::DeviceShouldBringNetworkInterfaceDownAfterDisabled));
  }

  ~TestDevice() override = default;

  void Start(Error* error,
             const EnabledStateChangedCallback& callback) override {
    DCHECK(error);
  }

  void Stop(Error* error,
            const EnabledStateChangedCallback& callback) override {
    DCHECK(error);
  }

  MOCK_METHOD(bool, IsTrafficMonitorEnabled, (), (const, override));
  MOCK_METHOD(bool,
              ShouldBringNetworkInterfaceDownAfterDisabled,
              (),
              (const, override));
  MOCK_METHOD(bool,
              SetIPFlag,
              (IPAddress::Family, const std::string&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              StartDNSTest,
              (const std::vector<std::string>&,
               const bool,
               const base::Callback<void(const DnsServerTester::Status)>&),
              (override));
  MOCK_METHOD(bool,
              StartConnectionDiagnosticsAfterPortalDetection,
              (const PortalDetector::Result&, const PortalDetector::Result&),
              (override));

  virtual bool DeviceIsTrafficMonitorEnabled() const {
    return Device::IsTrafficMonitorEnabled();
  }

  virtual bool DeviceSetIPFlag(IPAddress::Family family,
                               const std::string& flag,
                               const std::string& value) {
    return Device::SetIPFlag(family, flag, value);
  }

  virtual bool DeviceStartDNSTest(
      const std::vector<std::string>& dns_servers,
      const bool retry_until_success,
      const base::Callback<void(const DnsServerTester::Status)>& callback) {
    return Device::StartDNSTest(dns_servers, retry_until_success, callback);
  }

  virtual bool DeviceShouldBringNetworkInterfaceDownAfterDisabled() const {
    return Device::ShouldBringNetworkInterfaceDownAfterDisabled();
  }

  void device_set_mac_address(const std::string& mac_address) {
    Device::set_mac_address(mac_address);
  }
};

class DeviceTest : public testing::Test {
 public:
  DeviceTest()
      : manager_(control_interface(), dispatcher(), metrics()),
        device_(new NiceMock<TestDevice>(manager(),
                                         kDeviceName,
                                         kDeviceAddress,
                                         kDeviceInterfaceIndex,
                                         Technology::kUnknown)),
        device_info_(manager()) {
    manager()->set_mock_device_info(&device_info_);
    DHCPProvider::GetInstance()->control_interface_ = control_interface();
    DHCPProvider::GetInstance()->dispatcher_ = dispatcher();
    device_->time_ = &time_;

    auto client = std::make_unique<patchpanel::FakeClient>();
    patchpanel_client_ = client.get();
    manager_.patchpanel_client_ = std::move(client);
  }
  ~DeviceTest() override = default;

  void SetUp() override {
    device_->rtnl_handler_ = &rtnl_handler_;
    RoutingTable::GetInstance()->Start();
  }

 protected:
  static const char kDeviceName[];
  static const char kDeviceAddress[];
  static const int kDeviceInterfaceIndex;

  void OnIPConfigUpdated(const IPConfigRefPtr& ipconfig) {
    device_->OnIPConfigUpdated(ipconfig, true);
  }

  void OnIPConfigFailed(const IPConfigRefPtr& ipconfig) {
    device_->OnIPConfigFailed(ipconfig);
  }

  void OnIPConfigExpired(const IPConfigRefPtr& ipconfig) {
    device_->OnIPConfigExpired(ipconfig);
  }

  patchpanel::TrafficCounter CreateCounter(
      const std::valarray<uint64_t>& vals,
      patchpanel::TrafficCounter::Source source,
      const std::string& device_name) {
    EXPECT_EQ(4, vals.size());
    patchpanel::TrafficCounter counter;
    counter.set_rx_bytes(vals[0]);
    counter.set_tx_bytes(vals[1]);
    counter.set_rx_packets(vals[2]);
    counter.set_tx_packets(vals[3]);
    counter.set_source(source);
    counter.set_device(device_name);
    return counter;
  }

  void SelectService(const ServiceRefPtr service) {
    device_->SelectService(service);
  }

  void SetConnection(ConnectionRefPtr connection) {
    device_->connection_ = connection;
  }

  void SetLinkMonitor(LinkMonitor* link_monitor) {
    device_->set_link_monitor(link_monitor);  // Passes ownership.
  }

  bool HasLinkMonitor() { return device_->link_monitor(); }

  bool StartLinkMonitor() { return device_->StartLinkMonitor(); }

  void StopLinkMonitor() { device_->StopLinkMonitor(); }

  uint64_t GetLinkMonitorResponseTime(Error* error) {
    return device_->GetLinkMonitorResponseTime(error);
  }

  MockTrafficMonitor* SetTrafficMonitor(
      std::unique_ptr<MockTrafficMonitor> traffic_monitor) {
    MockTrafficMonitor* underlying_traffic_monitor = traffic_monitor.get();
    device_->set_traffic_monitor_for_test(std::move(traffic_monitor));
    return underlying_traffic_monitor;
  }

  void StartTrafficMonitor() { device_->StartTrafficMonitor(); }

  void StopTrafficMonitor() { device_->StopTrafficMonitor(); }

  void NetworkProblemDetected(int reason) {
    device_->OnEncounterNetworkProblem(reason);
  }

  DeviceMockAdaptor* GetDeviceMockAdaptor() {
    return static_cast<DeviceMockAdaptor*>(device_->adaptor_.get());
  }

  MockControl* control_interface() { return &control_interface_; }
  EventDispatcher* dispatcher() { return &dispatcher_; }
  MockMetrics* metrics() { return &metrics_; }
  MockManager* manager() { return &manager_; }

  MOCK_METHOD(void, ReliableLinkCallback, ());

  void SetReliableLinkCallback() {
    device_->reliable_link_callback_.Reset(
        base::Bind(&DeviceTest::ReliableLinkCallback, base::Unretained(this)));
  }

  bool ReliableLinkCallbackIsCancelled() {
    return device_->reliable_link_callback_.IsCancelled();
  }

  void SetupIPv6Config() {
    const char kAddress[] = "2001:db8::1";
    const char kDnsServer1[] = "2001:db8::2";
    const char kDnsServer2[] = "2001:db8::3";
    IPConfig::Properties properties;
    properties.address = kAddress;
    properties.dns_servers = {kDnsServer1, kDnsServer2};

    device_->ip6config_ =
        new NiceMock<MockIPConfig>(control_interface(), kDeviceName);
    device_->ip6config_->set_properties(properties);
  }

  bool SetHostname(const string& hostname) {
    return device_->SetHostname(hostname);
  }

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;

  scoped_refptr<TestDevice> device_;
  NiceMock<MockDeviceInfo> device_info_;
  MockTime time_;
  StrictMock<MockRTNLHandler> rtnl_handler_;
  patchpanel::FakeClient* patchpanel_client_;
};

const char DeviceTest::kDeviceName[] = "testdevice";
const char DeviceTest::kDeviceAddress[] = "address";
const int DeviceTest::kDeviceInterfaceIndex = 0;

TEST_F(DeviceTest, Contains) {
  EXPECT_TRUE(device_->store().Contains(kNameProperty));
  EXPECT_FALSE(device_->store().Contains(""));
}

TEST_F(DeviceTest, GetProperties) {
  brillo::VariantDictionary props;
  Error error;
  device_->store().GetProperties(&props, &error);
  ASSERT_FALSE(props.find(kNameProperty) == props.end());
  EXPECT_TRUE(props[kNameProperty].IsTypeCompatible<string>());
  EXPECT_EQ(props[kNameProperty].Get<string>(), string(kDeviceName));
}

// Note: there are currently no writeable Device properties that
// aren't registered in a subclass.
TEST_F(DeviceTest, SetReadOnlyProperty) {
  Error error;
  // Ensure that an attempt to write a R/O property returns InvalidArgs error.
  EXPECT_FALSE(device_->mutable_store()->SetAnyProperty(
      kAddressProperty, brillo::Any(string()), &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());
}

TEST_F(DeviceTest, ClearReadOnlyProperty) {
  Error error;
  EXPECT_FALSE(device_->mutable_store()->SetAnyProperty(
      kAddressProperty, brillo::Any(string()), &error));
}

TEST_F(DeviceTest, ClearReadOnlyDerivedProperty) {
  Error error;
  EXPECT_FALSE(device_->mutable_store()->SetAnyProperty(
      kIPConfigsProperty, brillo::Any(Strings()), &error));
}

TEST_F(DeviceTest, DestroyIPConfig) {
  ASSERT_EQ(nullptr, device_->ipconfig_);
  device_->ipconfig_ = new IPConfig(control_interface(), kDeviceName);
  device_->ip6config_ = new IPConfig(control_interface(), kDeviceName);
  device_->dhcpv6_config_ = new IPConfig(control_interface(), kDeviceName);
  device_->DestroyIPConfig();
  ASSERT_EQ(nullptr, device_->ipconfig_);
  ASSERT_EQ(nullptr, device_->ip6config_);
  ASSERT_EQ(nullptr, device_->dhcpv6_config_);
}

TEST_F(DeviceTest, DestroyIPConfigNULL) {
  ASSERT_EQ(nullptr, device_->ipconfig_);
  ASSERT_EQ(nullptr, device_->ip6config_);
  ASSERT_EQ(nullptr, device_->dhcpv6_config_);
  device_->DestroyIPConfig();
  ASSERT_EQ(nullptr, device_->ipconfig_);
  ASSERT_EQ(nullptr, device_->ip6config_);
  ASSERT_EQ(nullptr, device_->dhcpv6_config_);
}

MATCHER_P(MatchesDhcpProperties, dhcp_props, "") {
  return dhcp_props == arg.properties();
}

TEST_F(DeviceTest, AcquireIPConfigWithSelectedService) {
  device_->ipconfig_ = new IPConfig(control_interface(), "randomname");
  auto dhcp_provider = std::make_unique<MockDHCPProvider>();
  device_->dhcp_provider_ = dhcp_provider.get();

  scoped_refptr<MockDHCPConfig> dhcp_config(
      new MockDHCPConfig(control_interface(), kDeviceName));

  const string service_storage_id = "service_storage_id";
  FakeStore storage;
  storage.SetString(service_storage_id, "DHCPProperty.Hostname",
                    "name of host");

  scoped_refptr<MockService> service(new NiceMock<MockService>(manager()));
  auto service_dhcp_properties =
      std::make_unique<DhcpProperties>(/*manager=*/nullptr);
  service_dhcp_properties->Load(&storage, service_storage_id);
  service->dhcp_properties_ = std::move(service_dhcp_properties);
  SelectService(service);

  const string default_profile_storage_id = "default_profile_storage_id";
  FakeStore default_profile_storage;
  default_profile_storage.SetString(default_profile_storage_id,
                                    "DHCPProperty.VendorClass", "vendorclass");

  auto manager_dhcp_properties =
      std::make_unique<DhcpProperties>(/*manager=*/nullptr);
  manager_dhcp_properties->Load(&default_profile_storage,
                                default_profile_storage_id);
  DhcpProperties combined_props = DhcpProperties::Combine(
      *manager_dhcp_properties, service->dhcp_properties());

#ifndef DISABLE_DHCPV6
  device_->dhcpv6_config_ = new IPConfig(control_interface(), "randomname");
  scoped_refptr<MockDHCPConfig> dhcpv6_config(
      new MockDHCPConfig(control_interface(), kDeviceName));

  EXPECT_CALL(*manager(), IsDHCPv6EnabledForDevice(kDeviceName))
      .WillOnce(Return(true));
  EXPECT_CALL(*dhcp_provider, CreateIPv6Config(_, _))
      .WillOnce(Return(dhcpv6_config));
  EXPECT_CALL(*dhcpv6_config, RequestIP()).WillOnce(Return(true));
#endif  // DISABLE_DHCPV6
  manager()->dhcp_properties_ = std::move(manager_dhcp_properties);
  EXPECT_CALL(*dhcp_provider,
              CreateIPv4Config(
                  _, _, _, MatchesDhcpProperties(combined_props.properties())))
      .WillOnce(Return(dhcp_config));
  EXPECT_CALL(*dhcp_config, RequestIP()).WillOnce(Return(true));
  EXPECT_TRUE(device_->AcquireIPConfig());
  ASSERT_NE(nullptr, device_->ipconfig_);
  EXPECT_EQ(kDeviceName, device_->ipconfig_->device_name());
  EXPECT_FALSE(device_->ipconfig_->update_callback_.is_null());
#ifndef DISABLE_DHCPV6
  EXPECT_EQ(kDeviceName, device_->dhcpv6_config_->device_name());
  EXPECT_FALSE(device_->dhcpv6_config_->update_callback_.is_null());
#endif  // DISABLE_DHCPV6
  device_->dhcp_provider_ = nullptr;
}

TEST_F(DeviceTest, AcquireIPConfigWithoutSelectedService) {
  device_->ipconfig_ = new IPConfig(control_interface(), "randomname");
  auto dhcp_provider = std::make_unique<MockDHCPProvider>();
  device_->dhcp_provider_ = dhcp_provider.get();
  scoped_refptr<MockDHCPConfig> dhcp_config(
      new MockDHCPConfig(control_interface(), kDeviceName));
  auto manager_dhcp_properties = std::make_unique<DhcpProperties>(manager());
  manager()->dhcp_properties_ = std::move(manager_dhcp_properties);
#ifndef DISABLE_DHCPV6
  device_->dhcpv6_config_ = new IPConfig(control_interface(), "randomname");
  scoped_refptr<MockDHCPConfig> dhcpv6_config(
      new MockDHCPConfig(control_interface(), kDeviceName));

  EXPECT_CALL(*manager(), IsDHCPv6EnabledForDevice(kDeviceName))
      .WillOnce(Return(true));
  EXPECT_CALL(*dhcp_provider, CreateIPv6Config(_, _))
      .WillOnce(Return(dhcpv6_config));
  EXPECT_CALL(*dhcpv6_config, RequestIP()).WillOnce(Return(true));
#endif  // DISABLE_DHCPV6

  EXPECT_CALL(*dhcp_provider,
              CreateIPv4Config(_, _, _,
                               MatchesDhcpProperties(
                                   manager()->dhcp_properties().properties())))
      .WillOnce(Return(dhcp_config));
  EXPECT_CALL(*dhcp_config, RequestIP()).WillOnce(Return(true));
  EXPECT_TRUE(device_->AcquireIPConfig());
  ASSERT_NE(nullptr, device_->ipconfig_);
  EXPECT_EQ(kDeviceName, device_->ipconfig_->device_name());
  EXPECT_FALSE(device_->ipconfig_->update_callback_.is_null());
#ifndef DISABLE_DHCPV6
  EXPECT_EQ(kDeviceName, device_->dhcpv6_config_->device_name());
  EXPECT_FALSE(device_->dhcpv6_config_->update_callback_.is_null());
#endif  // DISABLE_DHCPV6
  device_->dhcp_provider_ = nullptr;
}

TEST_F(DeviceTest, ConfigWithMinimumMTU) {
  const int minimum_mtu = 1500;

  EXPECT_CALL(*manager(), GetMinimumMTU()).WillOnce(Return(minimum_mtu));

  device_->ipconfig_ = new IPConfig(control_interface(), "anothername");
  auto dhcp_provider = std::make_unique<MockDHCPProvider>();
  device_->dhcp_provider_ = dhcp_provider.get();

  scoped_refptr<MockDHCPConfig> dhcp_config(
      new NiceMock<MockDHCPConfig>(control_interface(), kDeviceName));
  EXPECT_CALL(*dhcp_provider, CreateIPv4Config(_, _, _, _))
      .WillOnce(Return(dhcp_config));
  EXPECT_CALL(*dhcp_config, set_minimum_mtu(minimum_mtu));

  device_->AcquireIPConfig();
}

TEST_F(DeviceTest, StartIPv6) {
  EXPECT_CALL(*device_,
              SetIPFlag(IPAddress::kFamilyIPv6,
                        StrEq(Device::kIPFlagDisableIPv6), StrEq("0")))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *device_,
      SetIPFlag(IPAddress::kFamilyIPv6,
                StrEq(Device::kIPFlagAcceptRouterAdvertisements), StrEq("2")))
      .WillOnce(Return(true));
  device_->StartIPv6();
}

TEST_F(DeviceTest, StartIPv6Disabled) {
  Error error;
  EXPECT_CALL(*device_,
              SetIPFlag(IPAddress::kFamilyIPv6,
                        StrEq(Device::kIPFlagDisableIPv6), StrEq("1")))
      .WillOnce(Return(true));
  device_->SetIPv6Disabled(true, &error);
  Mock::VerifyAndClearExpectations(device_.get());
  EXPECT_CALL(*device_, SetIPFlag(_, _, _)).Times(0);
  device_->StartIPv6();
  Mock::VerifyAndClearExpectations(device_.get());
  device_->SetIPv6Disabled(false, &error);
}

TEST_F(DeviceTest, MultiHomed) {
  // Device should have multi-homing disabled by default.
  EXPECT_CALL(*device_, SetIPFlag(_, _, _)).Times(0);
  device_->SetIsMultiHomed(false);
  Mock::VerifyAndClearExpectations(device_.get());

  // Disabled -> enabled should change flags on the device.
  EXPECT_CALL(*device_, SetIPFlag(IPAddress::kFamilyIPv4, StrEq("arp_announce"),
                                  StrEq("2")))
      .WillOnce(Return(true));
  EXPECT_CALL(*device_, SetIPFlag(IPAddress::kFamilyIPv4, StrEq("arp_ignore"),
                                  StrEq("1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*device_,
              SetIPFlag(IPAddress::kFamilyIPv4, StrEq("rp_filter"), StrEq("2")))
      .WillOnce(Return(true));
  device_->SetIsMultiHomed(true);
  Mock::VerifyAndClearExpectations(device_.get());

  // Enabled -> enabled should be a no-op.
  EXPECT_CALL(*device_, SetIPFlag(_, _, _)).Times(0);
  device_->SetIsMultiHomed(true);

  // Disabling or enabling reverse-path filtering should also be a no-op
  // (since it is disabled due to multi-homing).
  device_->SetLooseRouting(false);
  device_->SetLooseRouting(true);
  Mock::VerifyAndClearExpectations(device_.get());

  // Enabled -> disabled should reset the flags back to the default, but
  // because non-default routing is enabled, rp_filter will be left
  // in loose mode.
  EXPECT_CALL(*device_, SetIPFlag(IPAddress::kFamilyIPv4, StrEq("arp_announce"),
                                  StrEq("0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*device_, SetIPFlag(IPAddress::kFamilyIPv4, StrEq("arp_ignore"),
                                  StrEq("0")))
      .WillOnce(Return(true));
  device_->SetIsMultiHomed(false);
  Mock::VerifyAndClearExpectations(device_.get());

  // Re-enable reverse-path filtering.
  EXPECT_CALL(*device_,
              SetIPFlag(IPAddress::kFamilyIPv4, StrEq("rp_filter"), StrEq("1")))
      .WillOnce(Return(true));
  device_->SetLooseRouting(false);
  Mock::VerifyAndClearExpectations(device_.get());
}

TEST_F(DeviceTest, Load) {
  device_->enabled_persistent_ = false;

  FakeStore storage;
  const string id = device_->GetStorageIdentifier();
  storage.SetBool(id, Device::kStoragePowered, true);
  EXPECT_TRUE(device_->Load(&storage));
  EXPECT_TRUE(device_->enabled_persistent());
}

TEST_F(DeviceTest, Save) {
  device_->enabled_persistent_ = true;

  FakeStore storage;
  EXPECT_TRUE(device_->Save(&storage));
  const string id = device_->GetStorageIdentifier();
  bool powered = false;
  EXPECT_TRUE(storage.GetBool(id, Device::kStoragePowered, &powered));
  EXPECT_TRUE(powered);
}

TEST_F(DeviceTest, SelectedService) {
  EXPECT_EQ(nullptr, device_->selected_service_);
  device_->SetServiceState(Service::kStateAssociating);
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  EXPECT_EQ(device_->selected_service_, service);

  EXPECT_CALL(*service, SetState(Service::kStateConfiguring));
  device_->SetServiceState(Service::kStateConfiguring);
  EXPECT_CALL(*service, SetFailure(Service::kFailureOutOfRange));
  device_->SetServiceFailure(Service::kFailureOutOfRange);

  // Service should be returned to "Idle" state
  EXPECT_CALL(*service, state()).WillOnce(Return(Service::kStateUnknown));
  EXPECT_CALL(*service, SetState(Service::kStateIdle));
  EXPECT_CALL(*service, SetConnection(IsNullRefPtr()));
  SelectService(nullptr);

  // A service in the "Failure" state should not be reset to "Idle"
  SelectService(service);
  EXPECT_CALL(*service, state()).WillOnce(Return(Service::kStateFailure));
  EXPECT_CALL(*service, SetConnection(IsNullRefPtr()));
  SelectService(nullptr);
}

TEST_F(DeviceTest, ResetConnection) {
  EXPECT_EQ(nullptr, device_->selected_service_);
  device_->SetServiceState(Service::kStateAssociating);
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  EXPECT_EQ(device_->selected_service_, service);

  // ResetConnection() should drop the connection and the selected service,
  // but should not change the service state.
  EXPECT_CALL(*service, SetState(_)).Times(0);
  EXPECT_CALL(*service, SetConnection(IsNullRefPtr()));
  device_->ResetConnection();
  EXPECT_EQ(nullptr, device_->selected_service_);
}

TEST_F(DeviceTest, LinkMonitorFailure) {
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  EXPECT_EQ(device_->selected_service(), service);

  time_t current_time = 1000;

  // Initial link monitor failure.
  EXPECT_CALL(time_, GetSecondsBoottime(_))
      .WillOnce(DoAll(SetArgPointee<0>(current_time), Return(true)));
  EXPECT_CALL(*metrics(), NotifyUnreliableLinkSignalStrength(_, _)).Times(0);
  device_->OnLinkMonitorFailure();
  EXPECT_FALSE(service->unreliable());

  // Another link monitor failure after 3 minutes, report signal strength.
  current_time += 180;
  EXPECT_CALL(time_, GetSecondsBoottime(_))
      .WillOnce(DoAll(SetArgPointee<0>(current_time), Return(true)));
  EXPECT_CALL(*metrics(), NotifyUnreliableLinkSignalStrength(_, _)).Times(1);
  device_->OnLinkMonitorFailure();
  EXPECT_TRUE(service->unreliable());

  // Device is connected with the reliable link callback setup, then
  // another link monitor failure after 3 minutes, which implies link is
  // still unreliable, reliable link callback should be cancelled.
  current_time += 180;
  SetReliableLinkCallback();
  EXPECT_CALL(time_, GetSecondsBoottime(_))
      .WillOnce(DoAll(SetArgPointee<0>(current_time), Return(true)));
  EXPECT_CALL(*metrics(), NotifyUnreliableLinkSignalStrength(_, _)).Times(1);
  device_->OnLinkMonitorFailure();
  EXPECT_TRUE(service->unreliable());
  EXPECT_TRUE(ReliableLinkCallbackIsCancelled());

  // Another link monitor failure after an hour, link is still reliable, signal
  // strength not reported.
  current_time += 3600;
  service->set_unreliable(false);
  EXPECT_CALL(time_, GetSecondsBoottime(_))
      .WillOnce(DoAll(SetArgPointee<0>(current_time), Return(true)));
  EXPECT_CALL(*metrics(), NotifyUnreliableLinkSignalStrength(_, _)).Times(0);
  device_->OnLinkMonitorFailure();
  EXPECT_FALSE(service->unreliable());
}

TEST_F(DeviceTest, LinkStatusResetOnSelectService) {
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  service->set_unreliable(true);
  SetReliableLinkCallback();
  EXPECT_FALSE(ReliableLinkCallbackIsCancelled());

  // Service is deselected, link status of the service should be resetted.
  EXPECT_CALL(*service, state()).WillOnce(Return(Service::kStateIdle));
  EXPECT_CALL(*service, SetState(_));
  EXPECT_CALL(*service, SetConnection(_));
  SelectService(nullptr);
  EXPECT_FALSE(service->unreliable());
  EXPECT_TRUE(ReliableLinkCallbackIsCancelled());
}

TEST_F(DeviceTest, IPConfigUpdatedFailure) {
  scoped_refptr<MockIPConfig> ipconfig =
      new MockIPConfig(control_interface(), kDeviceName);
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  EXPECT_CALL(*service, DisconnectWithFailure(Service::kFailureDHCP, _,
                                              HasSubstr("OnIPConfigFailure")));
  EXPECT_CALL(*service, SetConnection(IsNullRefPtr()));
  EXPECT_CALL(*ipconfig, ResetProperties());
  OnIPConfigFailed(ipconfig.get());
}

TEST_F(DeviceTest, IPConfigUpdatedFailureWithIPv6Config) {
  // Setup IPv6 configuration.
  SetupIPv6Config();
  EXPECT_THAT(device_->ip6config_, NotNullRefPtr());

  // IPv4 configuration failed, fallback to use IPv6 configuration.
  scoped_refptr<MockIPConfig> ipconfig =
      new NiceMock<MockIPConfig>(control_interface(), kDeviceName);
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  SetConnection(connection.get());

  EXPECT_CALL(*ipconfig, ResetProperties());
  EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(false));
  EXPECT_CALL(*connection, UpdateFromIPConfig(device_->ip6config_));
  EXPECT_CALL(*service, IsOnline()).WillOnce(Return(false));
  EXPECT_CALL(*service, SetState(Service::kStateConnected));
  EXPECT_CALL(*service, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, SetState(Service::kStateOnline));
  EXPECT_CALL(*service, SetConnection(NotNullRefPtr()));
  OnIPConfigFailed(ipconfig.get());
}

// IPv4 configuration failed with existing IPv6 connection.
TEST_F(DeviceTest, IPConfigUpdatedFailureWithIPv6Connection) {
  // Setup IPv6 configuration.
  SetupIPv6Config();
  EXPECT_THAT(device_->ip6config_, NotNullRefPtr());

  scoped_refptr<MockIPConfig> ipconfig =
      new NiceMock<MockIPConfig>(control_interface(), kDeviceName);
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  SetConnection(connection.get());

  EXPECT_CALL(*ipconfig, ResetProperties());
  EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(true));
  EXPECT_CALL(*service, DisconnectWithFailure(_, _, _)).Times(0);
  EXPECT_CALL(*service, SetConnection(IsNullRefPtr())).Times(0);
  OnIPConfigFailed(ipconfig.get());
  // Verify connection not teardown.
  EXPECT_THAT(device_->connection(), NotNullRefPtr());
}

TEST_F(DeviceTest, IPConfigUpdatedFailureWithStatic) {
  scoped_refptr<MockIPConfig> ipconfig =
      new MockIPConfig(control_interface(), kDeviceName);
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  service->static_ip_parameters_.args_.Set<string>(kAddressProperty, "1.1.1.1");
  service->static_ip_parameters_.args_.Set<int32_t>(kPrefixlenProperty, 16);
  // Even though we won't call DisconnectWithFailure, we should still have
  // the service learn from the failed DHCP attempt.
  EXPECT_CALL(*service, DisconnectWithFailure(_, _, _)).Times(0);
  EXPECT_CALL(*service, SetConnection(_)).Times(0);
  // The IPConfig should retain the previous values.
  EXPECT_CALL(*ipconfig, ResetProperties()).Times(0);
  OnIPConfigFailed(ipconfig.get());
}

TEST_F(DeviceTest, IPConfigUpdatedSuccess) {
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  scoped_refptr<MockIPConfig> ipconfig =
      new NiceMock<MockIPConfig>(control_interface(), kDeviceName);
  device_->set_ipconfig(ipconfig);
  EXPECT_CALL(*service, IsOnline()).WillOnce(Return(false));
  EXPECT_CALL(*service, SetState(Service::kStateConnected));
  EXPECT_CALL(*metrics(), NotifyNetworkConnectionIPType(
                              device_->technology(),
                              Metrics::kNetworkConnectionIPTypeIPv4));
  EXPECT_CALL(*metrics(),
              NotifyIPv6ConnectivityStatus(device_->technology(), false));
  EXPECT_CALL(*service, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, HasStaticNameServers()).WillRepeatedly(Return(false));
  EXPECT_CALL(*service, SetState(Service::kStateOnline));
  EXPECT_CALL(*service, SetConnection(NotNullRefPtr()));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));

  OnIPConfigUpdated(ipconfig.get());
}

TEST_F(DeviceTest, IPConfigUpdatedAlreadyOnline) {
  // The service is already Online and selected, so it should not transition
  // back to Connected.
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  scoped_refptr<MockIPConfig> ipconfig =
      new NiceMock<MockIPConfig>(control_interface(), kDeviceName);
  device_->set_ipconfig(ipconfig);
  EXPECT_CALL(*service, IsOnline()).WillOnce(Return(true));
  EXPECT_CALL(*service, SetState(Service::kStateConnected)).Times(0);
  EXPECT_CALL(*metrics(), NotifyNetworkConnectionIPType(
                              device_->technology(),
                              Metrics::kNetworkConnectionIPTypeIPv4));
  EXPECT_CALL(*metrics(),
              NotifyIPv6ConnectivityStatus(device_->technology(), false));
  EXPECT_CALL(*service, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, HasStaticNameServers()).WillRepeatedly(Return(false));

  // Successful portal (non-)detection forces the service Online.
  EXPECT_CALL(*service, SetState(Service::kStateOnline));
  EXPECT_CALL(*service, SetConnection(NotNullRefPtr()));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));

  OnIPConfigUpdated(ipconfig.get());
}

TEST_F(DeviceTest, IPConfigUpdatedSuccessNoSelectedService) {
  // Make sure shill doesn't crash if a service is disabled immediately
  // after receiving its IP config (selected_service_ is nullptr in this case).
  scoped_refptr<MockIPConfig> ipconfig =
      new NiceMock<MockIPConfig>(control_interface(), kDeviceName);
  SelectService(nullptr);
  OnIPConfigUpdated(ipconfig.get());
}

TEST_F(DeviceTest, OnIPConfigExpired) {
  scoped_refptr<MockIPConfig> ipconfig =
      new NiceMock<MockIPConfig>(control_interface(), kDeviceName);
  const int kLeaseLength = 1234;
  ipconfig->properties_.lease_duration_seconds = kLeaseLength;

  EXPECT_CALL(
      *metrics(),
      SendToUMA("Network.Shill.Unknown.ExpiredLeaseLengthSeconds2",
                kLeaseLength, Metrics::kMetricExpiredLeaseLengthSecondsMin,
                Metrics::kMetricExpiredLeaseLengthSecondsMax,
                Metrics::kMetricExpiredLeaseLengthSecondsNumBuckets));

  OnIPConfigExpired(ipconfig.get());
}

TEST_F(DeviceTest, SetEnabledNonPersistent) {
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->enabled_persistent_ = false;
  Error error;
  device_->SetEnabledNonPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);

  // Enable while already enabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_persistent_ = false;
  device_->enabled_pending_ = true;
  device_->enabled_ = true;
  device_->SetEnabledNonPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Enable while enabled but disabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = false;
  device_->SetEnabledNonPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Disable while already disabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_ = false;
  device_->SetEnabledNonPersistent(false, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Disable while already enabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = true;
  device_->SetEnabledNonPersistent(false, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(DeviceTest, SetEnabledPersistent) {
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->enabled_persistent_ = false;
  EXPECT_CALL(*manager(), UpdateDevice(_));
  Error error;
  device_->SetEnabledPersistent(true, &error, ResultCallback());
  EXPECT_TRUE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);

  // Enable while already enabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_persistent_ = false;
  device_->enabled_pending_ = true;
  device_->enabled_ = true;
  device_->SetEnabledPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Enable while enabled but disabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = false;
  device_->SetEnabledPersistent(true, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_TRUE(device_->enabled_);
  EXPECT_EQ(Error::kOperationFailed, error.type());

  // Disable while already disabled.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_ = false;
  device_->SetEnabledPersistent(false, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_FALSE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_TRUE(error.IsSuccess());

  // Disable while already enabling.
  error.Populate(Error::kOperationInitiated);
  device_->enabled_pending_ = true;
  device_->SetEnabledPersistent(false, &error, ResultCallback());
  EXPECT_FALSE(device_->enabled_persistent_);
  EXPECT_TRUE(device_->enabled_pending_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_EQ(Error::kOperationFailed, error.type());
}

TEST_F(DeviceTest, Start) {
  EXPECT_FALSE(device_->running_);
  EXPECT_FALSE(device_->enabled_);
  EXPECT_FALSE(device_->enabled_pending_);
  device_->SetEnabled(true);
  EXPECT_TRUE(device_->running_);
  EXPECT_TRUE(device_->enabled_pending_);
  device_->OnEnabledStateChanged(ResultCallback(),
                                 Error(Error::kOperationFailed));
  EXPECT_FALSE(device_->enabled_pending_);
}

TEST_F(DeviceTest, Stop) {
  device_->enabled_ = true;
  device_->enabled_pending_ = true;
  device_->ipconfig_ = new IPConfig(control_interface(), kDeviceName);
  scoped_refptr<MockService> service(new NiceMock<MockService>(manager()));
  SelectService(service);

  EXPECT_CALL(*service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitBoolChanged(kPoweredProperty, false));
  EXPECT_CALL(rtnl_handler_, SetInterfaceFlags(_, 0, IFF_UP));
  device_->SetEnabled(false);
  device_->OnEnabledStateChanged(ResultCallback(), Error());

  EXPECT_EQ(nullptr, device_->ipconfig_);
  EXPECT_EQ(nullptr, device_->selected_service_);
}

TEST_F(DeviceTest, StopWithFixedIpParams) {
  device_->SetFixedIpParams(true);
  device_->enabled_ = true;
  device_->enabled_pending_ = true;
  device_->ipconfig_ = new IPConfig(control_interface(), kDeviceName);
  scoped_refptr<MockService> service(new NiceMock<MockService>(manager()));
  SelectService(service);

  EXPECT_CALL(*service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitBoolChanged(kPoweredProperty, false));
  EXPECT_CALL(rtnl_handler_, SetInterfaceFlags(_, _, _)).Times(0);
  device_->SetEnabled(false);
  device_->OnEnabledStateChanged(ResultCallback(), Error());

  EXPECT_EQ(nullptr, device_->ipconfig_);
  EXPECT_EQ(nullptr, device_->selected_service_);
}

TEST_F(DeviceTest, StopWithNetworkInterfaceDisabledAfterward) {
  device_->enabled_ = true;
  device_->enabled_pending_ = true;
  device_->ipconfig_ = new IPConfig(control_interface(), kDeviceName);
  scoped_refptr<MockService> service(new NiceMock<MockService>(manager()));
  SelectService(service);

  EXPECT_CALL(*device_, ShouldBringNetworkInterfaceDownAfterDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, state())
      .WillRepeatedly(Return(Service::kStateConnected));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitBoolChanged(kPoweredProperty, false));
  device_->SetEnabled(false);
  EXPECT_CALL(rtnl_handler_, SetInterfaceFlags(_, 0, IFF_UP));
  device_->OnEnabledStateChanged(ResultCallback(), Error());

  EXPECT_EQ(nullptr, device_->ipconfig_);
  EXPECT_EQ(nullptr, device_->selected_service_);
}

TEST_F(DeviceTest, StartProhibited) {
  DeviceRefPtr device(new TestDevice(manager(), kDeviceName, kDeviceAddress,
                                     kDeviceInterfaceIndex, Technology::kWifi));
  {
    Error error;
    manager()->SetProhibitedTechnologies("wifi", &error);
    EXPECT_TRUE(error.IsSuccess());
  }

  device->SetEnabled(true);
  EXPECT_FALSE(device->running());

  {
    Error error;
    manager()->SetProhibitedTechnologies("", &error);
    EXPECT_TRUE(error.IsSuccess());
  }
  device->SetEnabled(true);
  EXPECT_TRUE(device->running());
}

TEST_F(DeviceTest, Reset) {
  Error e;
  device_->Reset(&e, ResultCallback());
  EXPECT_EQ(Error::kNotSupported, e.type());
  EXPECT_EQ("Device doesn't support Reset.", e.message());
}

TEST_F(DeviceTest, ResumeWithIPConfig) {
  scoped_refptr<MockIPConfig> ipconfig =
      new MockIPConfig(control_interface(), kDeviceName);
  device_->set_ipconfig(ipconfig);
  EXPECT_CALL(*ipconfig, RenewIP());
  device_->OnAfterResume();
}

TEST_F(DeviceTest, ResumeWithoutIPConfig) {
  // Just test that we don't crash in this case.
  ASSERT_EQ(nullptr, device_->ipconfig());
  device_->OnAfterResume();
}

TEST_F(DeviceTest, ResumeWithLinkMonitor) {
  MockLinkMonitor* link_monitor = new StrictMock<MockLinkMonitor>();
  SetLinkMonitor(link_monitor);  // Passes ownership.
  EXPECT_CALL(*link_monitor, OnAfterResume());
  device_->OnAfterResume();
}

TEST_F(DeviceTest, ResumeWithoutLinkMonitor) {
  // Just test that we don't crash in this case.
  EXPECT_FALSE(HasLinkMonitor());
  device_->OnAfterResume();
}

TEST_F(DeviceTest, ResumeWithUnreliableLink) {
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  service->set_unreliable(true);
  SetReliableLinkCallback();

  // Link status should be resetted upon resume.
  device_->OnAfterResume();
  EXPECT_FALSE(service->unreliable());
  EXPECT_TRUE(ReliableLinkCallbackIsCancelled());
}

TEST_F(DeviceTest, OnConnected) {
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);

  // Link is reliable, no need to post delayed task to reset link status.
  device_->OnConnected();
  EXPECT_TRUE(ReliableLinkCallbackIsCancelled());

  // Link is unreliable when connected, delayed task is posted to reset the
  // link state.
  service->set_unreliable(true);
  device_->OnConnected();
  EXPECT_FALSE(ReliableLinkCallbackIsCancelled());
}

TEST_F(DeviceTest, LinkMonitor) {
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  SetConnection(connection.get());
  MockLinkMonitor* link_monitor = new StrictMock<MockLinkMonitor>();
  SetLinkMonitor(link_monitor);  // Passes ownership.
  EXPECT_CALL(*link_monitor, Start()).Times(0);
  EXPECT_CALL(*manager(),
              IsTechnologyLinkMonitorEnabled(Technology(Technology::kUnknown)))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(StartLinkMonitor());

  EXPECT_CALL(*link_monitor, Start()).Times(0);
  EXPECT_CALL(*connection, IsIPv6())
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(StartLinkMonitor());

  EXPECT_CALL(*link_monitor, Start()).Times(0);
  EXPECT_CALL(*service, link_monitor_disabled())
      .WillOnce(Return(true))
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(StartLinkMonitor());

  EXPECT_CALL(*link_monitor, Start())
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_FALSE(StartLinkMonitor());
  EXPECT_TRUE(StartLinkMonitor());

  unsigned int kResponseTime = 123;
  EXPECT_CALL(*link_monitor, GetResponseTimeMilliseconds())
      .WillOnce(Return(kResponseTime));
  {
    Error error;
    EXPECT_EQ(kResponseTime, GetLinkMonitorResponseTime(&error));
    EXPECT_TRUE(error.IsSuccess());
  }
  StopLinkMonitor();
  {
    Error error;
    EXPECT_EQ(0, GetLinkMonitorResponseTime(&error));
    EXPECT_FALSE(error.IsSuccess());
  }
}

TEST_F(DeviceTest, LinkMonitorCancelledOnSelectService) {
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  SetConnection(connection.get());
  MockLinkMonitor* link_monitor = new StrictMock<MockLinkMonitor>();
  SetLinkMonitor(link_monitor);  // Passes ownership.
  EXPECT_CALL(*service, state()).WillOnce(Return(Service::kStateIdle));
  EXPECT_CALL(*service, SetState(_));
  EXPECT_CALL(*service, SetConnection(_));
  EXPECT_TRUE(HasLinkMonitor());
  SelectService(nullptr);
  EXPECT_FALSE(HasLinkMonitor());
}

TEST_F(DeviceTest, TrafficMonitor) {
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  SetConnection(connection.get());
  MockTrafficMonitor* traffic_monitor =
      SetTrafficMonitor(std::make_unique<StrictMock<MockTrafficMonitor>>());

  EXPECT_CALL(*device_, IsTrafficMonitorEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(*traffic_monitor, Start());
  StartTrafficMonitor();
  EXPECT_CALL(*traffic_monitor, Stop());
  StopTrafficMonitor();
  Mock::VerifyAndClearExpectations(traffic_monitor);

  EXPECT_CALL(*metrics(), NotifyNetworkProblemDetected(
                              _, Metrics::kNetworkProblemDNSFailure))
      .Times(1);
  NetworkProblemDetected(TrafficMonitor::kNetworkProblemDNSFailure);

  // Verify traffic monitor not running when it is disabled.
  traffic_monitor =
      SetTrafficMonitor(std::make_unique<StrictMock<MockTrafficMonitor>>());
  EXPECT_CALL(*device_, IsTrafficMonitorEnabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*traffic_monitor, Start()).Times(0);
  StartTrafficMonitor();
  EXPECT_CALL(*traffic_monitor, Stop()).Times(0);
  StopTrafficMonitor();
}

TEST_F(DeviceTest, TrafficMonitorCancelledOnSelectService) {
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  SetConnection(connection.get());
  MockTrafficMonitor* traffic_monitor =
      SetTrafficMonitor(std::make_unique<StrictMock<MockTrafficMonitor>>());
  EXPECT_CALL(*device_, IsTrafficMonitorEnabled()).WillRepeatedly(Return(true));
  EXPECT_CALL(*service, state()).WillOnce(Return(Service::kStateIdle));
  EXPECT_CALL(*service, SetState(_));
  EXPECT_CALL(*service, SetConnection(_));
  EXPECT_CALL(*traffic_monitor, Stop());
  SelectService(nullptr);
}

TEST_F(DeviceTest, ShouldUseArpGateway) {
  EXPECT_FALSE(device_->ShouldUseArpGateway());
}

TEST_F(DeviceTest, IsConnectedViaTether) {
  EXPECT_FALSE(device_->IsConnectedViaTether());

  // An empty ipconfig doesn't mean we're tethered.
  device_->ipconfig_ = new IPConfig(control_interface(), kDeviceName);
  EXPECT_FALSE(device_->IsConnectedViaTether());

  // Add an ipconfig property that indicates this is an Android tether.
  IPConfig::Properties properties;
  properties.vendor_encapsulated_options =
      ByteArray(Tethering::kAndroidVendorEncapsulatedOptions,
                Tethering::kAndroidVendorEncapsulatedOptions +
                    strlen(Tethering::kAndroidVendorEncapsulatedOptions));
  device_->ipconfig_->UpdateProperties(properties, true);
  EXPECT_TRUE(device_->IsConnectedViaTether());

  const char kTestVendorEncapsulatedOptions[] = "Some other non-empty value";
  properties.vendor_encapsulated_options = ByteArray(
      kTestVendorEncapsulatedOptions,
      kTestVendorEncapsulatedOptions + sizeof(kTestVendorEncapsulatedOptions));
  device_->ipconfig_->UpdateProperties(properties, true);
  EXPECT_FALSE(device_->IsConnectedViaTether());
}

TEST_F(DeviceTest, AvailableIPConfigs) {
  EXPECT_EQ(vector<RpcIdentifier>(), device_->AvailableIPConfigs(nullptr));
  device_->ipconfig_ = new IPConfig(control_interface(), kDeviceName);
  EXPECT_EQ(vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId},
            device_->AvailableIPConfigs(nullptr));
  device_->ip6config_ = new IPConfig(control_interface(), kDeviceName);

  // We don't really care that the RPC IDs for all IPConfig mock adaptors
  // are the same, or their ordering.  We just need to see that there are two
  // of them when both IPv6 and IPv4 IPConfigs are available.
  EXPECT_EQ(2, device_->AvailableIPConfigs(nullptr).size());

  device_->dhcpv6_config_ = new IPConfig(control_interface(), kDeviceName);
  EXPECT_EQ(3, device_->AvailableIPConfigs(nullptr).size());

  device_->dhcpv6_config_ = nullptr;
  EXPECT_EQ(2, device_->AvailableIPConfigs(nullptr).size());

  device_->ipconfig_ = nullptr;
  EXPECT_EQ(vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId},
            device_->AvailableIPConfigs(nullptr));

  device_->ip6config_ = nullptr;
  EXPECT_EQ(vector<RpcIdentifier>(), device_->AvailableIPConfigs(nullptr));
}

TEST_F(DeviceTest, OnIPv6AddressChanged) {
  EXPECT_CALL(*manager(), FilterPrependDNSServersByFamily(_))
      .WillRepeatedly(Return(vector<string>()));

  // An IPv6 clear while ip6config_ is nullptr will not emit a change.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _))
      .Times(0);
  device_->OnIPv6AddressChanged(nullptr);
  EXPECT_THAT(device_->ip6config_, IsNullRefPtr());
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());

  IPAddress address0(IPAddress::kFamilyIPv6);
  const char kAddress0[] = "fe80::1aa9:5ff:abcd:1234";
  ASSERT_TRUE(address0.SetAddressFromString(kAddress0));

  // Add an IPv6 address while ip6config_ is nullptr.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnIPv6AddressChanged(&address0);
  EXPECT_THAT(device_->ip6config_, NotNullRefPtr());
  EXPECT_EQ(kAddress0, device_->ip6config_->properties().address);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());

  // If the IPv6 address does not change, no signal is emitted.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _))
      .Times(0);
  device_->OnIPv6AddressChanged(&address0);
  EXPECT_EQ(kAddress0, device_->ip6config_->properties().address);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());

  IPAddress address1(IPAddress::kFamilyIPv6);
  const char kAddress1[] = "fe80::1aa9:5ff:abcd:5678";
  ASSERT_TRUE(address1.SetAddressFromString(kAddress1));

  // If the IPv6 address changes, a signal is emitted.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnIPv6AddressChanged(&address1);
  EXPECT_EQ(kAddress1, device_->ip6config_->properties().address);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());

  // If the IPv6 prefix changes, a signal is emitted.
  address1.set_prefix(64);
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnIPv6AddressChanged(&address1);
  EXPECT_EQ(kAddress1, device_->ip6config_->properties().address);

  // Return the IPv6 address to nullptr.
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty,
                                            vector<RpcIdentifier>()));
  device_->OnIPv6AddressChanged(nullptr);
  EXPECT_THAT(device_->ip6config_, IsNullRefPtr());
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
}

TEST_F(DeviceTest, OnIPv6DnsServerAddressesChanged) {
  EXPECT_CALL(*manager(), FilterPrependDNSServersByFamily(_))
      .WillRepeatedly(Return(vector<string>()));

  // With existing IPv4 connection, so no attempt to setup IPv6 connection.
  // IPv6 connection is being tested in OnIPv6ConfigurationCompleted test.
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  SetConnection(connection.get());
  EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(false));

  // IPv6 DNS server addresses are not provided will not emit a change.
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _))
      .Times(0);
  device_->OnIPv6DnsServerAddressesChanged();
  EXPECT_THAT(device_->ip6config_, IsNullRefPtr());
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  const char kAddress1[] = "fe80::1aa9:5ff:abcd:1234";
  const char kAddress2[] = "fe80::1aa9:5ff:abcd:1235";
  const uint32_t kInfiniteLifetime = 0xffffffff;
  IPAddress ipv6_address1(IPAddress::kFamilyIPv6);
  IPAddress ipv6_address2(IPAddress::kFamilyIPv6);
  ASSERT_TRUE(ipv6_address1.SetAddressFromString(kAddress1));
  ASSERT_TRUE(ipv6_address2.SetAddressFromString(kAddress2));
  vector<IPAddress> dns_server_addresses = {ipv6_address1, ipv6_address2};
  vector<string> dns_server_addresses_str = {kAddress1, kAddress2};

  // Add IPv6 DNS server addresses while ip6config_ is nullptr.
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(dns_server_addresses),
                      SetArgPointee<2>(kInfiniteLifetime), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnIPv6DnsServerAddressesChanged();
  EXPECT_THAT(device_->ip6config_, NotNullRefPtr());
  EXPECT_EQ(dns_server_addresses_str,
            device_->ip6config_->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // Add an IPv6 address while IPv6 DNS server addresses already existed.
  IPAddress address3(IPAddress::kFamilyIPv6);
  const char kAddress3[] = "fe80::1aa9:5ff:abcd:1236";
  ASSERT_TRUE(address3.SetAddressFromString(kAddress3));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnIPv6AddressChanged(&address3);
  EXPECT_THAT(device_->ip6config_, NotNullRefPtr());
  EXPECT_EQ(kAddress3, device_->ip6config_->properties().address);
  EXPECT_EQ(dns_server_addresses_str,
            device_->ip6config_->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // If the IPv6 DNS server addresses does not change, no signal is emitted.
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(dns_server_addresses),
                      SetArgPointee<2>(kInfiniteLifetime), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(kIPConfigsProperty, _))
      .Times(0);
  device_->OnIPv6DnsServerAddressesChanged();
  EXPECT_EQ(dns_server_addresses_str,
            device_->ip6config_->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // Setting lifetime to 0 should expire and clear out the DNS server.
  const uint32_t kExpiredLifetime = 0;
  vector<string> empty_dns_server;
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(dns_server_addresses),
                      SetArgPointee<2>(kExpiredLifetime), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnIPv6DnsServerAddressesChanged();
  EXPECT_EQ(empty_dns_server, device_->ip6config_->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // Set DNS server with lifetime of 1 hour.
  const uint32_t kLifetimeOneHr = 3600;
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(dns_server_addresses),
                      SetArgPointee<2>(kLifetimeOneHr), Return(true)));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnIPv6DnsServerAddressesChanged();
  EXPECT_EQ(dns_server_addresses_str,
            device_->ip6config_->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);

  // Return the DNS server addresses to nullptr.
  EXPECT_CALL(device_info_,
              GetIPv6DnsServerAddresses(kDeviceInterfaceIndex, _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnIPv6DnsServerAddressesChanged();
  EXPECT_EQ(empty_dns_server, device_->ip6config_->properties().dns_servers);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);
}

TEST_F(DeviceTest, OnIPv6ConfigurationCompleted) {
  EXPECT_CALL(*manager(), FilterPrependDNSServersByFamily(_))
      .WillRepeatedly(Return(vector<string>()));
  scoped_refptr<MockService> service(new StrictMock<MockService>(manager()));
  SelectService(service);
  scoped_refptr<MockConnection> connection(
      new StrictMock<MockConnection>(&device_info_));
  SetConnection(connection.get());

  // Setup initial IPv6 configuration.
  SetupIPv6Config();
  EXPECT_THAT(device_->ip6config_, NotNullRefPtr());

  // IPv6 configuration update with non-IPv6 connection, no connection update.
  EXPECT_THAT(device_->connection(), NotNullRefPtr());
  IPAddress address1(IPAddress::kFamilyIPv6);
  const char kAddress1[] = "fe80::1aa9:5ff:abcd:1231";
  ASSERT_TRUE(address1.SetAddressFromString(kAddress1));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(false));
  EXPECT_CALL(*service, SetConnection(_)).Times(0);
  device_->OnIPv6AddressChanged(&address1);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);
  Mock::VerifyAndClearExpectations(service.get());
  Mock::VerifyAndClearExpectations(connection.get());

  // IPv6 configuration update with IPv6 connection, connection update.
  IPAddress address2(IPAddress::kFamilyIPv6);
  const char kAddress2[] = "fe80::1aa9:5ff:abcd:1232";
  ASSERT_TRUE(address2.SetAddressFromString(kAddress2));
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(true));
  EXPECT_CALL(*connection, UpdateFromIPConfig(device_->ip6config_));
  EXPECT_CALL(*metrics(), NotifyNetworkConnectionIPType(
                              device_->technology(),
                              Metrics::kNetworkConnectionIPTypeIPv6));
  EXPECT_CALL(*metrics(),
              NotifyIPv6ConnectivityStatus(device_->technology(), true));
  EXPECT_CALL(*service, IsOnline()).WillOnce(Return(false));
  EXPECT_CALL(*service, SetState(Service::kStateConnected));
  EXPECT_CALL(*service, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service, SetState(Service::kStateOnline));
  EXPECT_CALL(*service, SetConnection(NotNullRefPtr()));
  EXPECT_CALL(*manager(), IsTechnologyLinkMonitorEnabled(_))
      .WillRepeatedly(Return(false));
  device_->OnIPv6AddressChanged(&address2);
  Mock::VerifyAndClearExpectations(GetDeviceMockAdaptor());
  Mock::VerifyAndClearExpectations(&device_info_);
  Mock::VerifyAndClearExpectations(service.get());
  Mock::VerifyAndClearExpectations(connection.get());
}

TEST_F(DeviceTest, OnDHCPv6ConfigUpdated) {
  device_->dhcpv6_config_ = new IPConfig(control_interface(), kDeviceName);
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnDHCPv6ConfigUpdated(device_->dhcpv6_config_.get(), true);
}

TEST_F(DeviceTest, OnDHCPv6ConfigFailed) {
  device_->dhcpv6_config_ = new IPConfig(control_interface(), kDeviceName);
  IPConfig::Properties properties;
  properties.dhcpv6_addresses = {{{kDhcpv6AddressProperty, "2001:db8:0:1::1"}}};
  properties.dhcpv6_delegated_prefixes = {
      {{kDhcpv6AddressProperty, "2001:db8:0:100::"}}};
  properties.lease_duration_seconds = 1;
  device_->dhcpv6_config_->set_properties(properties);
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnDHCPv6ConfigFailed(device_->dhcpv6_config_.get());
  EXPECT_TRUE(device_->dhcpv6_config_->properties().dhcpv6_addresses.empty());
  EXPECT_TRUE(
      device_->dhcpv6_config_->properties().dhcpv6_delegated_prefixes.empty());
  EXPECT_EQ(0, device_->dhcpv6_config_->properties().lease_duration_seconds);
}

TEST_F(DeviceTest, OnDHCPv6ConfigExpired) {
  device_->dhcpv6_config_ = new IPConfig(control_interface(), kDeviceName);
  IPConfig::Properties properties;
  properties.dhcpv6_addresses = {{{kDhcpv6AddressProperty, "2001:db8:0:1::1"}}};
  properties.dhcpv6_delegated_prefixes = {
      {{kDhcpv6AddressProperty, "2001:db8:0:100::"}}};
  properties.lease_duration_seconds = 1;
  device_->dhcpv6_config_->set_properties(properties);
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitRpcIdentifierArrayChanged(
                  kIPConfigsProperty,
                  vector<RpcIdentifier>{IPConfigMockAdaptor::kRpcId}));
  device_->OnDHCPv6ConfigExpired(device_->dhcpv6_config_.get());
  EXPECT_TRUE(device_->dhcpv6_config_->properties().dhcpv6_addresses.empty());
  EXPECT_TRUE(
      device_->dhcpv6_config_->properties().dhcpv6_delegated_prefixes.empty());
  EXPECT_EQ(0, device_->dhcpv6_config_->properties().lease_duration_seconds);
}

TEST_F(DeviceTest, PrependIPv4DNSServers) {
  const struct {
    vector<string> ipconfig_servers;
    vector<string> prepend_servers;
    vector<string> expected_servers;
  } expectations[] = {
      {{}, {"8.8.8.8"}, {"8.8.8.8"}},
      {{"8.8.8.8"}, {}, {"8.8.8.8"}},
      {{"8.8.8.8"}, {"10.10.10.10"}, {"10.10.10.10", "8.8.8.8"}},
      {{"8.8.8.8", "10.10.10.10"}, {"10.10.10.10"}, {"10.10.10.10", "8.8.8.8"}},
      {{"8.8.8.8", "10.10.10.10"}, {"8.8.8.8"}, {"8.8.8.8", "10.10.10.10"}},
      {{"8.8.8.8", "9.9.9.9", "10.10.10.10"},
       {"9.9.9.9"},
       {"9.9.9.9", "8.8.8.8", "10.10.10.10"}},
  };

  for (const auto& expectation : expectations) {
    scoped_refptr<IPConfig> ipconfig =
        new IPConfig(control_interface(), kDeviceName);

    EXPECT_CALL(*manager(),
                FilterPrependDNSServersByFamily(IPAddress::kFamilyIPv4))
        .WillOnce(Return(expectation.prepend_servers));
    IPConfig::Properties properties;
    properties.dns_servers = expectation.ipconfig_servers;
    properties.address_family = IPAddress::kFamilyIPv4;
    ipconfig->set_properties(properties);

    device_->set_ipconfig(ipconfig);
    OnIPConfigUpdated(ipconfig.get());
    EXPECT_EQ(expectation.expected_servers,
              device_->ipconfig()->properties().dns_servers);
  }
}

TEST_F(DeviceTest, PrependIPv6DNSServers) {
  vector<IPAddress> dns_server_addresses = {IPAddress("2001:4860:4860::8888"),
                                            IPAddress("2001:4860:4860::8844")};

  const uint32_t kAddressLifetime = 1000;
  EXPECT_CALL(device_info_, GetIPv6DnsServerAddresses(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(dns_server_addresses),
                            SetArgPointee<2>(kAddressLifetime), Return(true)));
  const vector<string> kOutputServers{"2001:4860:4860::8899"};
  EXPECT_CALL(*manager(),
              FilterPrependDNSServersByFamily(IPAddress::kFamilyIPv6))
      .WillOnce(Return(kOutputServers));
  device_->OnIPv6DnsServerAddressesChanged();

  const vector<string> kExpectedServers{
      "2001:4860:4860::8899", "2001:4860:4860::8888", "2001:4860:4860::8844"};
  EXPECT_EQ(kExpectedServers, device_->ip6config()->properties().dns_servers);
}

TEST_F(DeviceTest, PrependWithStaticConfiguration) {
  scoped_refptr<IPConfig> ipconfig =
      new IPConfig(control_interface(), kDeviceName);

  device_->set_ipconfig(ipconfig);

  scoped_refptr<MockService> service(new NiceMock<MockService>(manager()));
  EXPECT_CALL(*service, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(true));
  SelectService(service);

  auto parameters = service->mutable_static_ip_parameters();
  parameters->args_.Set<string>(kAddressProperty, "1.1.1.1");
  parameters->args_.Set<int32_t>(kPrefixlenProperty, 16);

  scoped_refptr<MockConnection> connection =
      new NiceMock<MockConnection>(&device_info_);
  SetConnection(connection);

  // Ensure that in the absence of statically configured nameservers that the
  // prepend DNS servers are still prepended.
  EXPECT_CALL(*service, HasStaticNameServers()).WillOnce(Return(false));
  const vector<string> kOutputServers{"8.8.8.8"};
  EXPECT_CALL(*manager(),
              FilterPrependDNSServersByFamily(IPAddress::kFamilyIPv4))
      .WillRepeatedly(Return(kOutputServers));
  OnIPConfigUpdated(ipconfig.get());
  EXPECT_EQ(kOutputServers, device_->ipconfig()->properties().dns_servers);

  // Ensure that when nameservers are statically configured that the prepend DNS
  // servers are not used.
  const vector<string> static_servers = {"4.4.4.4", "5.5.5.5"};
  parameters->args_.Set<Strings>(kNameServersProperty, static_servers);
  EXPECT_CALL(*service, HasStaticNameServers()).WillOnce(Return(true));
  OnIPConfigUpdated(ipconfig.get());
  EXPECT_EQ(static_servers, device_->ipconfig()->properties().dns_servers);
}

TEST_F(DeviceTest, ResolvePeerMacAddress) {
  IPAddress device_address(IPAddress::kFamilyIPv4);
  ASSERT_TRUE(device_address.SetAddressAndPrefixFromString("192.168.5.2/24"));
  EXPECT_CALL(device_info_, GetAddresses(device_->interface_index()))
      .WillRepeatedly(Return(std::vector<IPAddress>{device_address}));

  const char kResolvedMac[] = "00:11:22:33:44:55";
  const ByteString kMacBytes(
      Device::MakeHardwareAddressFromString(kResolvedMac));
  EXPECT_CALL(device_info_,
              GetMacAddressOfPeer(device_->interface_index(), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(kMacBytes), Return(true)));

  // Invalid peer address (not a valid IP address nor MAC address).
  Error error;
  string result;
  const char kInvalidPeer[] = "peer";
  EXPECT_FALSE(device_->ResolvePeerMacAddress(kInvalidPeer, &result, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());

  // No direct connectivity to the peer.
  error.Reset();
  EXPECT_FALSE(device_->ResolvePeerMacAddress("192.168.1.1", &result, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());

  // Provided IP address is in the ARP cache, return the resolved MAC address.
  error.Reset();
  EXPECT_TRUE(device_->ResolvePeerMacAddress("192.168.5.1", &result, &error));
  EXPECT_EQ(kResolvedMac, result);
}

TEST_F(DeviceTest, SetHostnameWithEmptyHostname) {
  EXPECT_CALL(*manager(), ShouldAcceptHostnameFrom(_)).Times(0);
  EXPECT_CALL(device_info_, SetHostname(_)).Times(0);
  EXPECT_FALSE(SetHostname(""));
}

TEST_F(DeviceTest, SetHostnameForDisallowedDevice) {
  EXPECT_CALL(*manager(), ShouldAcceptHostnameFrom(kDeviceName))
      .WillOnce(Return(false));
  EXPECT_CALL(device_info_, SetHostname(_)).Times(0);
  EXPECT_FALSE(SetHostname("wilson"));
}

TEST_F(DeviceTest, SetHostnameWithFailingDeviceInfo) {
  EXPECT_CALL(*manager(), ShouldAcceptHostnameFrom(kDeviceName))
      .WillOnce(Return(true));
  EXPECT_CALL(device_info_, SetHostname("wilson")).WillOnce(Return(false));
  EXPECT_FALSE(SetHostname("wilson"));
}

TEST_F(DeviceTest, SetHostnameMaximumHostnameLength) {
  EXPECT_CALL(*manager(), ShouldAcceptHostnameFrom(kDeviceName))
      .WillOnce(Return(true));
  EXPECT_CALL(
      device_info_,
      SetHostname(
          "wilson.was-a-good-ball.and-was.an-excellent-swimmer.in-high-seas"))
      .WillOnce(Return(true));
  EXPECT_TRUE(SetHostname(
      "wilson.was-a-good-ball.and-was.an-excellent-swimmer.in-high-seas"));
}

TEST_F(DeviceTest, SetHostnameTruncateDomainName) {
  EXPECT_CALL(*manager(), ShouldAcceptHostnameFrom(kDeviceName))
      .WillOnce(Return(true));
  EXPECT_CALL(device_info_, SetHostname("wilson")).WillOnce(Return(false));
  EXPECT_FALSE(SetHostname(
      "wilson.was-a-great-ball.and-was.an-excellent-swimmer.in-high-seas"));
}

TEST_F(DeviceTest, SetHostnameTruncateHostname) {
  EXPECT_CALL(*manager(), ShouldAcceptHostnameFrom(kDeviceName))
      .WillOnce(Return(true));
  EXPECT_CALL(
      device_info_,
      SetHostname(
          "wilson-was-a-great-ball-and-was-an-excellent-swimmer-in-high-sea"))
      .WillOnce(Return(true));
  EXPECT_TRUE(SetHostname(
      "wilson-was-a-great-ball-and-was-an-excellent-swimmer-in-high-sea-chop"));
}

TEST_F(DeviceTest, SetMacAddress) {
  constexpr char mac_address[] = "abcdefabcdef";
  EXPECT_CALL(*GetDeviceMockAdaptor(),
              EmitStringChanged(kAddressProperty, mac_address));
  EXPECT_NE(mac_address, device_->mac_address());
  device_->device_set_mac_address(mac_address);
  EXPECT_EQ(mac_address, device_->mac_address());
}

TEST_F(DeviceTest, FetchTrafficCounters) {
  auto source0 = patchpanel::TrafficCounter::CHROME;
  auto source1 = patchpanel::TrafficCounter::USER;
  std::valarray<uint64_t> counter_arr0{2842, 1243, 240598, 43095};
  std::valarray<uint64_t> counter_arr1{4554666, 43543, 5999, 500000};
  patchpanel::TrafficCounter counter0 =
      CreateCounter(counter_arr0, source0, kDeviceName);
  patchpanel::TrafficCounter counter1 =
      CreateCounter(counter_arr1, source1, kDeviceName);
  vector<patchpanel::TrafficCounter> counters{counter0, counter1};
  patchpanel_client_->set_stored_traffic_counters(counters);

  EXPECT_EQ(nullptr, device_->selected_service_);
  scoped_refptr<MockService> service0(new NiceMock<MockService>(manager()));
  EXPECT_TRUE(service0->traffic_counter_snapshot_.empty());
  EXPECT_TRUE(service0->current_traffic_counters_.empty());
  SelectService(service0);
  EXPECT_EQ(service0, device_->selected_service_);
  EXPECT_TRUE(service0->current_traffic_counters_.empty());
  EXPECT_EQ(2, service0->traffic_counter_snapshot_.size());
  for (size_t i = 0; i < Service::kTrafficCounterArraySize; i++) {
    EXPECT_EQ(counter_arr0[i], service0->traffic_counter_snapshot_[source0][i]);
    EXPECT_EQ(counter_arr1[i], service0->traffic_counter_snapshot_[source1][i]);
  }

  std::valarray<uint64_t> counter_diff0{12, 98, 34, 76};
  std::valarray<uint64_t> counter_diff1{324534, 23434, 785676, 256};
  std::valarray<uint64_t> new_total0 = counter_arr0 + counter_diff0;
  std::valarray<uint64_t> new_total1 = counter_arr1 + counter_diff1;
  counter0 = CreateCounter(new_total0, source0, kDeviceName);
  counter1 = CreateCounter(new_total1, source1, kDeviceName);
  counters = {counter0, counter1};
  patchpanel_client_->set_stored_traffic_counters(counters);

  scoped_refptr<MockService> service1(new NiceMock<MockService>(manager()));
  SelectService(service1);
  EXPECT_EQ(service1, device_->selected_service_);
  for (size_t i = 0; i < Service::kTrafficCounterArraySize; i++) {
    EXPECT_EQ(counter_diff0[i],
              service0->current_traffic_counters_[source0][i]);
    EXPECT_EQ(counter_diff1[i],
              service0->current_traffic_counters_[source1][i]);

    EXPECT_EQ(new_total0[i], service1->traffic_counter_snapshot_[source0][i]);
    EXPECT_EQ(new_total1[i], service1->traffic_counter_snapshot_[source1][i]);
  }
  EXPECT_TRUE(service1->current_traffic_counters_.empty());
}

class DevicePortalDetectionTest : public DeviceTest {
 public:
  DevicePortalDetectionTest()
      : connection_(new StrictMock<MockConnection>(&device_info_)),
        service_(new StrictMock<MockService>(manager())),
        portal_detector_(new StrictMock<MockPortalDetector>(connection_)) {}
  ~DevicePortalDetectionTest() override = default;
  void SetUp() override {
    DeviceTest::SetUp();
    SelectService(service_);
    SetConnection(connection_.get());
    device_->portal_detector_.reset(portal_detector_);  // Passes ownership.
  }

 protected:
  static const int kPortalAttempts;

  bool StartPortalDetection() { return device_->StartPortalDetection(); }
  void StopPortalDetection() { device_->StopPortalDetection(); }

  void PortalDetectorCallback(const PortalDetector::Result& http_result,
                              const PortalDetector::Result& https_result) {
    device_->PortalDetectorCallback(http_result, https_result);
  }
  bool RequestPortalDetection() { return device_->RequestPortalDetection(); }
  void SetServiceConnectedState(Service::ConnectState state) {
    device_->SetServiceConnectedState(state);
  }
  void ExpectPortalDetectorReset() {
    EXPECT_EQ(nullptr, device_->portal_detector_);
  }
  void ExpectPortalDetectorSet() {
    EXPECT_NE(nullptr, device_->portal_detector_);
  }
  void ExpectPortalDetectorIsMock() {
    EXPECT_EQ(portal_detector_, device_->portal_detector_.get());
  }
  void InvokeFallbackDNSResultCallback(DnsServerTester::Status status) {
    device_->FallbackDNSResultCallback(status);
  }
  void InvokeConfigDNSResultCallback(DnsServerTester::Status status) {
    device_->ConfigDNSResultCallback(status);
  }
  void DestroyConnection() { device_->DestroyConnection(); }
  scoped_refptr<MockConnection> connection_;
  scoped_refptr<MockService> service_;

  // Used only for EXPECT_CALL().  Object is owned by device.
  MockPortalDetector* portal_detector_;
};

const int DevicePortalDetectionTest::kPortalAttempts = 2;

TEST_F(DevicePortalDetectionTest, ServicePortalDetectionDisabled) {
  EXPECT_CALL(*service_, IsPortalDetectionDisabled()).WillOnce(Return(true));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  EXPECT_FALSE(StartPortalDetection());
}

TEST_F(DevicePortalDetectionTest, TechnologyPortalDetectionDisabled) {
  EXPECT_CALL(*service_, IsPortalDetectionDisabled()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, IsPortalDetectionAuto()).WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(false));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  EXPECT_FALSE(StartPortalDetection());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionProxyConfig) {
  EXPECT_CALL(*service_, IsPortalDetectionDisabled()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, HasProxyConfig()).WillOnce(Return(true));
  EXPECT_CALL(*service_, IsPortalDetectionAuto()).WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(true));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  EXPECT_FALSE(StartPortalDetection());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionBadUrl) {
  EXPECT_CALL(*service_, IsPortalDetectionDisabled()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, HasProxyConfig()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsPortalDetectionAuto()).WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(true));
  const string http_portal_url, https_portal_url;
  const vector<string> fallback_urls;
  EXPECT_CALL(manager_, GetPortalCheckHttpUrl())
      .WillRepeatedly(ReturnRef(http_portal_url));
  EXPECT_CALL(manager_, GetPortalCheckHttpsUrl())
      .WillRepeatedly(ReturnRef(https_portal_url));
  EXPECT_CALL(manager_, GetPortalCheckFallbackHttpUrls())
      .WillRepeatedly(ReturnRef(fallback_urls));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  EXPECT_FALSE(StartPortalDetection());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionStart) {
  EXPECT_CALL(*service_, IsPortalDetectionDisabled()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, HasProxyConfig()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsPortalDetectionAuto()).WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(true));
  const string http_portal_url(PortalDetector::kDefaultHttpUrl);
  const string https_portal_url(PortalDetector::kDefaultHttpsUrl);
  const vector<string> fallback_urls(PortalDetector::kDefaultFallbackHttpUrls);
  EXPECT_CALL(manager_, GetPortalCheckHttpUrl())
      .WillRepeatedly(ReturnRef(http_portal_url));
  EXPECT_CALL(manager_, GetPortalCheckHttpsUrl())
      .WillRepeatedly(ReturnRef(https_portal_url));
  EXPECT_CALL(manager_, GetPortalCheckFallbackHttpUrls())
      .WillRepeatedly(ReturnRef(fallback_urls));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline)).Times(0);
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  EXPECT_CALL(*connection_, IsIPv6()).WillRepeatedly(Return(false));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection_, dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));
  EXPECT_TRUE(StartPortalDetection());

  // Drop all references to device_info before it falls out of scope.
  SetConnection(nullptr);
  StopPortalDetection();
}

TEST_F(DevicePortalDetectionTest, PortalDetectionStartIPv6) {
  EXPECT_CALL(*service_, IsPortalDetectionDisabled()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, HasProxyConfig()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsPortalDetectionAuto()).WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(true));
  const string http_portal_url(PortalDetector::kDefaultHttpUrl);
  const string https_portal_url(PortalDetector::kDefaultHttpsUrl);
  const vector<string> fallback_urls(PortalDetector::kDefaultFallbackHttpUrls);
  EXPECT_CALL(manager_, GetPortalCheckHttpUrl())
      .WillRepeatedly(ReturnRef(http_portal_url));
  EXPECT_CALL(manager_, GetPortalCheckHttpsUrl())
      .WillRepeatedly(ReturnRef(https_portal_url));
  EXPECT_CALL(manager_, GetPortalCheckFallbackHttpUrls())
      .WillRepeatedly(ReturnRef(fallback_urls));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline)).Times(0);
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  EXPECT_CALL(*connection_, IsIPv6()).WillRepeatedly(Return(true));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection_, dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));
  EXPECT_TRUE(StartPortalDetection());

  // Drop all references to device_info before it falls out of scope.
  SetConnection(nullptr);
  StopPortalDetection();
}

MATCHER_P(IsPortalDetectorResult, result, "") {
  return (result.num_attempts == arg.num_attempts &&
          result.phase == arg.phase && result.status == arg.status);
}

TEST_F(DevicePortalDetectionTest, PortalDetectionFailure) {
  const int kFailureStatusCode = 204;
  PortalDetector::Result http_result(PortalDetector::Phase::kConnection,
                                     PortalDetector::Status::kFailure,
                                     kPortalAttempts);
  http_result.status_code = kFailureStatusCode;
  PortalDetector::Result https_result(PortalDetector::Phase::kContent,
                                      PortalDetector::Status::kSuccess);
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseConnection,
                                        kPortalDetectionStatusFailure,
                                        kFailureStatusCode));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  EXPECT_CALL(*metrics(), SendEnumToUMA("Network.Shill.Unknown.PortalResult",
                                        Metrics::kPortalResultConnectionFailure,
                                        Metrics::kPortalResultMax));
  EXPECT_CALL(
      *metrics(),
      SendToUMA("Network.Shill.Unknown.PortalAttemptsToOnline", _, _, _, _))
      .Times(0);
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, IsIPv6()).WillOnce(Return(false));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection(
                            IsPortalDetectorResult(http_result),
                            IsPortalDetectorResult(https_result)));
  PortalDetectorCallback(http_result, https_result);
}

TEST_F(DevicePortalDetectionTest, PortalDetectionSuccess) {
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_, SetPortalDetectionFailure(_, _, _)).Times(0);
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  EXPECT_CALL(*metrics(), SendEnumToUMA("Network.Shill.Unknown.PortalResult",
                                        Metrics::kPortalResultSuccess,
                                        Metrics::kPortalResultMax));
  EXPECT_CALL(
      *metrics(),
      SendToUMA("Network.Shill.Unknown.PortalAttemptsToOnline", kPortalAttempts,
                Metrics::kMetricPortalAttemptsToOnlineMin,
                Metrics::kMetricPortalAttemptsToOnlineMax,
                Metrics::kMetricPortalAttemptsToOnlineNumBuckets));
  EXPECT_CALL(*metrics(),
              SendToUMA("Network.Shill.Unknown.PortalAttempts", _, _, _, _))
      .Times(0);
  PortalDetectorCallback(
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kSuccess, kPortalAttempts),
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kSuccess));
}

TEST_F(DevicePortalDetectionTest, PortalDetectionSuccessAfterFailure) {
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseConnection,
                                        kPortalDetectionStatusFailure, _));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  EXPECT_CALL(*metrics(), SendEnumToUMA("Network.Shill.Unknown.PortalResult",
                                        Metrics::kPortalResultConnectionFailure,
                                        Metrics::kPortalResultMax));
  EXPECT_CALL(
      *metrics(),
      SendToUMA("Network.Shill.Unknown.PortalAttemptsToOnline", _, _, _, _))
      .Times(0);
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, IsIPv6()).WillOnce(Return(false));
  PortalDetectorCallback(
      PortalDetector::Result(PortalDetector::Phase::kConnection,
                             PortalDetector::Status::kFailure, kPortalAttempts),
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kFailure));
  Mock::VerifyAndClearExpectations(metrics());
  EXPECT_CALL(*service_, SetPortalDetectionFailure(_, _, _)).Times(0);
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  EXPECT_CALL(*metrics(), SendEnumToUMA("Network.Shill.Unknown.PortalResult",
                                        Metrics::kPortalResultSuccess,
                                        Metrics::kPortalResultMax));
  EXPECT_CALL(
      *metrics(),
      SendToUMA("Network.Shill.Unknown.PortalAttemptsToOnline",
                kPortalAttempts * 2, Metrics::kMetricPortalAttemptsToOnlineMin,
                Metrics::kMetricPortalAttemptsToOnlineMax,
                Metrics::kMetricPortalAttemptsToOnlineNumBuckets));
  PortalDetectorCallback(
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kSuccess,
                             kPortalAttempts * 2),
      PortalDetector::Result(PortalDetector::Phase::kContent,
                             PortalDetector::Status::kSuccess));
}

TEST_F(DevicePortalDetectionTest, RequestPortalDetection) {
  EXPECT_CALL(*service_, state())
      .WillOnce(Return(Service::kStateOnline))
      .WillRepeatedly(Return(Service::kStateNoConnectivity));
  EXPECT_FALSE(RequestPortalDetection());

  EXPECT_CALL(*connection_, IsDefault())
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(RequestPortalDetection());

  EXPECT_CALL(*portal_detector_, IsInProgress()).WillOnce(Return(true));
  // Portal detection already running.
  EXPECT_TRUE(RequestPortalDetection());

  // Make sure our running mock portal detector was not replaced.
  ExpectPortalDetectorIsMock();

  // Throw away our pre-fabricated portal detector, and have the device create
  // a new one.
  StopPortalDetection();
  EXPECT_CALL(*service_, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_, IsPortalDetectionAuto()).WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, HasProxyConfig()).WillRepeatedly(Return(false));
  const string kPortalCheckHttpUrl("http://portal");
  const string kPortalCheckHttpsUrl("https://portal");
  const vector<string> kPortalCheckFallbackHttpUrls(
      {"http://fallback", "http://other"});
  EXPECT_CALL(manager_, GetPortalCheckHttpUrl())
      .WillOnce(ReturnRef(kPortalCheckHttpUrl));
  EXPECT_CALL(manager_, GetPortalCheckHttpsUrl())
      .WillOnce(ReturnRef(kPortalCheckHttpsUrl));
  EXPECT_CALL(manager_, GetPortalCheckFallbackHttpUrls())
      .WillRepeatedly(ReturnRef(kPortalCheckFallbackHttpUrls));
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, IsIPv6()).WillRepeatedly(Return(false));
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection_, dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));
  EXPECT_TRUE(RequestPortalDetection());
}

TEST_F(DevicePortalDetectionTest, RequestStartConnectivityTest) {
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  EXPECT_CALL(*connection_, IsIPv6()).WillRepeatedly(Return(false));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection_, dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));

  EXPECT_EQ(nullptr, device_->connection_tester_);
  EXPECT_TRUE(device_->StartConnectivityTest());
  EXPECT_NE(nullptr, device_->connection_tester_);
}

TEST_F(DevicePortalDetectionTest, NotConnected) {
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(false));
  SetServiceConnectedState(Service::kStateNoConnectivity);
  // We don't check for the portal detector to be reset here, because
  // it would have been reset as a part of disconnection.
}

TEST_F(DevicePortalDetectionTest, NotPortal) {
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_, SetState(Service::kStateOnline));
  SetServiceConnectedState(Service::kStateOnline);
  ExpectPortalDetectorReset();
}

TEST_F(DevicePortalDetectionTest, NotDefault) {
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  SetServiceConnectedState(Service::kStateNoConnectivity);
  ExpectPortalDetectorReset();
}

TEST_F(DevicePortalDetectionTest, PortalIntervalIsZero) {
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(true));
  device_->portal_check_interval_seconds_ = 0;
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  SetServiceConnectedState(Service::kStateNoConnectivity);
  ExpectPortalDetectorReset();
}

TEST_F(DevicePortalDetectionTest, RestartPortalDetection) {
  int portal_check_interval = 3;
  device_->portal_check_interval_seconds_ = portal_check_interval;
  const string kPortalCheckHttpUrl("http://portal");
  const string kPortalCheckHttpsUrl("https://portal");
  const vector<string> kPortalCheckFallbackHttpUrls(
      {"http://fallback", "http://other"});
  PortalDetector::Properties props = PortalDetector::Properties(
      kPortalCheckHttpUrl, kPortalCheckHttpsUrl, kPortalCheckFallbackHttpUrls);
  for (int i = 0; i < 10; i++) {
    EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
    EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(true));
    EXPECT_CALL(manager_, GetPortalCheckHttpUrl())
        .WillOnce(ReturnRef(kPortalCheckHttpUrl));
    EXPECT_CALL(manager_, GetPortalCheckHttpsUrl())
        .WillOnce(ReturnRef(kPortalCheckHttpsUrl));
    EXPECT_CALL(manager_, GetPortalCheckFallbackHttpUrls())
        .WillRepeatedly(ReturnRef(kPortalCheckFallbackHttpUrls));
    EXPECT_CALL(*portal_detector_, AdjustStartDelay(portal_check_interval))
        .WillOnce(Return(portal_check_interval));
    EXPECT_CALL(*portal_detector_,
                StartAfterDelay(props, portal_check_interval))
        .WillOnce(Return(true));
    EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
    SetServiceConnectedState(Service::kStateNoConnectivity);
    portal_check_interval =
        std::min(portal_check_interval * 2,
                 PortalDetector::kMaxPortalCheckIntervalSeconds);
  }
  ExpectPortalDetectorSet();
}

TEST_F(DevicePortalDetectionTest, CancelledOnSelectService) {
  ExpectPortalDetectorSet();
  EXPECT_CALL(*service_, state()).WillOnce(Return(Service::kStateIdle));
  EXPECT_CALL(*service_, SetState(_));
  EXPECT_CALL(*service_, SetConnection(_));
  SelectService(nullptr);
  ExpectPortalDetectorReset();
}

TEST_F(DevicePortalDetectionTest, PortalDetectionDNSFailure) {
  static const char* const kGoogleDNSServers[] = {"8.8.8.8", "8.8.4.4"};
  vector<string> fallback_dns_servers(kGoogleDNSServers, kGoogleDNSServers + 2);
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));

  // DNS Failure, start DNS test for fallback DNS servers.
  const int kFailureStatusCode = 204;
  PortalDetector::Result result_dns_failure(PortalDetector::Phase::kDNS,
                                            PortalDetector::Status::kFailure,
                                            kPortalAttempts);
  result_dns_failure.status_code = kFailureStatusCode;
  PortalDetector::Result https_result(PortalDetector::Phase::kContent,
                                      PortalDetector::Status::kFailure);
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseDns,
                                        kPortalDetectionStatusFailure,
                                        kFailureStatusCode));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, IsIPv6()).WillOnce(Return(false));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection(
                            IsPortalDetectorResult(result_dns_failure),
                            IsPortalDetectorResult(https_result)));
  EXPECT_CALL(*device_, StartDNSTest(fallback_dns_servers, false, _)).Times(1);
  PortalDetectorCallback(result_dns_failure, https_result);
  Mock::VerifyAndClearExpectations(device_.get());

  // DNS Timeout, start DNS test for fallback DNS servers.
  PortalDetector::Result result_dns_timeout(PortalDetector::Phase::kDNS,
                                            PortalDetector::Status::kTimeout,
                                            kPortalAttempts);
  result_dns_timeout.status_code = 0;
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseDns,
                                        kPortalDetectionStatusTimeout, 0));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, IsIPv6()).WillOnce(Return(false));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection(
                            IsPortalDetectorResult(result_dns_timeout),
                            IsPortalDetectorResult(https_result)));
  EXPECT_CALL(*device_, StartDNSTest(fallback_dns_servers, false, _)).Times(1);
  PortalDetectorCallback(result_dns_timeout, https_result);
  Mock::VerifyAndClearExpectations(device_.get());

  // Other Failure, DNS server tester not started.
  PortalDetector::Result result_connection_failure(
      PortalDetector::Phase::kConnection, PortalDetector::Status::kFailure,
      kPortalAttempts);
  result_connection_failure.status_code = kFailureStatusCode;
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseConnection,
                                        kPortalDetectionStatusFailure,
                                        kFailureStatusCode));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, IsIPv6()).WillOnce(Return(false));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection(
                            IsPortalDetectorResult(result_connection_failure),
                            IsPortalDetectorResult(https_result)));
  EXPECT_CALL(*device_, StartDNSTest(_, _, _)).Times(0);
  PortalDetectorCallback(result_connection_failure, https_result);
  Mock::VerifyAndClearExpectations(device_.get());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionRedirect) {
  static const char* const kGoogleDNSServers[] = {"8.8.8.8", "8.8.4.4"};
  vector<string> fallback_dns_servers(kGoogleDNSServers, kGoogleDNSServers + 2);
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));

  const int kRedirectStatusCode = 302;
  PortalDetector::Result result_redirect(PortalDetector::Phase::kContent,
                                         PortalDetector::Status::kRedirect);
  result_redirect.status_code = kRedirectStatusCode;
  PortalDetector::Result https_result(PortalDetector::Phase::kContent,
                                      PortalDetector::Status::kSuccess);
  result_redirect.redirect_url_string = PortalDetector::kDefaultHttpUrl;
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseContent,
                                        kPortalDetectionStatusRedirect,
                                        kRedirectStatusCode));
  EXPECT_CALL(*service_, SetState(Service::kStateRedirectFound));
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, IsIPv6()).WillOnce(Return(false));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection(
                            IsPortalDetectorResult(result_redirect),
                            IsPortalDetectorResult(https_result)));
  PortalDetectorCallback(result_redirect, https_result);
  Mock::VerifyAndClearExpectations(device_.get());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionRedirectNoUrl) {
  static const char* const kGoogleDNSServers[] = {"8.8.8.8", "8.8.4.4"};
  vector<string> fallback_dns_servers(kGoogleDNSServers, kGoogleDNSServers + 2);
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));

  const int kRedirectStatusCode = 302;
  PortalDetector::Result result_redirect(PortalDetector::Phase::kContent,
                                         PortalDetector::Status::kRedirect);
  result_redirect.status_code = kRedirectStatusCode;
  PortalDetector::Result https_result(PortalDetector::Phase::kContent,
                                      PortalDetector::Status::kSuccess);
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseContent,
                                        kPortalDetectionStatusRedirect,
                                        kRedirectStatusCode));
  EXPECT_CALL(*service_, SetState(Service::kStatePortalSuspected));
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, IsIPv6()).WillOnce(Return(false));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection(
                            IsPortalDetectorResult(result_redirect),
                            IsPortalDetectorResult(https_result)));
  PortalDetectorCallback(result_redirect, https_result);
  Mock::VerifyAndClearExpectations(device_.get());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionPortalSuspected) {
  static const char* const kGoogleDNSServers[] = {"8.8.8.8", "8.8.4.4"};
  vector<string> fallback_dns_servers(kGoogleDNSServers, kGoogleDNSServers + 2);
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));

  const int kFailureStatusCode = 300;
  PortalDetector::Result http_result(PortalDetector::Phase::kContent,
                                     PortalDetector::Status::kSuccess);
  PortalDetector::Result https_result(PortalDetector::Phase::kContent,
                                      PortalDetector::Status::kFailure);
  http_result.status_code = kFailureStatusCode;
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseContent,
                                        kPortalDetectionStatusSuccess,
                                        kFailureStatusCode));
  EXPECT_CALL(*service_, SetState(Service::kStatePortalSuspected));
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, IsIPv6()).WillOnce(Return(false));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection(
                            IsPortalDetectorResult(http_result),
                            IsPortalDetectorResult(https_result)));
  PortalDetectorCallback(http_result, https_result);
  Mock::VerifyAndClearExpectations(device_.get());
}

TEST_F(DevicePortalDetectionTest, PortalDetectionNoConnectivity) {
  static const char* const kGoogleDNSServers[] = {"8.8.8.8", "8.8.4.4"};
  vector<string> fallback_dns_servers(kGoogleDNSServers, kGoogleDNSServers + 2);
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));

  const int kFailureStatusCode = 204;
  PortalDetector::Result http_result(PortalDetector::Phase::kUnknown,
                                     PortalDetector::Status::kFailure);
  http_result.status_code = kFailureStatusCode;
  PortalDetector::Result https_result(PortalDetector::Phase::kContent,
                                      PortalDetector::Status::kFailure);
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillOnce(Return(true));
  EXPECT_CALL(*service_,
              SetPortalDetectionFailure(kPortalDetectionPhaseUnknown,
                                        kPortalDetectionStatusFailure,
                                        kFailureStatusCode));
  EXPECT_CALL(*service_, SetState(Service::kStateNoConnectivity));
  EXPECT_CALL(*connection_, IsDefault()).WillOnce(Return(false));
  EXPECT_CALL(*connection_, IsIPv6()).WillOnce(Return(false));
  EXPECT_CALL(*device_, StartConnectionDiagnosticsAfterPortalDetection(
                            IsPortalDetectorResult(http_result),
                            IsPortalDetectorResult(https_result)));
  PortalDetectorCallback(http_result, https_result);
  Mock::VerifyAndClearExpectations(device_.get());
}

TEST_F(DevicePortalDetectionTest, FallbackDNSResultCallback) {
  scoped_refptr<MockIPConfig> ipconfig =
      new MockIPConfig(control_interface(), kDeviceName);
  device_->set_ipconfig(ipconfig);

  // Fallback DNS test failed.
  EXPECT_CALL(*connection_, UpdateDNSServers(_)).Times(0);
  EXPECT_CALL(*ipconfig, UpdateDNSServers(_)).Times(0);
  EXPECT_CALL(*device_, StartDNSTest(_, _, _)).Times(0);
  EXPECT_CALL(*metrics(), NotifyFallbackDNSTestResult(
                              _, Metrics::kFallbackDNSTestResultFailure))
      .Times(1);
  InvokeFallbackDNSResultCallback(DnsServerTester::kStatusFailure);
  Mock::VerifyAndClearExpectations(connection_.get());
  Mock::VerifyAndClearExpectations(ipconfig.get());
  Mock::VerifyAndClearExpectations(metrics());

  // Fallback DNS test succeed with auto fallback disabled.
  EXPECT_CALL(*service_, is_dns_auto_fallback_allowed())
      .WillOnce(Return(false));
  EXPECT_CALL(*connection_, UpdateDNSServers(_)).Times(0);
  EXPECT_CALL(*ipconfig, UpdateDNSServers(_)).Times(0);
  EXPECT_CALL(*service_, NotifyIPConfigChanges()).Times(0);
  EXPECT_CALL(*device_, StartDNSTest(_, _, _)).Times(0);
  EXPECT_CALL(*metrics(), NotifyFallbackDNSTestResult(
                              _, Metrics::kFallbackDNSTestResultSuccess))
      .Times(1);
  InvokeFallbackDNSResultCallback(DnsServerTester::kStatusSuccess);
  Mock::VerifyAndClearExpectations(service_.get());
  Mock::VerifyAndClearExpectations(connection_.get());
  Mock::VerifyAndClearExpectations(ipconfig.get());
  Mock::VerifyAndClearExpectations(metrics());

  // Fallback DNS test succeed with auto fallback enabled.
  EXPECT_CALL(*service_, is_dns_auto_fallback_allowed()).WillOnce(Return(true));
  EXPECT_CALL(*service_, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_, IsPortalDetectionAuto()).WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, HasProxyConfig()).WillRepeatedly(Return(false));
  const string kPortalCheckHttpUrl("http://portal");
  const string kPortalCheckHttpsUrl("https://portal");
  const vector<string> kPortalCheckFallbackHttpUrls(
      {"http://fallback", "http://other"});
  EXPECT_CALL(manager_, GetPortalCheckHttpUrl())
      .WillOnce(ReturnRef(kPortalCheckHttpUrl));
  EXPECT_CALL(manager_, GetPortalCheckHttpsUrl())
      .WillOnce(ReturnRef(kPortalCheckHttpsUrl));
  EXPECT_CALL(manager_, GetPortalCheckFallbackHttpUrls())
      .WillRepeatedly(ReturnRef(kPortalCheckFallbackHttpUrls));
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, IsIPv6()).WillRepeatedly(Return(false));
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection_, dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));

  EXPECT_CALL(*ipconfig, UpdateDNSServers(_)).Times(1);
  EXPECT_CALL(*connection_, UpdateDNSServers(_)).Times(1);
  EXPECT_CALL(*service_, NotifyIPConfigChanges()).Times(1);
  EXPECT_CALL(*device_, StartDNSTest(_, true, _)).Times(1);
  EXPECT_CALL(*metrics(), NotifyFallbackDNSTestResult(
                              _, Metrics::kFallbackDNSTestResultSuccess))
      .Times(1);
  InvokeFallbackDNSResultCallback(DnsServerTester::kStatusSuccess);
  Mock::VerifyAndClearExpectations(service_.get());
  Mock::VerifyAndClearExpectations(connection_.get());
  Mock::VerifyAndClearExpectations(ipconfig.get());
  Mock::VerifyAndClearExpectations(metrics());
}

TEST_F(DevicePortalDetectionTest, ConfigDNSResultCallback) {
  scoped_refptr<MockIPConfig> ipconfig =
      new MockIPConfig(control_interface(), kDeviceName);
  device_->set_ipconfig(ipconfig);

  // DNS test failed for configured DNS servers.
  EXPECT_CALL(*connection_, UpdateDNSServers(_)).Times(0);
  EXPECT_CALL(*ipconfig, UpdateDNSServers(_)).Times(0);
  InvokeConfigDNSResultCallback(DnsServerTester::kStatusFailure);
  Mock::VerifyAndClearExpectations(connection_.get());
  Mock::VerifyAndClearExpectations(ipconfig.get());

  // DNS test succeed for configured DNS servers.
  EXPECT_CALL(*service_, IsPortalDetectionDisabled())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*service_, IsPortalDetectionAuto()).WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, HasProxyConfig()).WillRepeatedly(Return(false));
  const string kPortalCheckHttpUrl("http://portal");
  const string kPortalCheckHttpsUrl("https://portal");
  const vector<string> kPortalCheckFallbackHttpUrls(
      {"http://fallback", "http://other"});
  EXPECT_CALL(manager_, GetPortalCheckHttpUrl())
      .WillOnce(ReturnRef(kPortalCheckHttpUrl));
  EXPECT_CALL(manager_, GetPortalCheckHttpsUrl())
      .WillOnce(ReturnRef(kPortalCheckHttpsUrl));
  EXPECT_CALL(manager_, GetPortalCheckFallbackHttpUrls())
      .WillRepeatedly(ReturnRef(kPortalCheckFallbackHttpUrls));
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection_, IsIPv6()).WillRepeatedly(Return(false));
  EXPECT_CALL(*connection_, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection_, dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));
  EXPECT_CALL(*connection_, UpdateDNSServers(_)).Times(1);
  EXPECT_CALL(*ipconfig, UpdateDNSServers(_)).Times(1);
  EXPECT_CALL(*service_, NotifyIPConfigChanges()).Times(1);
  InvokeConfigDNSResultCallback(DnsServerTester::kStatusSuccess);
  Mock::VerifyAndClearExpectations(service_.get());
  Mock::VerifyAndClearExpectations(connection_.get());
  Mock::VerifyAndClearExpectations(ipconfig.get());
}

TEST_F(DevicePortalDetectionTest, DestroyConnection) {
  scoped_refptr<MockConnection> connection =
      new NiceMock<MockConnection>(&device_info_);
  // This test holds a single reference to the mock connection.
  EXPECT_TRUE(connection->HasOneRef());

  SetConnection(connection);

  EXPECT_CALL(*service_, IsPortalDetectionDisabled()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsConnected(nullptr)).WillRepeatedly(Return(true));
  EXPECT_CALL(*service_, HasProxyConfig()).WillOnce(Return(false));
  EXPECT_CALL(*service_, IsPortalDetectionAuto()).WillOnce(Return(true));
  EXPECT_CALL(manager_, IsPortalDetectionEnabled(device_->technology()))
      .WillOnce(Return(true));
  const string http_portal_url(PortalDetector::kDefaultHttpUrl);
  const string https_portal_url(PortalDetector::kDefaultHttpsUrl);
  const vector<string> fallback_urls(PortalDetector::kDefaultFallbackHttpUrls);
  EXPECT_CALL(manager_, GetPortalCheckHttpUrl())
      .WillRepeatedly(ReturnRef(http_portal_url));
  EXPECT_CALL(manager_, GetPortalCheckHttpsUrl())
      .WillRepeatedly(ReturnRef(https_portal_url));
  EXPECT_CALL(manager_, GetPortalCheckFallbackHttpUrls())
      .WillRepeatedly(ReturnRef(fallback_urls));
  const string kInterfaceName("int0");
  EXPECT_CALL(*connection, interface_name())
      .WillRepeatedly(ReturnRef(kInterfaceName));
  EXPECT_CALL(*connection, IsIPv6()).WillRepeatedly(Return(false));
  const vector<string> kDNSServers;
  EXPECT_CALL(*connection, dns_servers())
      .WillRepeatedly(ReturnRef(kDNSServers));

  EXPECT_TRUE(device_->StartConnectivityTest());
  EXPECT_TRUE(StartPortalDetection());

  // Ensure that the DestroyConnection method removes all connection references
  // except the one left in this scope.
  EXPECT_CALL(*service_, SetConnection(IsNullRefPtr()));
  DestroyConnection();
  EXPECT_TRUE(connection->HasOneRef());
}

class DeviceByteCountTest : public DeviceTest {
 public:
  DeviceByteCountTest() : rx_byte_count_(0), tx_byte_count_(0) {}
  ~DeviceByteCountTest() override = default;

  void SetUp() override {
    DeviceTest::SetUp();
    EXPECT_CALL(*manager(), device_info())
        .WillRepeatedly(Return(&device_info_));
    EXPECT_CALL(device_info_, GetByteCounts(kDeviceInterfaceIndex, _, _))
        .WillRepeatedly(Invoke(this, &DeviceByteCountTest::ReturnByteCounts));
  }

  void SetStoredByteCounts(uint64_t rx, uint64_t tx) {
    const string id = device_->GetStorageIdentifier();
    storage_.SetUint64(id, Device::kStorageReceiveByteCount, rx);
    storage_.SetUint64(id, Device::kStorageTransmitByteCount, tx);
  }

  bool ReturnByteCounts(int interface_index, uint64_t* rx, uint64_t* tx) {
    *rx = rx_byte_count_;
    *tx = tx_byte_count_;
    return true;
  }

  bool ExpectByteCounts(DeviceRefPtr device,
                        int64_t expected_rx,
                        int64_t expected_tx) {
    int64_t actual_rx = device->GetReceiveByteCount();
    int64_t actual_tx = device->GetTransmitByteCount();
    return expected_rx == actual_rx && expected_tx == actual_tx;
  }

  bool ExpectSavedCounts(DeviceRefPtr device,
                         int64_t expected_rx,
                         int64_t expected_tx) {
    const string id = device_->GetStorageIdentifier();
    uint64_t rx, tx;
    EXPECT_TRUE(storage_.GetUint64(id, Device::kStorageReceiveByteCount, &rx));
    EXPECT_TRUE(storage_.GetUint64(id, Device::kStorageTransmitByteCount, &tx));
    return static_cast<uint64_t>(expected_rx) == rx &&
           static_cast<uint64_t>(expected_tx) == tx;
  }

 protected:
  FakeStore storage_;
  uint64_t rx_byte_count_;
  uint64_t tx_byte_count_;
};

TEST_F(DeviceByteCountTest, GetByteCounts) {
  // On Device initialization, byte counts should be zero, independent of
  // the byte counts reported by the interface.
  rx_byte_count_ = 123;
  tx_byte_count_ = 456;
  DeviceRefPtr device(new TestDevice(manager(), kDeviceName, kDeviceAddress,
                                     kDeviceInterfaceIndex,
                                     Technology::kUnknown));
  EXPECT_TRUE(ExpectByteCounts(device, 0, 0));

  // Device should report any increase in the byte counts reported in the
  // interface.
  const int64_t delta_rx_count = 789;
  const int64_t delta_tx_count = 12;
  rx_byte_count_ += delta_rx_count;
  tx_byte_count_ += delta_tx_count;
  EXPECT_TRUE(ExpectByteCounts(device, delta_rx_count, delta_tx_count));

  // Expect the correct values to be saved to the profile.
  EXPECT_TRUE(device->Save(&storage_));
  EXPECT_TRUE(ExpectSavedCounts(device, delta_rx_count, delta_tx_count));

  // If Device is loaded from a profile that does not contain stored byte
  // counts, the byte counts reported should remain unchanged.
  EXPECT_TRUE(device->Load(&storage_));
  EXPECT_TRUE(ExpectByteCounts(device, delta_rx_count, delta_tx_count));

  // If Device is loaded from a profile that contains stored byte
  // counts, the byte counts reported should now reflect the stored values.
  uint64_t rx_stored_byte_count = 345;
  uint64_t tx_stored_byte_count = 678;
  SetStoredByteCounts(rx_stored_byte_count, tx_stored_byte_count);
  EXPECT_TRUE(device->Load(&storage_));
  EXPECT_TRUE(
      ExpectByteCounts(device, rx_stored_byte_count, tx_stored_byte_count));

  // Increases to the interface receive count should be reflected as offsets
  // to the stored byte counts.
  rx_byte_count_ += delta_rx_count;
  tx_byte_count_ += delta_tx_count;
  EXPECT_TRUE(ExpectByteCounts(device, rx_stored_byte_count + delta_rx_count,
                               tx_stored_byte_count + delta_tx_count));

  // Expect the correct values to be saved to the profile.
  EXPECT_TRUE(device->Save(&storage_));
  EXPECT_TRUE(ExpectSavedCounts(device, rx_stored_byte_count + delta_rx_count,
                                tx_stored_byte_count + delta_tx_count));

  // Expect that after resetting byte counts, read-back values return to zero,
  // and that the device requests this information to be persisted.
  EXPECT_CALL(*manager(), UpdateDevice(device));
  device->ResetByteCounters();
  EXPECT_TRUE(ExpectByteCounts(device, 0, 0));
}

}  // namespace shill
