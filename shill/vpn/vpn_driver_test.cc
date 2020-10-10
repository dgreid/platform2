// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_driver.h"

#include <vector>

#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/fake_store.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_service.h"
#include "shill/property_store.h"
#include "shill/test_event_dispatcher.h"

using std::string;
using std::vector;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::Test;

namespace shill {

namespace {

const char kVPNHostProperty[] = "VPN.Host";
const char kOTPProperty[] = "VPN.OTP";
const char kPinProperty[] = "VPN.PIN";
const char kPSKProperty[] = "VPN.PSK";
const char kPasswordProperty[] = "VPN.Password";
const char kPortProperty[] = "VPN.Port";

const char kPin[] = "5555";
const char kPassword[] = "random-password";
const char kPort[] = "1234";
const char kStorageID[] = "vpn_service_id";

}  // namespace

class VPNDriverUnderTest : public VPNDriver {
 public:
  explicit VPNDriverUnderTest(Manager* manager);
  ~VPNDriverUnderTest() override = default;

  // Inherited from VPNDriver.
  MOCK_METHOD(void, Connect, (const VPNServiceRefPtr&, Error*), (override));
  MOCK_METHOD(void, Disconnect, (), (override));
  MOCK_METHOD(string, GetProviderType, (), (const, override));
  MOCK_METHOD(IfType, GetIfType, (), (const, override));

 private:
  static const Property kProperties[];

  DISALLOW_COPY_AND_ASSIGN(VPNDriverUnderTest);
};

// static
const VPNDriverUnderTest::Property VPNDriverUnderTest::kProperties[] = {
    {kEapCaCertPemProperty, Property::kArray},
    {kVPNHostProperty, 0},
    {kL2tpIpsecCaCertPemProperty, Property::kArray},
    {kOTPProperty, Property::kEphemeral},
    {kPinProperty, Property::kWriteOnly},
    {kPSKProperty, Property::kCredential},
    {kPasswordProperty, Property::kCredential},
    {kPortProperty, 0},
    {kProviderTypeProperty, 0},
};

VPNDriverUnderTest::VPNDriverUnderTest(Manager* manager)
    : VPNDriver(manager, nullptr, kProperties, base::size(kProperties)) {}

class VPNDriverTest : public Test {
 public:
  VPNDriverTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        device_info_(&manager_),
        driver_(&manager_) {}

  ~VPNDriverTest() override = default;

 protected:
  const base::CancelableClosure& connect_timeout_callback() const {
    return driver_.connect_timeout_callback_;
  }

  bool IsConnectTimeoutStarted() { return driver_.IsConnectTimeoutStarted(); }

  int connect_timeout_seconds() const {
    return driver_.connect_timeout_seconds();
  }

  string credential_prefix() const { return VPNDriver::kCredentialPrefix; }

  void StartConnectTimeout(int timeout_seconds) {
    driver_.StartConnectTimeout(timeout_seconds);
  }

  void StopConnectTimeout() { driver_.StopConnectTimeout(); }

  void SetArg(const string& arg, const string& value) {
    driver_.args()->Set<string>(arg, value);
  }

  void SetArgArray(const string& arg, const vector<string>& value) {
    driver_.args()->Set<Strings>(arg, value);
  }

  KeyValueStore* GetArgs() { return driver_.args(); }

  bool GetProviderPropertyString(const PropertyStore& store,
                                 const string& key,
                                 string* value);

  bool GetProviderPropertyStrings(const PropertyStore& store,
                                  const string& key,
                                  vector<string>* value);

  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  NiceMock<MockDeviceInfo> device_info_;
  VPNDriverUnderTest driver_;
};

bool VPNDriverTest::GetProviderPropertyString(const PropertyStore& store,
                                              const string& key,
                                              string* value) {
  KeyValueStore provider_properties;
  Error error;
  EXPECT_TRUE(store.GetKeyValueStoreProperty(kProviderProperty,
                                             &provider_properties, &error));
  if (!provider_properties.Contains<string>(key)) {
    return false;
  }
  if (value != nullptr) {
    *value = provider_properties.Get<string>(key);
  }
  return true;
}

bool VPNDriverTest::GetProviderPropertyStrings(const PropertyStore& store,
                                               const string& key,
                                               vector<string>* value) {
  KeyValueStore provider_properties;
  Error error;
  EXPECT_TRUE(store.GetKeyValueStoreProperty(kProviderProperty,
                                             &provider_properties, &error));
  if (!provider_properties.Contains<Strings>(key)) {
    return false;
  }
  if (value != nullptr) {
    *value = provider_properties.Get<Strings>(key);
  }
  return true;
}

TEST_F(VPNDriverTest, Load) {
  FakeStore storage;
  GetArgs()->Set<string>(kVPNHostProperty, "1.2.3.4");
  GetArgs()->Set<string>(kPSKProperty, "1234");
  GetArgs()->Set<Strings>(kL2tpIpsecCaCertPemProperty,
                          vector<string>{"cleared-cert0", "cleared-cert1"});
  vector<string> kCaCerts{"cert0", "cert1"};
  storage.SetStringList(kStorageID, kEapCaCertPemProperty, kCaCerts);
  storage.SetString(kStorageID, kPortProperty, kPort);
  storage.SetString(kStorageID, kPinProperty, kPin);
  storage.SetString(kStorageID, credential_prefix() + kPasswordProperty,
                    kPassword);

  EXPECT_TRUE(driver_.Load(&storage, kStorageID));

  EXPECT_EQ(kCaCerts, GetArgs()->Get<Strings>(kEapCaCertPemProperty));
  EXPECT_EQ(kPort, GetArgs()->Lookup<string>(kPortProperty, ""));
  EXPECT_EQ(kPin, GetArgs()->Lookup<string>(kPinProperty, ""));
  EXPECT_EQ(kPassword, GetArgs()->Lookup<string>(kPasswordProperty, ""));

  // Properties missing from the persistent store should be deleted.
  EXPECT_FALSE(GetArgs()->Contains<string>(kVPNHostProperty));
  EXPECT_FALSE(GetArgs()->Contains<Strings>(kL2tpIpsecCaCertPemProperty));
  EXPECT_FALSE(GetArgs()->Contains<string>(kPSKProperty));
}

TEST_F(VPNDriverTest, Save) {
  SetArg(kProviderTypeProperty, kProviderOpenVpn);
  SetArg(kPinProperty, kPin);
  SetArg(kPortProperty, kPort);
  SetArg(kPasswordProperty, kPassword);
  SetArg(kOTPProperty, "987654");
  const vector<string> kCaCerts{"cert0", "cert1"};
  SetArgArray(kEapCaCertPemProperty, kCaCerts);

  FakeStore storage;
  EXPECT_TRUE(driver_.Save(&storage, kStorageID, true));

  vector<string> ca_pem;
  string provider_type, port, pin, password;
  EXPECT_TRUE(
      storage.GetStringList(kStorageID, kEapCaCertPemProperty, &ca_pem));
  EXPECT_EQ(ca_pem, kCaCerts);
  EXPECT_TRUE(
      storage.GetString(kStorageID, kProviderTypeProperty, &provider_type));
  EXPECT_EQ(provider_type, kProviderOpenVpn);
  EXPECT_TRUE(storage.GetString(kStorageID, kPortProperty, &port));
  EXPECT_EQ(port, kPort);
  EXPECT_TRUE(storage.GetString(kStorageID, kPinProperty, &pin));
  EXPECT_EQ(pin, kPin);
  EXPECT_TRUE(storage.GetString(
      kStorageID, credential_prefix() + kPasswordProperty, &password));
  EXPECT_EQ(password, kPassword);

  EXPECT_FALSE(storage.GetString(kStorageID, credential_prefix() + kPSKProperty,
                                 nullptr));
}

TEST_F(VPNDriverTest, SaveNoCredentials) {
  SetArg(kPasswordProperty, kPassword);
  SetArg(kPSKProperty, "");

  FakeStore storage;
  EXPECT_TRUE(driver_.Save(&storage, kStorageID, false));

  EXPECT_FALSE(storage.GetString(
      kStorageID, credential_prefix() + kPasswordProperty, nullptr));
  EXPECT_FALSE(storage.GetString(kStorageID, credential_prefix() + kPSKProperty,
                                 nullptr));
  EXPECT_FALSE(storage.GetString(kStorageID, kEapCaCertPemProperty, nullptr));
  EXPECT_FALSE(
      storage.GetString(kStorageID, kL2tpIpsecCaCertPemProperty, nullptr));
}

TEST_F(VPNDriverTest, UnloadCredentials) {
  SetArg(kOTPProperty, "654321");
  SetArg(kPasswordProperty, kPassword);
  SetArg(kPortProperty, kPort);
  driver_.UnloadCredentials();
  EXPECT_FALSE(GetArgs()->Contains<string>(kOTPProperty));
  EXPECT_FALSE(GetArgs()->Contains<string>(kPasswordProperty));
  EXPECT_EQ(kPort, GetArgs()->Lookup<string>(kPortProperty, ""));
}

TEST_F(VPNDriverTest, InitPropertyStore) {
  // Figure out if the store is actually hooked up to the driver argument
  // KeyValueStore.
  PropertyStore store;
  driver_.InitPropertyStore(&store);

  // An un-set property should not be readable.
  {
    Error error;
    EXPECT_FALSE(store.GetStringProperty(kPortProperty, nullptr, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  {
    Error error;
    EXPECT_FALSE(
        store.GetStringsProperty(kEapCaCertPemProperty, nullptr, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  EXPECT_FALSE(GetProviderPropertyString(store, kPortProperty, nullptr));
  EXPECT_FALSE(
      GetProviderPropertyStrings(store, kEapCaCertPemProperty, nullptr));

  const string kProviderType = "boo";
  SetArg(kPortProperty, kPort);
  SetArg(kPasswordProperty, kPassword);
  SetArg(kProviderTypeProperty, kProviderType);
  SetArg(kVPNHostProperty, "");
  const vector<string> kCaCerts{"cert1"};
  SetArgArray(kEapCaCertPemProperty, kCaCerts);
  SetArgArray(kL2tpIpsecCaCertPemProperty, vector<string>());

  // We should not be able to read a property out of the driver args using the
  // key to the args directly.
  {
    Error error;
    EXPECT_FALSE(store.GetStringProperty(kPortProperty, nullptr, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  {
    Error error;
    EXPECT_FALSE(
        store.GetStringsProperty(kEapCaCertPemProperty, nullptr, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }

  // We should instead be able to find it within the "Provider" stringmap.
  {
    string value;
    EXPECT_TRUE(GetProviderPropertyString(store, kPortProperty, &value));
    EXPECT_EQ(kPort, value);
  }
  {
    vector<string> value;
    EXPECT_TRUE(
        GetProviderPropertyStrings(store, kEapCaCertPemProperty, &value));
    EXPECT_EQ(kCaCerts, value);
  }

  // We should be able to read empty properties from the "Provider" stringmap.
  {
    string value;
    EXPECT_TRUE(GetProviderPropertyString(store, kVPNHostProperty, &value));
    EXPECT_TRUE(value.empty());
  }
  {
    vector<string> value;
    EXPECT_TRUE(
        GetProviderPropertyStrings(store, kL2tpIpsecCaCertPemProperty, &value));
    EXPECT_TRUE(value.empty());
  }

  // Properties that start with the prefix "Provider." should be mapped to the
  // name in the Properties dict with the prefix removed.
  {
    string value;
    EXPECT_TRUE(GetProviderPropertyString(store, kTypeProperty, &value));
    EXPECT_EQ(kProviderType, value);
  }

  // If we clear a property, we should no longer be able to find it.
  {
    Error error;
    EXPECT_TRUE(store.ClearProperty(kPortProperty, &error));
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_FALSE(GetProviderPropertyString(store, kPortProperty, nullptr));
  }
  {
    Error error;
    EXPECT_TRUE(store.ClearProperty(kEapCaCertPemProperty, &error));
    EXPECT_TRUE(error.IsSuccess());
    EXPECT_FALSE(
        GetProviderPropertyStrings(store, kEapCaCertPemProperty, nullptr));
  }

  // A second attempt to clear this property should return an error.
  {
    Error error;
    EXPECT_FALSE(store.ClearProperty(kPortProperty, &error));
    EXPECT_EQ(Error::kNotFound, error.type());
  }
  {
    Error error;
    EXPECT_FALSE(store.ClearProperty(kEapCaCertPemProperty, &error));
    EXPECT_EQ(Error::kNotFound, error.type());
  }

  // Test write only properties.
  EXPECT_FALSE(GetProviderPropertyString(store, kPinProperty, nullptr));

  // Write properties to the driver args using the PropertyStore interface.
  {
    const string kValue = "some-value";
    Error error;
    EXPECT_TRUE(store.SetStringProperty(kPinProperty, kValue, &error));
    EXPECT_EQ(kValue, GetArgs()->Get<string>(kPinProperty));
  }
  {
    const vector<string> kValue{"some-value"};
    Error error;
    EXPECT_TRUE(
        store.SetStringsProperty(kEapCaCertPemProperty, kValue, &error));
    EXPECT_EQ(kValue, GetArgs()->Get<Strings>(kEapCaCertPemProperty));
  }
}

TEST_F(VPNDriverTest, ConnectTimeout) {
  EXPECT_TRUE(connect_timeout_callback().IsCancelled());
  EXPECT_FALSE(IsConnectTimeoutStarted());
  StartConnectTimeout(0);
  EXPECT_EQ(0, connect_timeout_seconds());
  EXPECT_FALSE(connect_timeout_callback().IsCancelled());
  EXPECT_TRUE(IsConnectTimeoutStarted());
  StartConnectTimeout(10);  // This should take no effect.
  EXPECT_EQ(0, connect_timeout_seconds());
  dispatcher_.DispatchPendingEvents();
  EXPECT_TRUE(connect_timeout_callback().IsCancelled());
  EXPECT_FALSE(IsConnectTimeoutStarted());
}

TEST_F(VPNDriverTest, StartStopConnectTimeout) {
  EXPECT_FALSE(IsConnectTimeoutStarted());
  EXPECT_EQ(0, connect_timeout_seconds());
  const int kTimeout = 123;
  StartConnectTimeout(kTimeout);
  EXPECT_TRUE(IsConnectTimeoutStarted());
  EXPECT_EQ(kTimeout, connect_timeout_seconds());
  StartConnectTimeout(kTimeout - 20);
  EXPECT_EQ(kTimeout, connect_timeout_seconds());
  StopConnectTimeout();
  EXPECT_FALSE(IsConnectTimeoutStarted());
  EXPECT_EQ(0, connect_timeout_seconds());
}

}  // namespace shill
