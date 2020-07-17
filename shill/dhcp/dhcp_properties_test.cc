// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp/dhcp_properties.h"

#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_store.h"
#include "shill/property_store.h"
#include "shill/test_event_dispatcher.h"

using std::string;
using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::Test;

namespace shill {

namespace {
const char kVendorClass[] = "Chromebook";
const char kHostname[] = "TestHost";
const char kStorageID[] = "dhcp_service_id";
const char kOverrideValue[] = "override";
}  // namespace

class DhcpPropertiesTest : public Test {
 public:
  virtual ~DhcpPropertiesTest() {}

  KeyValueStore& GetDhcpProperties() { return dhcp_properties_.properties_; }

 protected:
  NiceMock<MockControl> control_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> mock_manager_{&control_, &dispatcher_, &metrics_};
  DhcpProperties dhcp_properties_{&mock_manager_};
};

TEST_F(DhcpPropertiesTest, Ctor) {
  EXPECT_TRUE(GetDhcpProperties().IsEmpty());
}

TEST_F(DhcpPropertiesTest, InitPropertyStore) {
  PropertyStore store;
  dhcp_properties_.InitPropertyStore(&store);

  Error error;
  string value_in_prop_store;
  // DHCPProperty.Hostname is a valid option.
  EXPECT_FALSE(store.GetStringProperty("DHCPProperty.Hostname",
                                       &value_in_prop_store, &error));
  EXPECT_EQ(Error::kNotFound, error.type());

  // DHCPProperty.VendorClass is a valid option.
  EXPECT_FALSE(store.GetStringProperty("DHCPProperty.VendorClass",
                                       &value_in_prop_store, &error));
  EXPECT_EQ(Error::kNotFound, error.type());

  // DhcpProperty.NotAProp is not a valid option.
  EXPECT_FALSE(store.GetStringProperty("DHCPProperty.NotAProp",
                                       &value_in_prop_store, &error));
  EXPECT_EQ(Error::kInvalidProperty, error.type());
}

TEST_F(DhcpPropertiesTest, SetMappedStringPropertyOverrideExisting) {
  PropertyStore store;
  dhcp_properties_.InitPropertyStore(&store);
  GetDhcpProperties().Set<string>("Hostname", kHostname);

  Error error;
  EXPECT_TRUE(
      store.SetStringProperty("DHCPProperty.Hostname", kOverrideValue, &error));
  EXPECT_EQ(kOverrideValue, GetDhcpProperties().Get<string>("Hostname"));
}

TEST_F(DhcpPropertiesTest, SetMappedStringPropertyNoExistingValue) {
  PropertyStore store;
  dhcp_properties_.InitPropertyStore(&store);

  Error error;
  EXPECT_TRUE(
      store.SetStringProperty("DHCPProperty.Hostname", kHostname, &error));
  EXPECT_EQ(kHostname, GetDhcpProperties().Get<string>("Hostname"));
}

TEST_F(DhcpPropertiesTest, SetMappedStringPropertySameAsExistingValue) {
  PropertyStore store;
  dhcp_properties_.InitPropertyStore(&store);
  GetDhcpProperties().Set<string>("Hostname", kHostname);

  Error error;
  EXPECT_FALSE(
      store.SetStringProperty("DHCPProperty.Hostname", kHostname, &error));
  EXPECT_EQ(kHostname, GetDhcpProperties().Get<string>("Hostname"));
}

TEST_F(DhcpPropertiesTest, DhcpPropertyChanged) {
  const std::string kTestHostname = "test-hostname";
  EXPECT_CALL(mock_manager_, OnDhcpPropertyChanged(_, _));

  PropertyStore store;
  dhcp_properties_.InitPropertyStore(&store);
  dhcp_properties_.SetMappedStringProperty(/*index=*/0, kTestHostname,
                                           /*error=*/nullptr);
}

TEST_F(DhcpPropertiesTest, GetMappedStringPropertyWithSetValue) {
  PropertyStore store;
  dhcp_properties_.InitPropertyStore(&store);
  GetDhcpProperties().Set<string>("Hostname", kHostname);

  Error error;
  string value_in_prop_store;
  store.GetStringProperty("DHCPProperty.Hostname", &value_in_prop_store,
                          &error);
  EXPECT_EQ(kHostname, value_in_prop_store);
}

TEST_F(DhcpPropertiesTest, GetMappedStringPropertyNoExistingValue) {
  PropertyStore store;
  dhcp_properties_.InitPropertyStore(&store);

  Error error;
  string value_in_prop_store;
  store.GetStringProperty("DHCPProperty.Hostname", &value_in_prop_store,
                          &error);
  EXPECT_EQ(Error::kNotFound, error.type());
}

TEST_F(DhcpPropertiesTest, ClearMappedStringPropertyWithSetValue) {
  PropertyStore store;
  dhcp_properties_.InitPropertyStore(&store);
  GetDhcpProperties().Set<string>("Hostname", kHostname);

  Error error;
  string value_in_prop_store;
  store.ClearProperty("DHCPProperty.Hostname", &error);
  EXPECT_FALSE(GetDhcpProperties().Contains<string>("Hostname"));
}

TEST_F(DhcpPropertiesTest, ClearMappedStringPropertyNoExistingValue) {
  PropertyStore store;
  dhcp_properties_.InitPropertyStore(&store);

  Error error;
  string value_in_prop_store;
  store.ClearProperty("DHCPProperty.Hostname", &error);
  EXPECT_EQ(Error::kNotFound, error.type());
}

TEST_F(DhcpPropertiesTest, LoadEmpty) {
  MockStore storage;
  EXPECT_CALL(storage, GetString(kStorageID, "DHCPProperty.VendorClass", _))
      .WillOnce(Return(false));
  EXPECT_CALL(storage, GetString(kStorageID, "DHCPProperty.Hostname", _))
      .WillOnce(Return(false));
  dhcp_properties_.Load(&storage, kStorageID);
  EXPECT_TRUE(GetDhcpProperties().IsEmpty());
}

TEST_F(DhcpPropertiesTest, Load) {
  MockStore storage;
  EXPECT_CALL(storage, GetString(kStorageID, "DHCPProperty.VendorClass", _))
      .WillOnce(DoAll(SetArgPointee<2>(string(kVendorClass)), Return(true)));
  EXPECT_CALL(storage, GetString(kStorageID, "DHCPProperty.Hostname", _))
      .WillOnce(DoAll(SetArgPointee<2>(string(kHostname)), Return(true)));
  dhcp_properties_.Load(&storage, kStorageID);
  EXPECT_EQ(kVendorClass, GetDhcpProperties().Get<string>("VendorClass"));
  EXPECT_EQ(kHostname, GetDhcpProperties().Get<string>("Hostname"));
}

TEST_F(DhcpPropertiesTest, LoadWithValuesSetAndClearRequired) {
  MockStore storage;
  GetDhcpProperties().Set<string>("Hostname", kHostname);

  EXPECT_CALL(storage, GetString(kStorageID, "DHCPProperty.VendorClass", _))
      .WillOnce(DoAll(SetArgPointee<2>(string(kVendorClass)), Return(true)));
  EXPECT_CALL(storage, GetString(kStorageID, "DHCPProperty.Hostname", _))
      .WillOnce(Return(false));
  dhcp_properties_.Load(&storage, kStorageID);
  EXPECT_EQ(kVendorClass, GetDhcpProperties().Get<string>("VendorClass"));
  EXPECT_FALSE(GetDhcpProperties().ContainsVariant("Hostname"));
}

TEST_F(DhcpPropertiesTest, SaveWithValuesSet) {
  MockStore storage;
  GetDhcpProperties().Set<string>("VendorClass", kVendorClass);
  GetDhcpProperties().Set<string>("Hostname", "");

  EXPECT_CALL(storage,
              SetString(kStorageID, "DHCPProperty.VendorClass", kVendorClass))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, SetString(kStorageID, "DHCPProperty.Hostname", ""))
      .WillOnce(Return(true));
  dhcp_properties_.Save(&storage, kStorageID);
}

TEST_F(DhcpPropertiesTest, SavePropertyNotSetShouldBeDeleted) {
  MockStore storage;
  GetDhcpProperties().Set<string>("VendorClass", kVendorClass);

  EXPECT_CALL(storage, SetString(_, _, _)).Times(0);
  EXPECT_CALL(storage,
              SetString(kStorageID, "DHCPProperty.VendorClass", kVendorClass))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, DeleteKey(kStorageID, "DHCPProperty.Hostname"))
      .WillOnce(Return(true));
  dhcp_properties_.Save(&storage, kStorageID);
}

TEST_F(DhcpPropertiesTest, GetValueForProperty) {
  string value;
  EXPECT_FALSE(dhcp_properties_.GetValueForProperty("VendorClass", &value));
  EXPECT_FALSE(dhcp_properties_.GetValueForProperty("Hostname", &value));

  GetDhcpProperties().Set<string>("VendorClass", kVendorClass);
  EXPECT_TRUE(dhcp_properties_.GetValueForProperty("VendorClass", &value));
  EXPECT_EQ(kVendorClass, value);
  EXPECT_FALSE(dhcp_properties_.GetValueForProperty("Hostname", &value));

  GetDhcpProperties().Set<string>("Hostname", kHostname);
  EXPECT_TRUE(dhcp_properties_.GetValueForProperty("VendorClass", &value));
  EXPECT_EQ(kVendorClass, value);
  EXPECT_TRUE(dhcp_properties_.GetValueForProperty("Hostname", &value));
  EXPECT_EQ(kHostname, value);
}

}  // namespace shill
