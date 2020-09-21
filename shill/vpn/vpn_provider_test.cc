// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_provider.h"

#include <memory>
#include <set>
#include <utility>

#include <base/stl_util.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "shill/error.h"
#include "shill/fake_store.h"
#include "shill/ipconfig.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/routing_policy_entry.h"
#include "shill/vpn/mock_vpn_driver.h"
#include "shill/vpn/mock_vpn_service.h"

using std::string;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StartsWith;

namespace shill {

class VPNProviderTest : public testing::Test {
 public:
  VPNProviderTest()
      : manager_(&control_, nullptr, &metrics_),
        device_info_(&manager_),
        provider_(&manager_) {
    manager_.set_mock_device_info(&device_info_);
  }

  ~VPNProviderTest() override = default;

 protected:
  static const char kHost[];
  static const char kName[];

  string GetServiceFriendlyName(const ServiceRefPtr& service) {
    return service->friendly_name();
  }

  void SetConnectState(const ServiceRefPtr& service,
                       Service::ConnectState state) {
    service->state_ = state;
  }

  void AddService(const VPNServiceRefPtr& service) {
    provider_.services_.push_back(service);
  }

  VPNServiceRefPtr GetServiceAt(int idx) { return provider_.services_[idx]; }

  size_t GetServiceCount() const { return provider_.services_.size(); }

  MockControl control_;
  MockMetrics metrics_;
  MockManager manager_;
  MockDeviceInfo device_info_;
  VPNProvider provider_;
};

const char VPNProviderTest::kHost[] = "10.8.0.1";
const char VPNProviderTest::kName[] = "vpn-name";

TEST_F(VPNProviderTest, GetServiceNoType) {
  KeyValueStore args;
  Error e;
  args.Set<string>(kTypeProperty, kTypeVPN);
  ServiceRefPtr service = provider_.GetService(args, &e);
  EXPECT_EQ(Error::kNotSupported, e.type());
  EXPECT_FALSE(service);
}

TEST_F(VPNProviderTest, GetServiceUnsupportedType) {
  KeyValueStore args;
  Error e;
  args.Set<string>(kTypeProperty, kTypeVPN);
  args.Set<string>(kProviderTypeProperty, "unknown-vpn-type");
  args.Set<string>(kProviderHostProperty, kHost);
  args.Set<string>(kNameProperty, kName);
  ServiceRefPtr service = provider_.GetService(args, &e);
  EXPECT_EQ(Error::kNotSupported, e.type());
  EXPECT_FALSE(service);
}

TEST_F(VPNProviderTest, GetService) {
  KeyValueStore args;
  args.Set<string>(kTypeProperty, kTypeVPN);
  args.Set<string>(kProviderTypeProperty, kProviderOpenVpn);
  args.Set<string>(kProviderHostProperty, kHost);
  args.Set<string>(kNameProperty, kName);

  {
    Error error;
    ServiceRefPtr service = provider_.FindSimilarService(args, &error);
    EXPECT_EQ(Error::kNotFound, error.type());
    EXPECT_EQ(nullptr, service);
  }

  EXPECT_EQ(0, GetServiceCount());

  ServiceRefPtr service;
  {
    Error error;
    EXPECT_CALL(manager_, RegisterService(_));
    service = provider_.GetService(args, &error);
    EXPECT_TRUE(error.IsSuccess());
    ASSERT_NE(nullptr, service);
    testing::Mock::VerifyAndClearExpectations(&manager_);
  }

  EXPECT_EQ("vpn_10_8_0_1_vpn_name", service->GetStorageIdentifier());
  EXPECT_EQ(kName, GetServiceFriendlyName(service));

  EXPECT_EQ(1, GetServiceCount());

  // Configure the service to set its properties (including Provider.Host).
  {
    Error error;
    service->Configure(args, &error);
    EXPECT_TRUE(error.IsSuccess());
  }

  // None of the calls below should cause a new service to be registered.
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);

  // A second call should return the same service.
  {
    Error error;
    ServiceRefPtr get_service = provider_.GetService(args, &error);
    EXPECT_TRUE(error.IsSuccess());
    ASSERT_EQ(service, get_service);
  }

  EXPECT_EQ(1, GetServiceCount());

  // FindSimilarService should also return this service.
  {
    Error error;
    ServiceRefPtr similar_service = provider_.FindSimilarService(args, &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_EQ(service, similar_service);
  }

  EXPECT_EQ(1, GetServiceCount());

  // However, CreateTemporaryService should create a different service.
  {
    Error error;
    ServiceRefPtr temporary_service =
        provider_.CreateTemporaryService(args, &error);
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_NE(service, temporary_service);

    // However this service will not be part of the provider.
    EXPECT_EQ(1, GetServiceCount());
  }
}

TEST_F(VPNProviderTest, OnDeviceInfoAvailable) {
  const string kInterfaceName("tun0");
  const int kInterfaceIndex = 1;

  auto bad_driver = std::make_unique<MockVPNDriver>();
  EXPECT_CALL(*bad_driver, ClaimInterface(_, _))
      .Times(2)
      .WillRepeatedly(Return(false));
  provider_.services_.push_back(
      new VPNService(&manager_, std::move(bad_driver)));

  EXPECT_FALSE(provider_.OnDeviceInfoAvailable(kInterfaceName, kInterfaceIndex,
                                               Technology::kTunnel));

  auto good_driver = std::make_unique<MockVPNDriver>();
  EXPECT_CALL(*good_driver, ClaimInterface(_, _)).WillOnce(Return(true));
  provider_.services_.push_back(
      new VPNService(&manager_, std::move(good_driver)));

  auto dup_driver = std::make_unique<MockVPNDriver>();
  EXPECT_CALL(*dup_driver, ClaimInterface(_, _)).Times(0);
  provider_.services_.push_back(
      new VPNService(&manager_, std::move(dup_driver)));

  EXPECT_TRUE(provider_.OnDeviceInfoAvailable(kInterfaceName, kInterfaceIndex,
                                              Technology::kTunnel));
  provider_.services_.clear();
}

TEST_F(VPNProviderTest, ArcDeviceFound) {
  const string kInterfaceName("arcbr0");
  const int kInterfaceIndex = 1;

  EXPECT_EQ(provider_.allowed_iifs_.size(), 0);
  EXPECT_TRUE(provider_.OnDeviceInfoAvailable(kInterfaceName, kInterfaceIndex,
                                              Technology::kArcBridge));
  EXPECT_EQ(provider_.allowed_iifs_.size(), 1);
  EXPECT_EQ(provider_.allowed_iifs_[0], kInterfaceName);
}

TEST_F(VPNProviderTest, RemoveService) {
  scoped_refptr<MockVPNService> service0(
      new MockVPNService(&manager_, nullptr));
  scoped_refptr<MockVPNService> service1(
      new MockVPNService(&manager_, nullptr));
  scoped_refptr<MockVPNService> service2(
      new MockVPNService(&manager_, nullptr));

  provider_.services_.push_back(service0.get());
  provider_.services_.push_back(service1.get());
  provider_.services_.push_back(service2.get());

  ASSERT_EQ(3, provider_.services_.size());

  provider_.RemoveService(service1);

  EXPECT_EQ(2, provider_.services_.size());
  EXPECT_EQ(service0, provider_.services_[0]);
  EXPECT_EQ(service2, provider_.services_[1]);

  provider_.RemoveService(service2);

  EXPECT_EQ(1, provider_.services_.size());
  EXPECT_EQ(service0, provider_.services_[0]);

  provider_.RemoveService(service0);
  EXPECT_EQ(0, provider_.services_.size());
}

MATCHER_P(ServiceWithStorageId, storage_id, "") {
  return arg->GetStorageIdentifier() == storage_id;
}

TEST_F(VPNProviderTest, CreateServicesFromProfile) {
  FakeStore storage;
  storage.SetString("no_type", "Name", "No Type Entry");
  storage.SetString("no_vpn", "Type", "cellular");
  storage.SetString("vpn_no_provider_type", "Type", "vpn");
  storage.SetString("vpn_no_name", "Type", "vpn");
  storage.SetString("vpn_no_name", "Provider.Type", "openvpn");
  storage.SetString("vpn_no_host", "Type", "vpn");
  storage.SetString("vpn_no_host", "Provider.Type", "openvpn");
  storage.SetString("vpn_ho_host", "Name", "name");
  storage.SetString("vpn_complete", "Type", "vpn");
  storage.SetString("vpn_complete", "Provider.Type", "openvpn");
  storage.SetString("vpn_complete", "Name", "name");
  storage.SetString("vpn_complete", "Provider.Host", "1.2.3.4");

  scoped_refptr<MockProfile> profile(new NiceMock<MockProfile>(&manager_, ""));
  EXPECT_CALL(*profile, GetConstStorage()).WillRepeatedly(Return(&storage));

  EXPECT_CALL(manager_, RegisterService(ServiceWithStorageId("vpn_complete")));
  EXPECT_CALL(*profile, ConfigureService(ServiceWithStorageId("vpn_complete")))
      .WillOnce(Return(true));
  provider_.CreateServicesFromProfile(profile);

  GetServiceAt(0)->driver()->args()->Set<string>(kProviderHostProperty,
                                                 "1.2.3.4");
  // Calling this again should not create any more services (checked by the
  // Times(1) above).
  provider_.CreateServicesFromProfile(profile);
}

TEST_F(VPNProviderTest, CreateService) {
  static const char kName[] = "test-vpn-service";
  static const char kStorageID[] = "test_vpn_storage_id";
  static const char kHost[] = "test-vpn-host";
  static const char* const kTypes[] = {kProviderOpenVpn, kProviderL2tpIpsec,
                                       kProviderThirdPartyVpn};
  const size_t kTypesCount = base::size(kTypes);
  EXPECT_CALL(manager_, RegisterService(_)).Times(kTypesCount);
  for (auto type : kTypes) {
    Error error;
    VPNServiceRefPtr service =
        provider_.CreateService(type, kName, kStorageID, &error);
    ASSERT_NE(nullptr, service) << type;
    ASSERT_TRUE(service->driver()) << type;
    EXPECT_EQ(type, service->driver()->GetProviderType());
    EXPECT_EQ(kName, GetServiceFriendlyName(service)) << type;
    EXPECT_EQ(kStorageID, service->GetStorageIdentifier()) << type;
    EXPECT_FALSE(service->IsAlwaysOnVpn(kHost)) << type;
    EXPECT_TRUE(error.IsSuccess()) << type;
  }
  Error error;
  VPNServiceRefPtr unknown_service =
      provider_.CreateService("unknown-vpn-type", kName, kStorageID, &error);
  EXPECT_FALSE(unknown_service);
  EXPECT_EQ(Error::kNotSupported, error.type());
}

TEST_F(VPNProviderTest, CreateArcService) {
  static const char kName[] = "test-vpn-service";
  static const char kStorageID[] = "test_vpn_storage_id";
  static const char kHost[] = "com.example.test.vpn";
  EXPECT_CALL(manager_, RegisterService(_));
  Error error;
  VPNServiceRefPtr service =
      provider_.CreateService(kProviderArcVpn, kName, kStorageID, &error);
  ASSERT_NE(nullptr, service);
  ASSERT_TRUE(service->driver());
  service->driver()->args()->Set<string>(kProviderHostProperty, kHost);

  EXPECT_EQ(kProviderArcVpn, service->driver()->GetProviderType());
  EXPECT_EQ(kName, GetServiceFriendlyName(service));
  EXPECT_EQ(kStorageID, service->GetStorageIdentifier());
  EXPECT_TRUE(service->IsAlwaysOnVpn(kHost));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNProviderTest, CreateTemporaryServiceFromProfile) {
  FakeStore storage;
  storage.SetString("no_vpn", "Type", "cellular");
  storage.SetString("vpn_no_provider_type", "Type", "vpn");
  storage.SetString("vpn_no_name", "Type", "vpn");
  storage.SetString("vpn_no_name", "Provider.Type", "openvpn");
  storage.SetString("vpn_no_host", "Type", "vpn");
  storage.SetString("vpn_no_host", "Provider.Type", "openvpn");
  storage.SetString("vpn_no_host", "Name", "name");
  storage.SetString("vpn_complete", "Type", "vpn");
  storage.SetString("vpn_complete", "Provider.Type", "openvpn");
  storage.SetString("vpn_complete", "Name", "name");
  storage.SetString("vpn_complete", "Provider.Host", "1.2.3.4");

  scoped_refptr<MockProfile> profile(new NiceMock<MockProfile>(&manager_, ""));
  EXPECT_CALL(*profile, GetConstStorage()).WillRepeatedly(Return(&storage));
  Error error;

  // Non VPN entry.
  EXPECT_EQ(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         profile, "no_vpn", &error));
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(),
              StartsWith("Unspecified or invalid network type"));

  // VPN type not specified.
  error.Reset();
  EXPECT_EQ(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         profile, "vpn_no_provider_type", &error));
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(), StartsWith("VPN type not specified"));

  // Name not specified.
  error.Reset();
  EXPECT_EQ(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         profile, "vpn_no_name", &error));
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(), StartsWith("Network name not specified"));

  // Host not specified.
  error.Reset();
  EXPECT_EQ(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         profile, "vpn_no_host", &error));
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(), StartsWith("Host not specified"));

  // Valid VPN service entry.
  error.Reset();
  EXPECT_NE(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         profile, "vpn_complete", &error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNProviderTest, HasActiveService) {
  EXPECT_FALSE(provider_.HasActiveService());

  scoped_refptr<MockVPNService> service0(
      new MockVPNService(&manager_, nullptr));
  scoped_refptr<MockVPNService> service1(
      new MockVPNService(&manager_, nullptr));
  scoped_refptr<MockVPNService> service2(
      new MockVPNService(&manager_, nullptr));

  AddService(service0);
  AddService(service1);
  AddService(service2);
  EXPECT_FALSE(provider_.HasActiveService());

  SetConnectState(service1, Service::kStateAssociating);
  EXPECT_TRUE(provider_.HasActiveService());

  SetConnectState(service1, Service::kStateOnline);
  EXPECT_TRUE(provider_.HasActiveService());
}

TEST_F(VPNProviderTest, SetDefaultRoutingPolicy) {
  manager_.user_traffic_uids_.push_back(1000);
  RoutingPolicyEntry::FwMark expected_fwmark;
  expected_fwmark.value = 0x00008000;
  expected_fwmark.mask = 0x0000c000;

  IPConfig::Properties properties;
  provider_.SetDefaultRoutingPolicy(&properties);

  EXPECT_EQ(1, properties.allowed_uids.size());
  EXPECT_EQ(1000, properties.allowed_uids[0]);
  EXPECT_EQ(expected_fwmark, properties.included_fwmarks[0]);
}

}  // namespace shill
