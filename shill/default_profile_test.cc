// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/default_profile.h"

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "shill/dhcp/mock_dhcp_properties.h"
#include "shill/fake_store.h"
#include "shill/link_monitor.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_service.h"
#include "shill/portal_detector.h"
#include "shill/property_store_test.h"
#include "shill/resolver.h"

using base::FilePath;
using std::set;
using std::string;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace shill {

class DefaultProfileTest : public PropertyStoreTest {
 public:
  DefaultProfileTest()
      : profile_(new DefaultProfile(manager(),
                                    FilePath(storage_path()),
                                    DefaultProfile::kDefaultId,
                                    properties_)),
        device_(new MockDevice(manager(), "null0", "addr0", 0)) {}

  ~DefaultProfileTest() override = default;

 protected:
  static const char kTestStoragePath[];

  scoped_refptr<DefaultProfile> profile_;
  scoped_refptr<MockDevice> device_;
  Manager::Properties properties_;
};

const char DefaultProfileTest::kTestStoragePath[] = "/no/where";

TEST_F(DefaultProfileTest, GetProperties) {
  // DBusAdaptor::GetProperties() will iterate over all the accessors
  // provided by Profile. The |kEntriesProperty| accessor calls
  // GetGroups() on the StoreInterface.
  auto storage = std::make_unique<FakeStore>();
  profile_->SetStorageForTest(std::move(storage));

  Error error(Error::kInvalidProperty, "");
  {
    brillo::VariantDictionary props;
    Error error;
    profile_->store().GetProperties(&props, &error);
    ASSERT_FALSE(props.find(kArpGatewayProperty) == props.end());
    EXPECT_TRUE(props[kArpGatewayProperty].IsTypeCompatible<bool>());
    EXPECT_EQ(props[kArpGatewayProperty].Get<bool>(), properties_.arp_gateway);
  }
  properties_.arp_gateway = false;
  {
    brillo::VariantDictionary props;
    Error error;
    profile_->store().GetProperties(&props, &error);
    ASSERT_FALSE(props.find(kArpGatewayProperty) == props.end());
    EXPECT_TRUE(props[kArpGatewayProperty].IsTypeCompatible<bool>());
    EXPECT_EQ(props[kArpGatewayProperty].Get<bool>(), properties_.arp_gateway);
  }
  {
    Error error(Error::kInvalidProperty, "");
    EXPECT_FALSE(profile_->mutable_store()->SetBoolProperty(kArpGatewayProperty,
                                                            true, &error));
  }
}

TEST_F(DefaultProfileTest, Save) {
  auto owned_storage = std::make_unique<FakeStore>();
  FakeStore* storage = owned_storage.get();

  EXPECT_CALL(*device_, Save(storage)).Times(0);
  profile_->SetStorageForTest(std::move(owned_storage));
  auto dhcp_props = std::make_unique<MockDhcpProperties>();
  EXPECT_CALL(*dhcp_props, Save(_, _));
  manager()->dhcp_properties_ = std::move(dhcp_props);

  manager()->RegisterDevice(device_);
  ASSERT_TRUE(profile_->Save());

  bool gateway;
  EXPECT_TRUE(storage->GetBool(DefaultProfile::kStorageId,
                               DefaultProfile::kStorageArpGateway, &gateway));
  EXPECT_TRUE(gateway);
  std::string name;
  EXPECT_TRUE(storage->GetString(DefaultProfile::kStorageId,
                                 DefaultProfile::kStorageName, &name));
  EXPECT_EQ(name, DefaultProfile::kDefaultId);

  manager()->DeregisterDevice(device_);
}

TEST_F(DefaultProfileTest, LoadManagerDefaultProperties) {
  auto owned_storage = std::make_unique<FakeStore>();
  Manager::Properties manager_props;
  auto dhcp_props = std::make_unique<MockDhcpProperties>();
  EXPECT_CALL(*dhcp_props, Load(_, DefaultProfile::kStorageId));
  manager()->dhcp_properties_ = std::move(dhcp_props);
  profile_->SetStorageForTest(std::move(owned_storage));

  profile_->LoadManagerProperties(&manager_props,
                                  manager()->dhcp_properties_.get());
  EXPECT_TRUE(manager_props.arp_gateway);
  EXPECT_EQ(PortalDetector::kDefaultCheckPortalList,
            manager_props.check_portal_list);
  EXPECT_EQ(Resolver::kDefaultIgnoredSearchList,
            manager_props.ignored_dns_search_paths);
  EXPECT_EQ(LinkMonitor::kDefaultLinkMonitorTechnologies,
            manager_props.link_monitor_technologies);
  EXPECT_EQ("", manager_props.no_auto_connect_technologies);
  EXPECT_EQ(PortalDetector::kDefaultHttpUrl, manager_props.portal_http_url);
  EXPECT_EQ(PortalDetector::kDefaultHttpsUrl, manager_props.portal_https_url);
  EXPECT_EQ(PortalDetector::kDefaultFallbackHttpUrls,
            manager_props.portal_fallback_http_urls);
  EXPECT_EQ("", manager_props.prohibited_technologies);
#if !defined(DISABLE_WIFI)
  EXPECT_FALSE(manager_props.ft_enabled.has_value());
#endif  // DISABLE_WIFI
}

TEST_F(DefaultProfileTest, LoadManagerProperties) {
  auto owned_storage = std::make_unique<FakeStore>();
  FakeStore* storage = owned_storage.get();
  storage->SetBool(DefaultProfile::kStorageId,
                   DefaultProfile::kStorageArpGateway, false);
  const string portal_list("technology1,technology2");
  storage->SetString(DefaultProfile::kStorageId,
                     DefaultProfile::kStorageCheckPortalList, portal_list);
  const string ignored_paths("chromium.org,google.com");
  storage->SetString(DefaultProfile::kStorageId,
                     DefaultProfile::kStorageIgnoredDNSSearchPaths,
                     ignored_paths);
  const string link_monitor_technologies("ethernet,wifi");
  storage->SetString(DefaultProfile::kStorageId,
                     DefaultProfile::kStorageLinkMonitorTechnologies,
                     link_monitor_technologies);
  const string no_auto_connect_technologies("wifi,cellular");
  storage->SetString(DefaultProfile::kStorageId,
                     DefaultProfile::kStorageNoAutoConnectTechnologies,
                     no_auto_connect_technologies);
  const string prohibited_technologies("vpn,wifi");
  storage->SetString(DefaultProfile::kStorageId,
                     DefaultProfile::kStorageProhibitedTechnologies,
                     prohibited_technologies);
#if !defined(DISABLE_WIFI)
  storage->SetBool(DefaultProfile::kStorageId,
                   DefaultProfile::kStorageWifiGlobalFTEnabled, true);
#endif  // DISABLE_WIFI
  profile_->SetStorageForTest(std::move(owned_storage));
  Manager::Properties manager_props;
  auto dhcp_props = std::make_unique<MockDhcpProperties>();
  EXPECT_CALL(*dhcp_props, Load(_, DefaultProfile::kStorageId));
  manager()->dhcp_properties_ = std::move(dhcp_props);

  profile_->LoadManagerProperties(&manager_props,
                                  manager()->dhcp_properties_.get());
  EXPECT_FALSE(manager_props.arp_gateway);
  EXPECT_EQ(portal_list, manager_props.check_portal_list);
  EXPECT_EQ(ignored_paths, manager_props.ignored_dns_search_paths);
  EXPECT_EQ(link_monitor_technologies, manager_props.link_monitor_technologies);
  EXPECT_EQ(no_auto_connect_technologies,
            manager_props.no_auto_connect_technologies);
  EXPECT_EQ(prohibited_technologies, manager_props.prohibited_technologies);
#if !defined(DISABLE_WIFI)
  EXPECT_TRUE(manager_props.ft_enabled.has_value());
  EXPECT_TRUE(manager_props.ft_enabled.value());
#endif  // DISABLE_WIFI
}

TEST_F(DefaultProfileTest, GetStoragePath) {
  EXPECT_EQ(storage_path() + "/default.profile",
            profile_->persistent_profile_path().value());
}

TEST_F(DefaultProfileTest, ConfigureService) {
  auto owned_storage = std::make_unique<FakeStore>();
  scoped_refptr<MockService> unknown_service(new MockService(manager()));
  EXPECT_CALL(*unknown_service, technology())
      .WillOnce(Return(Technology::kUnknown));
  EXPECT_CALL(*unknown_service, Save(_)).Times(0);

  scoped_refptr<MockService> ethernet_service(new MockService(manager()));
  EXPECT_CALL(*ethernet_service, technology())
      .WillOnce(Return(Technology::kEthernet));
  EXPECT_CALL(*ethernet_service, Save(owned_storage.get()))
      .WillOnce(Return(true));

  profile_->SetStorageForTest(std::move(owned_storage));
  EXPECT_FALSE(profile_->ConfigureService(unknown_service));
  EXPECT_TRUE(profile_->ConfigureService(ethernet_service));
}

TEST_F(DefaultProfileTest, UpdateDevice) {
  auto owned_storage = std::make_unique<FakeStore>();
  EXPECT_CALL(*device_, Save(owned_storage.get()))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  profile_->SetStorageForTest(std::move(owned_storage));
  EXPECT_TRUE(profile_->UpdateDevice(device_));
  EXPECT_FALSE(profile_->UpdateDevice(device_));
}

}  // namespace shill
