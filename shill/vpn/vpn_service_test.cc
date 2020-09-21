// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_service.h"

#include <string>

#include <base/memory/ptr_util.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "shill/error.h"
#include "shill/fake_store.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_connection.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/mock_service.h"
#include "shill/service_property_change_test.h"
#include "shill/vpn/mock_vpn_driver.h"
#include "shill/vpn/mock_vpn_provider.h"

using std::string;
using testing::_;
using testing::ByMove;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::ReturnRefOfCopy;

namespace shill {

class VPNServiceTest : public testing::Test {
 public:
  VPNServiceTest()
      : interface_name_("test-interface"),
        manager_(&control_, nullptr, &metrics_),
        device_info_(&manager_) {
    Service::SetNextSerialNumberForTesting(0);
    driver_ = new MockVPNDriver();
    EXPECT_CALL(*driver_, GetProviderType())
        .WillRepeatedly(Return(kProviderL2tpIpsec));
    connection_ = new NiceMock<MockConnection>(&device_info_);
    service_ = new VPNService(&manager_, base::WrapUnique(driver_));
  }

  ~VPNServiceTest() override = default;

 protected:
  void SetUp() override {
    ON_CALL(*connection_, interface_name())
        .WillByDefault(ReturnRef(interface_name_));
    ON_CALL(*connection_, ipconfig_rpc_identifier())
        .WillByDefault(ReturnRef(ipconfig_rpc_identifier_));
  }

  void TearDown() override { EXPECT_CALL(device_info_, FlushAddresses(0)); }

  void SetServiceState(Service::ConnectState state) {
    service_->state_ = state;
  }

  void SetHasEverConnected(bool connected) {
    service_->has_ever_connected_ = connected;
  }

  void SetConnectable(bool connectable) {
    service_->connectable_ = connectable;
  }

  const char* GetAutoConnOffline() { return Service::kAutoConnOffline; }

  const char* GetAutoConnNeverConnected() {
    return VPNService::kAutoConnNeverConnected;
  }

  const char* GetAutoConnVPNAlreadyActive() {
    return VPNService::kAutoConnVPNAlreadyActive;
  }

  bool IsAutoConnectable(const char** reason) const {
    return service_->IsAutoConnectable(reason);
  }

  // Takes ownership of |provider|.
  void SetVPNProvider(VPNProvider* provider) {
    manager_.vpn_provider_.reset(provider);
    manager_.UpdateProviderMapping();
  }

  ServiceMockAdaptor* GetAdaptor() {
    return static_cast<ServiceMockAdaptor*>(service_->adaptor());
  }

  ServiceRefPtr CreateUnderlyingService(
      ConnectionRefPtr underlying_connection) {
    auto service = new MockService(&manager_);
    service->set_mock_connection(underlying_connection);
    return service;
  }

  std::string interface_name_;
  RpcIdentifier ipconfig_rpc_identifier_;
  MockVPNDriver* driver_;  // Owned by |service_|.
  MockControl control_;
  MockMetrics metrics_;
  MockManager manager_;
  MockDeviceInfo device_info_;
  scoped_refptr<NiceMock<MockConnection>> connection_;
  VPNServiceRefPtr service_;
};

TEST_F(VPNServiceTest, LogName) {
  EXPECT_EQ("vpn_l2tpipsec_0", service_->log_name());
}

TEST_F(VPNServiceTest, Connect) {
  EXPECT_TRUE(service_->connectable());
  Error error;
  EXPECT_CALL(*driver_, Connect(_, &error));
  service_->Connect(&error, "in test");
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, ConnectAlreadyConnected) {
  Error error;
  EXPECT_CALL(*driver_, Connect(_, _)).Times(0);
  SetServiceState(Service::kStateOnline);
  service_->Connect(&error, "in test");
  EXPECT_EQ(Error::kAlreadyConnected, error.type());
  error.Reset();
  SetServiceState(Service::kStateConfiguring);
  service_->Connect(&error, "in test");
  EXPECT_EQ(Error::kInProgress, error.type());
}

TEST_F(VPNServiceTest, Disconnect) {
  Error error;
  service_->SetState(Service::kStateConnected);
  EXPECT_CALL(*driver_, Disconnect());
  service_->Disconnect(&error, "in test");
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, CreateStorageIdentifierNoHost) {
  KeyValueStore args;
  Error error;
  args.Set<string>(kNameProperty, "vpn-name");
  EXPECT_EQ("", VPNService::CreateStorageIdentifier(args, &error));
  EXPECT_EQ(Error::kInvalidProperty, error.type());
}

TEST_F(VPNServiceTest, CreateStorageIdentifierNoName) {
  KeyValueStore args;
  Error error;
  args.Set<string>(kProviderHostProperty, "10.8.0.1");
  EXPECT_EQ("", VPNService::CreateStorageIdentifier(args, &error));
  EXPECT_EQ(Error::kNotSupported, error.type());
}

TEST_F(VPNServiceTest, CreateStorageIdentifier) {
  KeyValueStore args;
  Error error;
  args.Set<string>(kNameProperty, "vpn-name");
  args.Set<string>(kProviderHostProperty, "10.8.0.1");
  EXPECT_EQ("vpn_10_8_0_1_vpn_name",
            VPNService::CreateStorageIdentifier(args, &error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(VPNServiceTest, GetStorageIdentifier) {
  EXPECT_EQ("", service_->GetStorageIdentifier());
  service_->set_storage_id("foo");
  EXPECT_EQ("foo", service_->GetStorageIdentifier());
}

TEST_F(VPNServiceTest, IsAlwaysOnVpn) {
  const string kPackage = "com.foo.vpn";
  const string kOtherPackage = "com.bar.vpn";
  EXPECT_FALSE(service_->IsAlwaysOnVpn(kPackage));

  EXPECT_CALL(*driver_, GetHost()).WillRepeatedly(Return(kPackage));
  EXPECT_FALSE(service_->IsAlwaysOnVpn(kPackage));

  EXPECT_CALL(*driver_, GetProviderType())
      .WillRepeatedly(Return(kProviderArcVpn));
  EXPECT_TRUE(service_->IsAlwaysOnVpn(kPackage));
  EXPECT_FALSE(service_->IsAlwaysOnVpn(kOtherPackage));
}

TEST_F(VPNServiceTest, GetDeviceRpcId) {
  Error error;
  EXPECT_EQ(RpcIdentifier("/"), service_->GetDeviceRpcId(&error));
  EXPECT_EQ(Error::kNotSupported, error.type());
}

TEST_F(VPNServiceTest, Load) {
  FakeStore storage;
  static const char kStorageID[] = "storage-id";
  service_->set_storage_id(kStorageID);
  storage.SetString(kStorageID, Service::kStorageType, kTypeVPN);
  EXPECT_CALL(*driver_, Load(&storage, kStorageID)).WillOnce(Return(true));
  EXPECT_TRUE(service_->Load(&storage));
}

TEST_F(VPNServiceTest, Save) {
  FakeStore storage;
  static const char kStorageID[] = "storage-id";
  service_->set_storage_id(kStorageID);
  EXPECT_CALL(*driver_, Save(&storage, kStorageID, false))
      .WillOnce(Return(true));
  EXPECT_TRUE(service_->Save(&storage));
  std::string type;
  EXPECT_TRUE(storage.GetString(kStorageID, Service::kStorageType, &type));
  EXPECT_EQ(type, kTypeVPN);
}

TEST_F(VPNServiceTest, SaveCredentials) {
  FakeStore storage;
  static const char kStorageID[] = "storage-id";
  service_->set_storage_id(kStorageID);
  service_->set_save_credentials(true);
  EXPECT_CALL(*driver_, Save(&storage, kStorageID, true))
      .WillOnce(Return(true));
  EXPECT_TRUE(service_->Save(&storage));
}

TEST_F(VPNServiceTest, Unload) {
  service_->SetAutoConnect(true);
  service_->set_save_credentials(true);
  service_->SetState(Service::kStateConnected);
  EXPECT_CALL(*driver_, Disconnect());
  EXPECT_CALL(*driver_, UnloadCredentials());
  MockVPNProvider* provider = new MockVPNProvider;
  SetVPNProvider(provider);
  provider->services_.push_back(service_);
  service_->Unload();
  EXPECT_FALSE(service_->auto_connect());
  EXPECT_FALSE(service_->save_credentials());
  EXPECT_TRUE(provider->services_.empty());
}

TEST_F(VPNServiceTest, InitPropertyStore) {
  EXPECT_CALL(*driver_, InitPropertyStore(service_->mutable_store()));
  service_->InitDriverPropertyStore();
}

TEST_F(VPNServiceTest, EnableAndRetainAutoConnect) {
  EXPECT_FALSE(service_->retain_auto_connect());
  EXPECT_FALSE(service_->auto_connect());
  service_->EnableAndRetainAutoConnect();
  EXPECT_TRUE(service_->retain_auto_connect());
  EXPECT_FALSE(service_->auto_connect());
}

TEST_F(VPNServiceTest, IsAutoConnectableOffline) {
  EXPECT_TRUE(service_->connectable());
  const char* reason = nullptr;
  EXPECT_CALL(manager_, IsConnected()).WillOnce(Return(false));
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(GetAutoConnOffline(), reason);
}

TEST_F(VPNServiceTest, IsAutoConnectableNeverConnected) {
  EXPECT_TRUE(service_->connectable());
  EXPECT_FALSE(service_->has_ever_connected());
  const char* reason = nullptr;
  EXPECT_CALL(manager_, IsConnected()).WillOnce(Return(true));
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(GetAutoConnNeverConnected(), reason);
}

TEST_F(VPNServiceTest, IsAutoConnectableVPNAlreadyActive) {
  EXPECT_TRUE(service_->connectable());
  SetHasEverConnected(true);
  EXPECT_CALL(manager_, IsConnected()).WillOnce(Return(true));
  MockVPNProvider* provider = new MockVPNProvider;
  SetVPNProvider(provider);
  EXPECT_CALL(*provider, HasActiveService()).WillOnce(Return(true));
  const char* reason = nullptr;
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(GetAutoConnVPNAlreadyActive(), reason);
}

TEST_F(VPNServiceTest, IsAutoConnectableNotConnectable) {
  const char* reason = nullptr;
  SetConnectable(false);
  EXPECT_FALSE(IsAutoConnectable(&reason));
}

TEST_F(VPNServiceTest, IsAutoConnectable) {
  EXPECT_TRUE(service_->connectable());
  SetHasEverConnected(true);
  EXPECT_CALL(manager_, IsConnected()).WillOnce(Return(true));
  MockVPNProvider* provider = new MockVPNProvider;
  SetVPNProvider(provider);
  EXPECT_CALL(*provider, HasActiveService()).WillOnce(Return(false));
  const char* reason = nullptr;
  EXPECT_TRUE(IsAutoConnectable(&reason));
  EXPECT_FALSE(reason);
}

TEST_F(VPNServiceTest, SetNamePropertyTrivial) {
  Error error;
  // A null change returns false, but with error set to success.
  EXPECT_FALSE(service_->mutable_store()->SetAnyProperty(
      kNameProperty, brillo::Any(service_->friendly_name()), &error));
  EXPECT_FALSE(error.IsFailure());
}

TEST_F(VPNServiceTest, SetNameProperty) {
  const string kHost = "1.2.3.4";
  driver_->args()->Set<string>(kProviderHostProperty, kHost);
  string kOldId = service_->GetStorageIdentifier();
  Error error;
  const string kName = "New Name";
  scoped_refptr<MockProfile> profile(new MockProfile(&manager_));
  EXPECT_CALL(*profile, DeleteEntry(kOldId, _));
  EXPECT_CALL(*profile, UpdateService(_));
  service_->set_profile(profile);
  EXPECT_TRUE(service_->mutable_store()->SetAnyProperty(
      kNameProperty, brillo::Any(kName), &error));
  EXPECT_NE(service_->GetStorageIdentifier(), kOldId);
  EXPECT_EQ(kName, service_->friendly_name());
}

TEST_F(VPNServiceTest, PropertyChanges) {
  TestCommonPropertyChanges(service_, GetAdaptor());
  TestAutoConnectPropertyChange(service_, GetAdaptor());

  const string kHost = "1.2.3.4";
  scoped_refptr<MockProfile> profile(new NiceMock<MockProfile>(&manager_));
  service_->set_profile(profile);
  driver_->args()->Set<string>(kProviderHostProperty, kHost);
  TestNamePropertyChange(service_, GetAdaptor());
}

// Custom property setters should return false, and make no changes, if
// the new value is the same as the old value.
TEST_F(VPNServiceTest, CustomSetterNoopChange) {
  TestCustomSetterNoopChange(service_, &manager_);
}

TEST_F(VPNServiceTest, GetPhysicalTechnologyPropertyFailsIfNoCarrier) {
  service_->SetConnection(connection_);
  EXPECT_EQ(connection_, service_->connection());

  // Simulate an error by causing GetPrimaryPhysicalService() to return nullptr.
  EXPECT_CALL(manager_, GetPrimaryPhysicalService()).WillOnce(Return(nullptr));

  Error error;
  EXPECT_EQ("", service_->GetPhysicalTechnologyProperty(&error));
  EXPECT_EQ(Error::kOperationFailed, error.type());
}

TEST_F(VPNServiceTest, GetPhysicalTechnologyPropertyOverWifi) {
  EXPECT_CALL(*connection_, technology()).Times(0);
  service_->SetConnection(connection_);
  EXPECT_EQ(connection_, service_->connection());

  scoped_refptr<NiceMock<MockConnection>> lower_connection =
      new NiceMock<MockConnection>(&device_info_);
  EXPECT_CALL(manager_, GetPrimaryPhysicalService())
      .WillOnce(Return(ByMove(CreateUnderlyingService(lower_connection))));

  // Set the type of the lower connection to "wifi" and expect that type to be
  // returned by GetPhysicalTechnologyProperty().
  EXPECT_CALL(*lower_connection, technology())
      .WillOnce(Return(Technology::kWifi));

  Error error;
  EXPECT_EQ(kTypeWifi, service_->GetPhysicalTechnologyProperty(&error));
  EXPECT_TRUE(error.IsSuccess());

  // Clear expectations now, so the Return(lower_connection) action releases
  // the reference to |lower_connection| allowing it to be destroyed now.
  Mock::VerifyAndClearExpectations(connection_.get());
  // Destroying the |lower_connection| at function exit will also call an extra
  // FlushAddresses on the |device_info_| object.
  EXPECT_CALL(device_info_, FlushAddresses(0));
}

TEST_F(VPNServiceTest, GetTethering) {
  service_->SetConnection(connection_);
  EXPECT_EQ(connection_, service_->connection());

  // Simulate an error by causing GetPrimaryPhysicalService() to return nullptr.
  EXPECT_CALL(manager_, GetPrimaryPhysicalService()).WillOnce(Return(nullptr));

  {
    Error error;
    EXPECT_EQ("", service_->GetTethering(&error));
    EXPECT_EQ(Error::kOperationFailed, error.type());
  }

  scoped_refptr<NiceMock<MockConnection>> lower_connection =
      new NiceMock<MockConnection>(&device_info_);

  EXPECT_CALL(*connection_, tethering()).Times(0);

  const char kTethering[] = "moon unit";
  EXPECT_CALL(*lower_connection, tethering())
      .WillOnce(ReturnRefOfCopy(string(kTethering)))
      .WillOnce(ReturnRefOfCopy(string()));

  {
    EXPECT_CALL(manager_, GetPrimaryPhysicalService())
        .WillOnce(Return(ByMove(CreateUnderlyingService(lower_connection))));
    Error error;
    EXPECT_EQ(kTethering, service_->GetTethering(&error));
    EXPECT_TRUE(error.IsSuccess());
  }
  {
    EXPECT_CALL(manager_, GetPrimaryPhysicalService())
        .WillOnce(Return(ByMove(CreateUnderlyingService(lower_connection))));
    Error error;
    EXPECT_EQ("", service_->GetTethering(&error));
    EXPECT_EQ(Error::kNotSupported, error.type());
  }

  // Clear expectations now, so the Return(lower_connection) action releases
  // the reference to |lower_connection| allowing it to be destroyed now.
  Mock::VerifyAndClearExpectations(connection_.get());
  // Destroying the |lower_connection| at function exit will also call an extra
  // FlushAddresses on the |device_info_| object.
  EXPECT_CALL(device_info_, FlushAddresses(0));
}

// Test that adding/removing VM allowed interfaces to/from the VPN provider
// list triggers calls to the active VPN connection to update the routing table.
TEST_F(VPNServiceTest, AddRemoveVMInterface) {
  const std::string kTestVMTapInterfaceName = "vmtap0";
  auto provider = std::make_unique<MockVPNProvider>();
  provider->services_.push_back(service_);

  service_->SetConnection(connection_);
  SetServiceState(Service::kStateOnline);

  EXPECT_CALL(*connection_,
              AddInputInterfaceToRoutingTable(kTestVMTapInterfaceName));
  provider->AddAllowedInterface(kTestVMTapInterfaceName);
  EXPECT_EQ(1, provider->allowed_iifs().size());

  EXPECT_CALL(*connection_,
              RemoveInputInterfaceFromRoutingTable(kTestVMTapInterfaceName));
  provider->RemoveAllowedInterface(kTestVMTapInterfaceName);
  EXPECT_EQ(0, provider->allowed_iifs().size());
}

}  // namespace shill
