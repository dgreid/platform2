// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_service.h"

#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "shill/cellular/cellular_capability.h"
#include "shill/cellular/mock_cellular.h"
#include "shill/cellular/mock_mobile_operator_info.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/mock_store.h"
#include "shill/service_property_change_test.h"

using std::string;
using testing::_;
using testing::AnyNumber;
using testing::InSequence;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;
using testing::StrEq;

namespace shill {

namespace {
const char kImsi[] = "111222123456789";
const char kIccid[] = "1234567890000";
}  // namespace

MATCHER_P2(ContainsCellularProperties, key, value, "") {
  return arg.template Contains<string>(CellularService::kStorageType) &&
         arg.template Get<string>(CellularService::kStorageType) ==
             kTypeCellular &&
         arg.template Contains<string>(key) &&
         arg.template Get<string>(key) == value;
}

class CellularServiceTest : public testing::Test {
 public:
  CellularServiceTest()
      : modem_info_(nullptr, &dispatcher_, nullptr, nullptr),
        adaptor_(nullptr) {
    Service::SetNextSerialNumberForTesting(0);
    // Many tests set service properties which call Manager.UpdateService().
    EXPECT_CALL(*modem_info_.mock_manager(), UpdateService(_))
        .Times(AnyNumber());
    device_ = new MockCellular(&modem_info_, "usb0", kAddress, 3,
                               Cellular::kTypeCdma, "", RpcIdentifier(""));
    // CellularService expects an IMSI and SIM ID be set in the Device.
    device_->set_imsi(kImsi);
    device_->set_iccid(kIccid);
    service_ = new CellularService(modem_info_.manager(), kImsi, kIccid,
                                   device_->GetSimCardId());
    service_->SetDevice(device_.get());
  }
  ~CellularServiceTest() override { adaptor_ = nullptr; }

  void SetUp() override {
    adaptor_ = static_cast<ServiceMockAdaptor*>(service_->adaptor());
  }

 protected:
  static const char kAddress[];

  string GetFriendlyName() const { return service_->friendly_name(); }
  bool IsAutoConnectable(const char** reason) const {
    return service_->IsAutoConnectable(reason);
  }
  bool SetAutoConnectFull(bool connect) {
    return service_->SetAutoConnectFull(connect, /*error=*/nullptr);
  }

  EventDispatcher dispatcher_;
  MockModemInfo modem_info_;
  scoped_refptr<MockCellular> device_;
  CellularServiceRefPtr service_;
  ServiceMockAdaptor* adaptor_;  // Owned by |service_|.
};

const char CellularServiceTest::kAddress[] = "000102030405";

TEST_F(CellularServiceTest, Constructor) {
  EXPECT_TRUE(service_->connectable());
}

TEST_F(CellularServiceTest, SetNetworkTechnology) {
  EXPECT_CALL(*adaptor_, EmitStringChanged(kNetworkTechnologyProperty,
                                           kNetworkTechnologyUmts));
  EXPECT_TRUE(service_->network_technology().empty());
  service_->SetNetworkTechnology(kNetworkTechnologyUmts);
  EXPECT_EQ(kNetworkTechnologyUmts, service_->network_technology());
  service_->SetNetworkTechnology(kNetworkTechnologyUmts);
}

TEST_F(CellularServiceTest, LogName) {
  EXPECT_EQ("cellular_0", service_->log_name());
  service_->SetNetworkTechnology(kNetworkTechnologyUmts);
  EXPECT_EQ("cellular_UMTS_0", service_->log_name());
  service_->SetNetworkTechnology(kNetworkTechnologyGsm);
  EXPECT_EQ("cellular_GSM_0", service_->log_name());
  service_->SetNetworkTechnology(kNetworkTechnologyLte);
  EXPECT_EQ("cellular_LTE_0", service_->log_name());
}

TEST_F(CellularServiceTest, SetRoamingState) {
  EXPECT_CALL(*adaptor_,
              EmitStringChanged(kRoamingStateProperty, kRoamingStateHome));
  EXPECT_TRUE(service_->roaming_state().empty());
  service_->SetRoamingState(kRoamingStateHome);
  EXPECT_EQ(kRoamingStateHome, service_->roaming_state());
  service_->SetRoamingState(kRoamingStateHome);
}

TEST_F(CellularServiceTest, SetServingOperator) {
  static const char kCode[] = "123456";
  static const char kName[] = "Some Cellular Operator";
  Stringmap test_operator;
  service_->SetServingOperator(test_operator);
  test_operator[kOperatorCodeKey] = kCode;
  test_operator[kOperatorNameKey] = kName;
  EXPECT_CALL(*adaptor_, EmitStringmapChanged(kServingOperatorProperty, _));
  service_->SetServingOperator(test_operator);
  const Stringmap& serving_operator = service_->serving_operator();
  ASSERT_NE(serving_operator.end(), serving_operator.find(kOperatorCodeKey));
  ASSERT_NE(serving_operator.end(), serving_operator.find(kOperatorNameKey));
  EXPECT_EQ(kCode, serving_operator.find(kOperatorCodeKey)->second);
  EXPECT_EQ(kName, serving_operator.find(kOperatorNameKey)->second);
  Mock::VerifyAndClearExpectations(adaptor_);
  EXPECT_CALL(*adaptor_, EmitStringmapChanged(kServingOperatorProperty, _))
      .Times(0);
  service_->SetServingOperator(serving_operator);
}

TEST_F(CellularServiceTest, SetOLP) {
  const char kMethod[] = "GET";
  const char kURL[] = "payment.url";
  const char kPostData[] = "post_man";
  Stringmap olp;

  service_->SetOLP("", "", "");
  olp = service_->olp();  // Copy to simplify assertions below.
  EXPECT_EQ("", olp[kPaymentPortalURL]);
  EXPECT_EQ("", olp[kPaymentPortalMethod]);
  EXPECT_EQ("", olp[kPaymentPortalPostData]);

  EXPECT_CALL(*adaptor_, EmitStringmapChanged(kPaymentPortalProperty, _));
  service_->SetOLP(kURL, kMethod, kPostData);
  olp = service_->olp();  // Copy to simplify assertions below.
  EXPECT_EQ(kURL, olp[kPaymentPortalURL]);
  EXPECT_EQ(kMethod, olp[kPaymentPortalMethod]);
  EXPECT_EQ(kPostData, olp[kPaymentPortalPostData]);
}

TEST_F(CellularServiceTest, SetUsageURL) {
  static const char kUsageURL[] = "usage.url";
  EXPECT_CALL(*adaptor_, EmitStringChanged(kUsageURLProperty, kUsageURL));
  EXPECT_TRUE(service_->usage_url().empty());
  service_->SetUsageURL(kUsageURL);
  EXPECT_EQ(kUsageURL, service_->usage_url());
  service_->SetUsageURL(kUsageURL);
}

TEST_F(CellularServiceTest, SetApn) {
  static const char kApn[] = "TheAPN";
  static const char kUsername[] = "commander.data";
  ProfileRefPtr profile(new NiceMock<MockProfile>(modem_info_.manager()));
  service_->set_profile(profile);
  Error error;
  Stringmap testapn;
  testapn[kApnProperty] = kApn;
  testapn[kApnUsernameProperty] = kUsername;
  EXPECT_CALL(*adaptor_, EmitStringmapChanged(kCellularApnProperty, _));
  service_->SetApn(testapn, &error);
  EXPECT_TRUE(error.IsSuccess());
  Stringmap resultapn = service_->GetApn(&error);
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(2, resultapn.size());
  Stringmap::const_iterator it = resultapn.find(kApnProperty);
  EXPECT_TRUE(it != resultapn.end() && it->second == kApn);
  it = resultapn.find(kApnUsernameProperty);
  EXPECT_TRUE(it != resultapn.end() && it->second == kUsername);
  EXPECT_NE(nullptr, service_->GetUserSpecifiedApn());
}

TEST_F(CellularServiceTest, ClearApn) {
  static const char kApn[] = "TheAPN";
  static const char kUsername[] = "commander.data";
  ProfileRefPtr profile(new NiceMock<MockProfile>(modem_info_.manager()));
  service_->set_profile(profile);
  Error error;
  // Set up an APN to make sure that it later gets cleared.
  Stringmap testapn;
  testapn[kApnProperty] = kApn;
  testapn[kApnUsernameProperty] = kUsername;
  EXPECT_CALL(*adaptor_, EmitStringmapChanged(kCellularApnProperty, _));
  service_->SetApn(testapn, &error);
  Stringmap resultapn = service_->GetApn(&error);
  ASSERT_TRUE(error.IsSuccess());
  ASSERT_EQ(2, service_->GetApn(&error).size());

  Stringmap emptyapn;
  EXPECT_CALL(*adaptor_, EmitStringmapChanged(kCellularLastGoodApnProperty, _))
      .Times(0);
  EXPECT_CALL(*adaptor_, EmitStringmapChanged(kCellularApnProperty, _))
      .Times(1);
  service_->SetApn(emptyapn, &error);
  EXPECT_TRUE(error.IsSuccess());
  resultapn = service_->GetApn(&error);
  EXPECT_TRUE(resultapn.empty());
  EXPECT_EQ(nullptr, service_->GetUserSpecifiedApn());
}

TEST_F(CellularServiceTest, LastGoodApn) {
  static const char kApn[] = "TheAPN";
  static const char kUsername[] = "commander.data";
  ProfileRefPtr profile(new NiceMock<MockProfile>(modem_info_.manager()));
  service_->set_profile(profile);
  Stringmap testapn;
  testapn[kApnProperty] = kApn;
  testapn[kApnUsernameProperty] = kUsername;
  EXPECT_CALL(*adaptor_, EmitStringmapChanged(kCellularLastGoodApnProperty, _));
  service_->SetLastGoodApn(testapn);
  Stringmap* resultapn = service_->GetLastGoodApn();
  ASSERT_NE(nullptr, resultapn);
  EXPECT_EQ(2, resultapn->size());
  EXPECT_EQ(kApn, (*resultapn)[kApnProperty]);
  EXPECT_EQ(kUsername, (*resultapn)[kApnUsernameProperty]);

  // Now set the user-specified APN, and check that LastGoodApn is preserved.
  Stringmap userapn;
  userapn[kApnProperty] = kApn;
  userapn[kApnUsernameProperty] = kUsername;
  EXPECT_CALL(*adaptor_, EmitStringmapChanged(kCellularApnProperty, _));
  Error error;
  service_->SetApn(userapn, &error);

  ASSERT_NE(nullptr, service_->GetLastGoodApn());
  EXPECT_EQ(2, resultapn->size());
  EXPECT_EQ(kApn, (*resultapn)[kApnProperty]);
  EXPECT_EQ(kUsername, (*resultapn)[kApnUsernameProperty]);
}

TEST_F(CellularServiceTest, IsAutoConnectable) {
  // This test assumes AutoConnect is not disabled by policy.
  EXPECT_CALL(*modem_info_.mock_manager(), IsTechnologyAutoConnectDisabled(_))
      .WillRepeatedly(Return(false));

  const char* reason = nullptr;

  // Auto-connect should be suppressed if the device is not running.
  device_->running_ = false;
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(CellularService::kAutoConnDeviceDisabled, reason);

  device_->running_ = true;

  // If we're in a process of activation, don't auto-connect.
  EXPECT_CALL(*modem_info_.mock_pending_activation_store(),
              GetActivationState(_, _))
      .WillOnce(Return(PendingActivationStore::kStatePending));
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(CellularService::kAutoConnActivating, reason);
  EXPECT_CALL(*modem_info_.mock_pending_activation_store(),
              GetActivationState(_, _))
      .WillRepeatedly(Return(PendingActivationStore::kStateActivated));

  // Auto-connect should be suppressed if we're out of credits.
  service_->NotifySubscriptionStateChanged(SubscriptionState::kOutOfCredits);
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(CellularService::kAutoConnOutOfCredits, reason);
  service_->NotifySubscriptionStateChanged(SubscriptionState::kProvisioned);

  // A PPP authentication failure means the Service is not auto-connectable.
  service_->SetFailure(Service::kFailurePPPAuth);
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(CellularService::kAutoConnBadPPPCredentials, reason);

  // Reset failure state, to make the Service auto-connectable again.
  service_->SetState(Service::kStateIdle);
  EXPECT_TRUE(IsAutoConnectable(&reason));

  // The following test cases are copied from ServiceTest.IsAutoConnectable

  service_->SetConnectable(true);
  EXPECT_TRUE(IsAutoConnectable(&reason));

  // We should not auto-connect to a Service that a user has
  // deliberately disconnected.
  Error error;
  service_->UserInitiatedDisconnect("RPC", &error);
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnExplicitDisconnect, reason);

  // But if the Service is reloaded, it is eligible for auto-connect
  // again.
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, ContainsGroup(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(storage, GetString(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(storage, GetGroupsWithProperties(ContainsCellularProperties(
                           CellularService::kStorageImsi, device_->imsi())))
      .WillRepeatedly(Return(std::set<string>{"matching-storage-id"}));
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_TRUE(IsAutoConnectable(&reason));

  // A non-user initiated Disconnect doesn't change anything.
  service_->Disconnect(&error, "in test");
  EXPECT_TRUE(IsAutoConnectable(&reason));

  // A resume also re-enables auto-connect.
  service_->UserInitiatedDisconnect("RPC", &error);
  EXPECT_FALSE(IsAutoConnectable(&reason));
  service_->OnAfterResume();
  EXPECT_TRUE(IsAutoConnectable(&reason));

  service_->SetState(Service::kStateConnected);
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnConnected, reason);

  service_->SetState(Service::kStateAssociating);
  EXPECT_FALSE(IsAutoConnectable(&reason));
  EXPECT_STREQ(Service::kAutoConnConnecting, reason);
}

TEST_F(CellularServiceTest, LoadResetsPPPAuthFailure) {
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, ContainsGroup(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(storage, GetString(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(storage, GetGroupsWithProperties(ContainsCellularProperties(
                           CellularService::kStorageImsi, device_->imsi())))
      .WillRepeatedly(Return(std::set<string>{"matching-storage-id"}));

  const string kDefaultUser;
  const string kDefaultPass;
  const string kNewUser("new-username");
  const string kNewPass("new-password");
  for (const auto change_username : {false, true}) {
    for (const auto change_password : {false, true}) {
      service_->ppp_username_ = kDefaultUser;
      service_->ppp_password_ = kDefaultPass;
      service_->SetFailure(Service::kFailurePPPAuth);
      EXPECT_TRUE(service_->IsFailed());
      EXPECT_EQ(Service::kFailurePPPAuth, service_->failure());
      if (change_username) {
        EXPECT_CALL(storage,
                    GetString(_, CellularService::kStoragePPPUsername, _))
            .WillOnce(DoAll(SetArgPointee<2>(kNewUser), Return(true)))
            .RetiresOnSaturation();
      }
      if (change_password) {
        EXPECT_CALL(storage,
                    GetString(_, CellularService::kStoragePPPPassword, _))
            .WillOnce(DoAll(SetArgPointee<2>(kNewPass), Return(true)))
            .RetiresOnSaturation();
      }
      EXPECT_TRUE(service_->Load(&storage));
      if (change_username || change_password) {
        EXPECT_NE(Service::kFailurePPPAuth, service_->failure());
      } else {
        EXPECT_EQ(Service::kFailurePPPAuth, service_->failure());
      }
    }
  }
}

TEST_F(CellularServiceTest, LoadFromProfileMatchingImsi) {
  NiceMock<MockStore> storage;
  string initial_storage_id = service_->GetStorageIdentifier();
  string matching_storage_id = "another-storage-id";
  std::set<string> groups = {matching_storage_id};
  EXPECT_CALL(storage, ContainsGroup(initial_storage_id)).Times(0);
  EXPECT_CALL(storage, ContainsGroup(matching_storage_id))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, GetGroupsWithProperties(ContainsCellularProperties(
                           CellularService::kStorageImsi, device_->imsi())))
      .WillRepeatedly(Return(groups));
  EXPECT_CALL(storage, GetString(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_TRUE(service_->IsLoadableFrom(storage));
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_EQ(matching_storage_id, service_->GetStorageIdentifier());
}

TEST_F(CellularServiceTest, LoadFromFirstOfMultipleMatchingProfiles) {
  NiceMock<MockStore> storage;
  string initial_storage_id = service_->GetStorageIdentifier();
  string matching_storage_id1 = "another-storage-id1";
  string matching_storage_id2 = "another-storage-id2";
  string matching_storage_id3 = "another-storage-id3";
  std::set<string> groups = {
      matching_storage_id1,
      matching_storage_id2,
      matching_storage_id3,
  };
  EXPECT_CALL(storage, ContainsGroup(initial_storage_id)).Times(0);
  EXPECT_CALL(storage, ContainsGroup(matching_storage_id1))
      .WillOnce(Return(true));
  EXPECT_CALL(storage, GetGroupsWithProperties(ContainsCellularProperties(
                           CellularService::kStorageImsi, device_->imsi())))
      .WillRepeatedly(Return(groups));
  EXPECT_CALL(storage, GetString(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_TRUE(service_->IsLoadableFrom(storage));
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_EQ(matching_storage_id1, service_->GetStorageIdentifier());
}

// The default storage_identifier_ will be {kCellular}_{kImsi}, however older
// profile/storage entries may use a different identifier. This sets up an entry
// with a matching IMSI but an arbitrary storage id and ensures that the older
// storage_identifer_ value is set.
TEST_F(CellularServiceTest, LoadFromOlderProfile) {
  NiceMock<MockStore> storage;
  string storage_id = "arbitrary-storage-id";
  std::set<string> groups = {storage_id};

  EXPECT_CALL(storage, GetGroupsWithProperties(ContainsCellularProperties(
                           CellularService::kStorageImsi, kImsi)))
      .WillRepeatedly(Return(groups));
  EXPECT_CALL(storage, ContainsGroup(storage_id)).WillRepeatedly(Return(true));

  EXPECT_TRUE(service_->IsLoadableFrom(storage));
  EXPECT_TRUE(service_->Load(&storage));
  EXPECT_EQ(storage_id, service_->GetStorageIdentifier());
}

TEST_F(CellularServiceTest, Save) {
  NiceMock<MockStore> storage;
  EXPECT_CALL(storage, SetString(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(storage, SetString(service_->GetStorageIdentifier(),
                                 StrEq(Service::kStorageType), kTypeCellular))
      .Times(1);
  EXPECT_CALL(storage,
              SetString(service_->GetStorageIdentifier(),
                        StrEq(CellularService::kStorageImsi), device_->imsi()))
      .Times(1);
  EXPECT_CALL(storage, SetString(service_->GetStorageIdentifier(),
                                 StrEq(CellularService::kStorageIccid),
                                 device_->iccid()))
      .Times(1);
  EXPECT_TRUE(service_->Save(&storage));
}

// Some of these tests duplicate signals tested above. However, it's
// convenient to have all the property change notifications documented
// (and tested) in one place.
TEST_F(CellularServiceTest, PropertyChanges) {
  TestCommonPropertyChanges(service_, adaptor_);
  TestAutoConnectPropertyChange(service_, adaptor_);

  EXPECT_CALL(*adaptor_, EmitStringChanged(kActivationTypeProperty, _));
  service_->SetActivationType(CellularService::kActivationTypeOTA);
  Mock::VerifyAndClearExpectations(adaptor_);

  EXPECT_NE(kActivationStateNotActivated, service_->activation_state());
  EXPECT_CALL(*adaptor_, EmitStringChanged(kActivationStateProperty, _));
  service_->SetActivationState(kActivationStateNotActivated);
  Mock::VerifyAndClearExpectations(adaptor_);

  string network_technology = service_->network_technology();
  EXPECT_CALL(*adaptor_, EmitStringChanged(kNetworkTechnologyProperty, _));
  service_->SetNetworkTechnology(network_technology + "and some new stuff");
  Mock::VerifyAndClearExpectations(adaptor_);

  EXPECT_CALL(*adaptor_, EmitBoolChanged(kOutOfCreditsProperty, true));
  service_->NotifySubscriptionStateChanged(SubscriptionState::kOutOfCredits);
  Mock::VerifyAndClearExpectations(adaptor_);
  EXPECT_CALL(*adaptor_, EmitBoolChanged(kOutOfCreditsProperty, false));
  service_->NotifySubscriptionStateChanged(SubscriptionState::kProvisioned);
  Mock::VerifyAndClearExpectations(adaptor_);

  string roaming_state = service_->roaming_state();
  EXPECT_CALL(*adaptor_, EmitStringChanged(kRoamingStateProperty, _));
  service_->SetRoamingState(roaming_state + "and some new stuff");
  Mock::VerifyAndClearExpectations(adaptor_);
}

// Custom property setters should return false, and make no changes, if
// the new value is the same as the old value.
TEST_F(CellularServiceTest, CustomSetterNoopChange) {
  // Test that we didn't break any setters provided by the base class.
  TestCustomSetterNoopChange(service_, modem_info_.mock_manager());

  // Test the new setter we added.
  // First set up our environment...
  static const char kApn[] = "TheAPN";
  static const char kUsername[] = "commander.data";
  Error error;
  Stringmap testapn;
  ProfileRefPtr profile(new NiceMock<MockProfile>(nullptr));
  service_->set_profile(profile);
  testapn[kApnProperty] = kApn;
  testapn[kApnUsernameProperty] = kUsername;
  // ... then set to a known value ...
  EXPECT_TRUE(service_->SetApn(testapn, &error));
  EXPECT_TRUE(error.IsSuccess());
  // ... then set to same value.
  EXPECT_FALSE(service_->SetApn(testapn, &error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(CellularServiceTest, IsMeteredByDefault) {
  // These services should be metered by default.
  EXPECT_TRUE(service_->IsMetered());
}

TEST_F(CellularServiceTest, SetActivationState) {
  // SetActivationState should emit a change.
  EXPECT_CALL(*adaptor_, EmitStringChanged(kActivationStateProperty,
                                           kActivationStateNotActivated));
  service_->SetActivationState(kActivationStateNotActivated);
  EXPECT_EQ(service_->activation_state(), kActivationStateNotActivated);
  EXPECT_CALL(*adaptor_, EmitStringChanged(kActivationStateProperty, _))
      .Times(AnyNumber());

  // Setting the activation state to activated should also set AutoConnect.
  EXPECT_FALSE(service_->auto_connect());
  service_->SetActivationState(kActivationStateActivated);
  EXPECT_EQ(service_->activation_state(), kActivationStateActivated);
  EXPECT_TRUE(service_->auto_connect());

  // After a client sets AutoConnect to false, setting the activation state to
  // activated should not set AutoConnect.
  SetAutoConnectFull(false);
  EXPECT_FALSE(service_->auto_connect());
  service_->SetActivationState(kActivationStateNotActivated);
  EXPECT_EQ(service_->activation_state(), kActivationStateNotActivated);
  EXPECT_FALSE(service_->auto_connect());
  service_->SetActivationState(kActivationStateActivated);
  EXPECT_EQ(service_->activation_state(), kActivationStateActivated);
  EXPECT_FALSE(service_->auto_connect());
}

}  // namespace shill
