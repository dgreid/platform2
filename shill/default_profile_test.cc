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
#include "shill/link_monitor.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_service.h"
#include "shill/mock_store.h"
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
  auto storage = std::make_unique<MockStore>();
  set<string> empty_group_set;
  EXPECT_CALL(*storage, GetGroups()).WillRepeatedly(Return(empty_group_set));
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
  auto storage = std::make_unique<MockStore>();
  EXPECT_CALL(*storage, SetBool(DefaultProfile::kStorageId,
                                DefaultProfile::kStorageArpGateway, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*storage, SetString(DefaultProfile::kStorageId,
                                  DefaultProfile::kStorageName,
                                  DefaultProfile::kDefaultId))
      .WillOnce(Return(true));
  EXPECT_CALL(*storage, SetString(DefaultProfile::kStorageId,
                                  DefaultProfile::kStorageHostName, ""))
      .WillOnce(Return(true));
  EXPECT_CALL(*storage, SetString(DefaultProfile::kStorageId,
                                  DefaultProfile::kStorageCheckPortalList, ""))
      .WillOnce(Return(true));
  EXPECT_CALL(*storage,
              SetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageIgnoredDNSSearchPaths, ""))
      .WillOnce(Return(true));
  EXPECT_CALL(*storage,
              SetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageLinkMonitorTechnologies, ""))
      .WillOnce(Return(true));
  EXPECT_CALL(*storage,
              SetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageNoAutoConnectTechnologies, ""))
      .WillOnce(Return(true));
  EXPECT_CALL(*storage,
              SetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageProhibitedTechnologies, ""))
      .WillOnce(Return(true));
#if !defined(DISABLE_WIFI)
  EXPECT_CALL(*storage, SetBool(DefaultProfile::kStorageId,
                                DefaultProfile::kStorageWifiGlobalFTEnabled, _))
      .Times(0);
#endif  // DISABLE_WIFI
  EXPECT_CALL(*storage, Flush()).WillOnce(Return(true));

  EXPECT_CALL(*device_, Save(storage.get())).Times(0);
  profile_->SetStorageForTest(std::move(storage));
  auto dhcp_props = std::make_unique<MockDhcpProperties>();
  EXPECT_CALL(*dhcp_props, Save(_, _));
  manager()->dhcp_properties_ = std::move(dhcp_props);

  manager()->RegisterDevice(device_);
  ASSERT_TRUE(profile_->Save());
  manager()->DeregisterDevice(device_);
}

TEST_F(DefaultProfileTest, LoadManagerDefaultProperties) {
  auto storage = std::make_unique<MockStore>();
  Manager::Properties manager_props;
  EXPECT_CALL(*storage, GetBool(DefaultProfile::kStorageId,
                                DefaultProfile::kStorageArpGateway,
                                &manager_props.arp_gateway))
      .WillOnce(Return(false));
  EXPECT_CALL(*storage, GetString(DefaultProfile::kStorageId,
                                  DefaultProfile::kStorageHostName,
                                  &manager_props.host_name))
      .WillOnce(Return(false));
  EXPECT_CALL(*storage, GetString(DefaultProfile::kStorageId,
                                  DefaultProfile::kStorageCheckPortalList,
                                  &manager_props.check_portal_list))
      .WillOnce(Return(false));
  EXPECT_CALL(*storage, GetString(DefaultProfile::kStorageId,
                                  DefaultProfile::kStorageIgnoredDNSSearchPaths,
                                  &manager_props.ignored_dns_search_paths))
      .WillOnce(Return(false));
  EXPECT_CALL(*storage,
              GetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageLinkMonitorTechnologies, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*storage,
              GetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageNoAutoConnectTechnologies, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*storage,
              GetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageProhibitedTechnologies, _))
      .WillOnce(Return(false));
#if !defined(DISABLE_WIFI)
  EXPECT_CALL(*storage, GetBool(DefaultProfile::kStorageId,
                                DefaultProfile::kStorageWifiGlobalFTEnabled, _))
      .WillOnce(Return(false));
#endif  // DISABLE_WIFI
  auto dhcp_props = std::make_unique<MockDhcpProperties>();
  EXPECT_CALL(*dhcp_props, Load(_, DefaultProfile::kStorageId));
  manager()->dhcp_properties_ = std::move(dhcp_props);
  profile_->SetStorageForTest(std::move(storage));

  profile_->LoadManagerProperties(&manager_props,
                                  manager()->dhcp_properties_.get());
  EXPECT_TRUE(manager_props.arp_gateway);
  EXPECT_EQ("", manager_props.host_name);
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
  auto storage = std::make_unique<MockStore>();
  const string host_name("hostname");
  EXPECT_CALL(*storage, GetBool(DefaultProfile::kStorageId,
                                DefaultProfile::kStorageArpGateway, _))
      .WillOnce(DoAll(SetArgPointee<2>(false), Return(true)));
  EXPECT_CALL(*storage, GetString(DefaultProfile::kStorageId,
                                  DefaultProfile::kStorageHostName, _))
      .WillOnce(DoAll(SetArgPointee<2>(host_name), Return(true)));
  const string portal_list("technology1,technology2");
  EXPECT_CALL(*storage, GetString(DefaultProfile::kStorageId,
                                  DefaultProfile::kStorageCheckPortalList, _))
      .WillOnce(DoAll(SetArgPointee<2>(portal_list), Return(true)));
  const string ignored_paths("chromium.org,google.com");
  EXPECT_CALL(*storage,
              GetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageIgnoredDNSSearchPaths, _))
      .WillOnce(DoAll(SetArgPointee<2>(ignored_paths), Return(true)));
  const string link_monitor_technologies("ethernet,wifi");
  EXPECT_CALL(*storage,
              GetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageLinkMonitorTechnologies, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(link_monitor_technologies), Return(true)));
  const string no_auto_connect_technologies("wifi,cellular");
  EXPECT_CALL(*storage,
              GetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageNoAutoConnectTechnologies, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(no_auto_connect_technologies), Return(true)));
  const string prohibited_technologies("vpn,wifi");
  EXPECT_CALL(*storage,
              GetString(DefaultProfile::kStorageId,
                        DefaultProfile::kStorageProhibitedTechnologies, _))
      .WillOnce(DoAll(SetArgPointee<2>(prohibited_technologies), Return(true)));
#if !defined(DISABLE_WIFI)
  EXPECT_CALL(*storage, GetBool(DefaultProfile::kStorageId,
                                DefaultProfile::kStorageWifiGlobalFTEnabled, _))
      .WillOnce(DoAll(SetArgPointee<2>(true), Return(true)));
#endif  // DISABLE_WIFI
  profile_->SetStorageForTest(std::move(storage));
  Manager::Properties manager_props;
  auto dhcp_props = std::make_unique<MockDhcpProperties>();
  EXPECT_CALL(*dhcp_props, Load(_, DefaultProfile::kStorageId));
  manager()->dhcp_properties_ = std::move(dhcp_props);

  profile_->LoadManagerProperties(&manager_props,
                                  manager()->dhcp_properties_.get());
  EXPECT_FALSE(manager_props.arp_gateway);
  EXPECT_EQ(host_name, manager_props.host_name);
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
  auto storage = std::make_unique<MockStore>();
  EXPECT_CALL(*storage, ContainsGroup(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*storage, Flush()).WillOnce(Return(true));

  scoped_refptr<MockService> unknown_service(new MockService(manager()));
  EXPECT_CALL(*unknown_service, technology())
      .WillOnce(Return(Technology::kUnknown));
  EXPECT_CALL(*unknown_service, Save(_)).Times(0);

  scoped_refptr<MockService> ethernet_service(new MockService(manager()));
  EXPECT_CALL(*ethernet_service, technology())
      .WillOnce(Return(Technology::kEthernet));
  EXPECT_CALL(*ethernet_service, Save(storage.get())).WillOnce(Return(true));

  profile_->SetStorageForTest(std::move(storage));
  EXPECT_FALSE(profile_->ConfigureService(unknown_service));
  EXPECT_TRUE(profile_->ConfigureService(ethernet_service));
}

TEST_F(DefaultProfileTest, UpdateDevice) {
  auto storage = std::make_unique<MockStore>();
  EXPECT_CALL(*storage, Flush()).WillOnce(Return(true));
  EXPECT_CALL(*device_, Save(storage.get()))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  profile_->SetStorageForTest(std::move(storage));
  EXPECT_TRUE(profile_->UpdateDevice(device_));
  EXPECT_FALSE(profile_->UpdateDevice(device_));
}

}  // namespace shill
