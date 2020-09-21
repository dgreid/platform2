// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_provider.h"

#include <set>
#include <string>
#include <vector>

#include <base/format_macros.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "shill/fake_store.h"
#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/net/ieee80211.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/technology.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/mock_wifi_service.h"
#include "shill/wifi/wifi_endpoint.h"

using base::StringPrintf;
using std::set;
using std::string;
using std::vector;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StartsWith;
using ::testing::StrictMock;

namespace shill {

class WiFiProviderTest : public testing::Test {
 public:
  WiFiProviderTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        provider_(&manager_),
        default_profile_(new NiceMock<MockProfile>(&manager_, "default")),
        user_profile_(new NiceMock<MockProfile>(&manager_, "user")),
        storage_entry_index_(0) {}

  ~WiFiProviderTest() override = default;

  void SetUp() override {
    EXPECT_CALL(*default_profile_, IsDefault()).WillRepeatedly(Return(true));
    EXPECT_CALL(*default_profile_, GetStorage())
        .WillRepeatedly(Return(&default_profile_storage_));
    EXPECT_CALL(*default_profile_, GetConstStorage())
        .WillRepeatedly(Return(&default_profile_storage_));

    EXPECT_CALL(*user_profile_, IsDefault()).WillRepeatedly(Return(false));
    EXPECT_CALL(*user_profile_, GetStorage())
        .WillRepeatedly(Return(&user_profile_storage_));
    EXPECT_CALL(*user_profile_, GetConstStorage())
        .WillRepeatedly(Return(&user_profile_storage_));

    // Default expectations for UMA metrics. Individual test cases
    // will override these, by adding later expectations.
    EXPECT_CALL(metrics_,
                SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, _,
                          Metrics::kMetricRememberedWiFiNetworkCountMin,
                          Metrics::kMetricRememberedWiFiNetworkCountMax,
                          Metrics::kMetricRememberedWiFiNetworkCountNumBuckets))
        .Times(AnyNumber());
    EXPECT_CALL(
        metrics_,
        SendToUMA(
            StartsWith("Network.Shill.WiFi.RememberedPrivateNetworkCount."), _,
            Metrics::kMetricRememberedWiFiNetworkCountMin,
            Metrics::kMetricRememberedWiFiNetworkCountMax,
            Metrics::kMetricRememberedWiFiNetworkCountNumBuckets))
        .Times(AnyNumber());
    EXPECT_CALL(
        metrics_,
        SendToUMA(
            StartsWith("Network.Shill.WiFi.RememberedSharedNetworkCount."), _,
            Metrics::kMetricRememberedWiFiNetworkCountMin,
            Metrics::kMetricRememberedWiFiNetworkCountMax,
            Metrics::kMetricRememberedWiFiNetworkCountNumBuckets))
        .Times(AnyNumber());
  }

  // Used by mock invocations of RegisterService() to maintain the side-effect
  // of assigning a profile to |service|.
  void BindServiceToDefaultProfile(const ServiceRefPtr& service) {
    service->set_profile(default_profile_);
  }
  void BindServiceToUserProfile(const ServiceRefPtr& service) {
    service->set_profile(user_profile_);
  }

 protected:
  using MockWiFiServiceRefPtr = scoped_refptr<MockWiFiService>;

  void CreateServicesFromProfile(Profile* profile) {
    provider_.CreateServicesFromProfile(profile);
  }

  const vector<WiFiServiceRefPtr> GetServices() { return provider_.services_; }

  const WiFiProvider::EndpointServiceMap& GetServiceByEndpoint() {
    return provider_.service_by_endpoint_;
  }

  bool GetRunning() { return provider_.running_; }

  void AddStringParameterToStorage(FakeStore* storage,
                                   const string& id,
                                   const string& key,
                                   const string& value) {
    storage->SetString(id, key, value);
  }

  // Adds service to profile's storage. But does not set profile on the Service.
  string AddServiceToProfileStorage(Profile* profile,
                                    const char* ssid,
                                    const char* mode,
                                    const char* security_class,
                                    bool is_hidden,
                                    bool provide_hidden) {
    string id = base::StringPrintf("entry_%d", storage_entry_index_);
    auto* profile_storage = static_cast<FakeStore*>(profile->GetStorage());
    AddStringParameterToStorage(profile_storage, id, WiFiService::kStorageType,
                                kTypeWifi);
    if (ssid) {
      const string ssid_string(ssid);
      const string hex_ssid(
          base::HexEncode(ssid_string.data(), ssid_string.size()));
      AddStringParameterToStorage(profile_storage, id,
                                  WiFiService::kStorageSSID, hex_ssid);
    }
    if (mode) {
      AddStringParameterToStorage(profile_storage, id,
                                  WiFiService::kStorageMode, mode);
    }
    if (security_class) {
      AddStringParameterToStorage(profile_storage, id,
                                  WiFiService::kStorageSecurityClass,
                                  security_class);
    }
    if (provide_hidden) {
      profile_storage->SetBool(id, kWifiHiddenSsid, is_hidden);
    } else {
      profile_storage->DeleteKey(id, kWifiHiddenSsid);
    }
    storage_entry_index_++;
    return id;
  }

  void SetServiceParameters(const char* ssid,
                            const char* mode,
                            const char* security_class,
                            bool is_hidden,
                            bool provide_hidden,
                            KeyValueStore* args) {
    args->Set<string>(kTypeProperty, kTypeWifi);
    if (ssid) {
      // TODO(pstew): When Chrome switches to using kWifiHexSsid primarily for
      // GetService and friends, we should switch to doing so here ourselves.
      args->Set<string>(kSSIDProperty, ssid);
    }
    if (mode) {
      args->Set<string>(kModeProperty, mode);
    }
    if (security_class) {
      args->Set<string>(kSecurityClassProperty, security_class);
    }
    if (provide_hidden) {
      args->Set<bool>(kWifiHiddenSsid, is_hidden);
    }
  }

  ServiceRefPtr CreateTemporaryService(const char* ssid,
                                       const char* mode,
                                       const char* security,
                                       bool is_hidden,
                                       bool provide_hidden,
                                       Error* error) {
    KeyValueStore args;
    SetServiceParameters(ssid, mode, security, is_hidden, provide_hidden,
                         &args);
    return provider_.CreateTemporaryService(args, error);
  }

  WiFiServiceRefPtr GetService(const char* ssid,
                               const char* mode,
                               const char* security_class,
                               bool is_hidden,
                               bool provide_hidden,
                               Error* error) {
    KeyValueStore args;
    SetServiceParameters(ssid, mode, security_class, is_hidden, provide_hidden,
                         &args);
    return provider_.GetWiFiService(args, error);
  }

  WiFiServiceRefPtr GetWiFiService(const KeyValueStore& args, Error* error) {
    return provider_.GetWiFiService(args, error);
  }

  WiFiServiceRefPtr FindService(const vector<uint8_t>& ssid,
                                const string& mode,
                                const string& security) {
    return provider_.FindService(ssid, mode, security);
  }
  WiFiEndpointRefPtr MakeOpenEndpoint(const string& ssid,
                                      const string& bssid,
                                      uint16_t frequency,
                                      int16_t signal_dbm) {
    return WiFiEndpoint::MakeOpenEndpoint(
        nullptr, nullptr, ssid, bssid,
        WPASupplicant::kNetworkModeInfrastructure, frequency, signal_dbm);
  }
  WiFiEndpointRefPtr MakeEndpoint(
      const string& ssid,
      const string& bssid,
      uint16_t frequency,
      int16_t signal_dbm,
      const WiFiEndpoint::SecurityFlags& security_flags) {
    return WiFiEndpoint::MakeEndpoint(nullptr, nullptr, ssid, bssid,
                                      WPASupplicant::kNetworkModeInfrastructure,
                                      frequency, signal_dbm, security_flags);
  }
  MockWiFiServiceRefPtr AddMockService(const vector<uint8_t>& ssid,
                                       const string& mode,
                                       const string& security,
                                       bool hidden_ssid) {
    MockWiFiServiceRefPtr service = new MockWiFiService(
        &manager_, &provider_, ssid, mode, security, hidden_ssid);
    provider_.services_.push_back(service);
    return service;
  }
  void AddEndpointToService(WiFiServiceRefPtr service,
                            const WiFiEndpointConstRefPtr& endpoint) {
    provider_.service_by_endpoint_[endpoint.get()] = service;
  }

  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  StrictMock<MockManager> manager_;
  WiFiProvider provider_;
  scoped_refptr<MockProfile> default_profile_;
  scoped_refptr<MockProfile> user_profile_;
  FakeStore default_profile_storage_;
  FakeStore user_profile_storage_;
  int storage_entry_index_;  // shared across profiles
};

MATCHER_P(RefPtrMatch, ref, "") {
  return ref.get() == arg.get();
}

TEST_F(WiFiProviderTest, Start) {
  // Doesn't do anything really.  Just testing for no crash.
  EXPECT_TRUE(GetServices().empty());
  EXPECT_FALSE(GetRunning());
  provider_.Start();
  EXPECT_TRUE(GetServices().empty());
  EXPECT_TRUE(GetRunning());
  EXPECT_TRUE(GetServiceByEndpoint().empty());
  EXPECT_FALSE(provider_.disable_vht());
}

TEST_F(WiFiProviderTest, Stop) {
  MockWiFiServiceRefPtr service0 = AddMockService(
      vector<uint8_t>(1, '0'), kModeManaged, kSecurityNone, false);
  MockWiFiServiceRefPtr service1 = AddMockService(
      vector<uint8_t>(1, '1'), kModeManaged, kSecurityNone, false);
  WiFiEndpointRefPtr endpoint = MakeOpenEndpoint("", "00:00:00:00:00:00", 0, 0);
  AddEndpointToService(service0, endpoint);

  EXPECT_EQ(2, GetServices().size());
  EXPECT_FALSE(GetServiceByEndpoint().empty());
  EXPECT_CALL(*service0, ResetWiFi()).Times(1);
  EXPECT_CALL(*service1, ResetWiFi()).Times(1);
  EXPECT_CALL(manager_, DeregisterService(RefPtrMatch(service0))).Times(1);
  EXPECT_CALL(manager_, DeregisterService(RefPtrMatch(service1))).Times(1);
  provider_.Stop();
  // Verify now, so it's clear that this happened as a result of the call
  // above, and not anything in the destructor(s).
  Mock::VerifyAndClearExpectations(service0.get());
  Mock::VerifyAndClearExpectations(service1.get());
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_TRUE(GetServices().empty());
  EXPECT_TRUE(GetServiceByEndpoint().empty());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileWithNoGroups) {
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 0,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(default_profile_.get());
  EXPECT_TRUE(GetServices().empty());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileMissingSSID) {
  AddServiceToProfileStorage(default_profile_.get(), nullptr, kModeManaged,
                             kSecurityNone, false, true);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 0,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(default_profile_.get());
  EXPECT_TRUE(GetServices().empty());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileEmptySSID) {
  AddServiceToProfileStorage(default_profile_.get(), "", kModeManaged,
                             kSecurityNone, false, true);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 0,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(default_profile_.get());
  EXPECT_TRUE(GetServices().empty());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileMissingMode) {
  AddServiceToProfileStorage(default_profile_.get(), "foo", nullptr,
                             kSecurityNone, false, true);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 0,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(default_profile_.get());
  EXPECT_TRUE(GetServices().empty());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileEmptyMode) {
  AddServiceToProfileStorage(default_profile_.get(), "foo", "", kSecurityNone,
                             false, true);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 0,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(default_profile_.get());
  EXPECT_TRUE(GetServices().empty());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileMissingSecurity) {
  AddServiceToProfileStorage(default_profile_.get(), "foo", kModeManaged,
                             nullptr, false, true);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 0,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(default_profile_.get());
  EXPECT_TRUE(GetServices().empty());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileEmptySecurity) {
  AddServiceToProfileStorage(default_profile_.get(), "foo", kModeManaged, "",
                             false, true);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 0,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(default_profile_.get());
  EXPECT_TRUE(GetServices().empty());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileMissingHidden) {
  AddServiceToProfileStorage(default_profile_.get(), "foo", kModeManaged,
                             kSecurityNone, false, false);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 0,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(default_profile_.get());
  EXPECT_TRUE(GetServices().empty());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileSingle) {
  string kSSID("foo");
  AddServiceToProfileStorage(default_profile_.get(), kSSID.c_str(),
                             kModeManaged, kSecurityNone, false, true);
  EXPECT_CALL(manager_, RegisterService(_))
      .WillOnce(Invoke(this, &WiFiProviderTest::BindServiceToDefaultProfile));
  EXPECT_CALL(manager_, IsServiceEphemeral(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 1,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets))
      .Times(2);
  CreateServicesFromProfile(default_profile_.get());
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(1, GetServices().size());

  const WiFiServiceRefPtr service = GetServices().front();
  const string service_ssid(service->ssid().begin(), service->ssid().end());
  EXPECT_EQ(kSSID, service_ssid);
  EXPECT_EQ(kModeManaged, service->mode());
  EXPECT_TRUE(service->IsSecurityMatch(kSecurityNone));

  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, IsServiceEphemeral(_)).WillRepeatedly(Return(false));
  CreateServicesFromProfile(default_profile_.get());
  EXPECT_EQ(1, GetServices().size());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileHiddenButConnected) {
  string kSSID("foo");
  AddServiceToProfileStorage(default_profile_.get(), kSSID.c_str(),
                             kModeManaged, kSecurityNone, true, true);
  EXPECT_CALL(manager_, RegisterService(_))
      .WillOnce(Invoke(this, &WiFiProviderTest::BindServiceToDefaultProfile));
  EXPECT_CALL(manager_, IsServiceEphemeral(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(manager_, IsTechnologyConnected(Technology(Technology::kWifi)))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, RequestScan(_, _)).Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 1,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets))
      .Times(2);
  CreateServicesFromProfile(default_profile_.get());
  Mock::VerifyAndClearExpectations(&manager_);

  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, IsTechnologyConnected(_)).Times(0);
  EXPECT_CALL(manager_, IsServiceEphemeral(_)).WillRepeatedly(Return(false));
  CreateServicesFromProfile(default_profile_.get());
}

TEST_F(WiFiProviderTest, CreateServicesFromProfileHiddenNotConnected) {
  string kSSID("foo");
  AddServiceToProfileStorage(default_profile_.get(), kSSID.c_str(),
                             kModeManaged, kSecurityNone, true, true);
  EXPECT_CALL(manager_, RegisterService(_))
      .WillOnce(Invoke(this, &WiFiProviderTest::BindServiceToDefaultProfile));
  EXPECT_CALL(manager_, IsServiceEphemeral(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(manager_, IsTechnologyConnected(Technology(Technology::kWifi)))
      .WillOnce(Return(false));
  EXPECT_CALL(manager_, RequestScan(kTypeWifi, _)).Times(1);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 1,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets))
      .Times(2);
  CreateServicesFromProfile(default_profile_.get());
  Mock::VerifyAndClearExpectations(&manager_);

  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, IsTechnologyConnected(_)).Times(0);
  EXPECT_CALL(manager_, RequestScan(_, _)).Times(0);
  EXPECT_CALL(manager_, IsServiceEphemeral(_)).WillRepeatedly(Return(false));
  CreateServicesFromProfile(default_profile_.get());
}

TEST_F(WiFiProviderTest, CreateTemporaryServiceFromProfileNonWiFi) {
  const string kEntryName("name");
  Error error;
  EXPECT_EQ(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         default_profile_, kEntryName, &error));
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(),
              StartsWith("Unspecified or invalid network type"));
}

TEST_F(WiFiProviderTest, CreateTemporaryServiceFromProfileMissingSSID) {
  string entry_name =
      AddServiceToProfileStorage(default_profile_.get(), nullptr, kModeManaged,
                                 kSecurityNone, false, true);
  Error error;
  EXPECT_EQ(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         default_profile_, entry_name, &error));
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(), StartsWith("Unspecified or invalid SSID"));
}

TEST_F(WiFiProviderTest, CreateTemporaryServiceFromProfileMissingMode) {
  string entry_name = AddServiceToProfileStorage(
      default_profile_.get(), "foo", "", kSecurityNone, false, true);

  Error error;
  EXPECT_EQ(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         default_profile_, entry_name, &error));
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(), StartsWith("Network mode not specified"));
}

TEST_F(WiFiProviderTest, CreateTemporaryServiceFromProfileMissingSecurity) {
  string entry_name = AddServiceToProfileStorage(default_profile_.get(), "foo",
                                                 kModeManaged, "", false, true);

  Error error;
  EXPECT_EQ(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         default_profile_, entry_name, &error));
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(),
              StartsWith("Unspecified or invalid security class"));
}

TEST_F(WiFiProviderTest, CreateTemporaryServiceFromProfileMissingHidden) {
  string entry_name = AddServiceToProfileStorage(
      default_profile_.get(), "foo", kModeManaged, kSecurityNone, false, false);

  Error error;
  EXPECT_EQ(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         default_profile_, entry_name, &error));
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_THAT(error.message(), StartsWith("Hidden SSID not specified"));
}

TEST_F(WiFiProviderTest, CreateTemporaryServiceFromProfile) {
  string entry_name = AddServiceToProfileStorage(
      default_profile_.get(), "foo", kModeManaged, kSecurityNone, false, true);

  Error error;
  EXPECT_NE(nullptr, provider_.CreateTemporaryServiceFromProfile(
                         default_profile_, entry_name, &error));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(WiFiProviderTest, CreateTwoServices) {
  AddServiceToProfileStorage(default_profile_.get(), "foo", kModeManaged,
                             kSecurityNone, false, true);
  AddServiceToProfileStorage(default_profile_.get(), "bar", kModeManaged,
                             kSecurityNone, true, true);
  EXPECT_CALL(manager_, RegisterService(_))
      .Times(2)
      .WillRepeatedly(
          Invoke(this, &WiFiProviderTest::BindServiceToDefaultProfile));
  EXPECT_CALL(manager_, IsServiceEphemeral(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(manager_, IsTechnologyConnected(Technology(Technology::kWifi)))
      .WillOnce(Return(true));
  EXPECT_CALL(manager_, RequestScan(kTypeWifi, _)).Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricRememberedWiFiNetworkCount, 2,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(default_profile_.get());
  Mock::VerifyAndClearExpectations(&manager_);

  EXPECT_EQ(2, GetServices().size());
}

TEST_F(WiFiProviderTest, ServiceSourceStats) {
  AddServiceToProfileStorage(default_profile_.get(), "foo", kModeManaged,
                             kSecurityPsk, false, true);
  EXPECT_CALL(manager_, RegisterService(_))
      .WillOnce(Invoke(this, &WiFiProviderTest::BindServiceToDefaultProfile));
  EXPECT_CALL(manager_, IsServiceEphemeral(_)).WillRepeatedly(Return(false));
  // Processing default profile does not generate UMA metrics.
  EXPECT_CALL(
      metrics_,
      SendToUMA(StartsWith("Network.Shill.WiFi.RememberedSystemNetworkCount."),
                _, _, _, _))
      .Times(0);
  EXPECT_CALL(
      metrics_,
      SendToUMA(StartsWith("Network.Shill.WiFi.RememberedUserNetworkCount."), _,
                _, _, _))
      .Times(0);
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricHiddenSSIDNetworkCount, _, _, _, _))
      .Times(0);
  CreateServicesFromProfile(default_profile_.get());
  Mock::VerifyAndClearExpectations(&manager_);

  AddServiceToProfileStorage(user_profile_.get(), "bar", kModeManaged,
                             kSecurityPsk, false, true);
  EXPECT_CALL(manager_, RegisterService(_))
      .WillOnce(Invoke(this, &WiFiProviderTest::BindServiceToUserProfile));
  EXPECT_CALL(manager_, IsServiceEphemeral(_)).WillRepeatedly(Return(false));
  // Processing user profile generates metrics for both, default profile,
  // and user profile.
  EXPECT_CALL(
      metrics_,
      SendToUMA(StartsWith("Network.Shill.WiFi.RememberedSystemNetworkCount."),
                0, Metrics::kMetricRememberedWiFiNetworkCountMin,
                Metrics::kMetricRememberedWiFiNetworkCountMax,
                Metrics::kMetricRememberedWiFiNetworkCountNumBuckets))
      .Times(3);  // none, wep, 802.1x
  EXPECT_CALL(
      metrics_,
      SendToUMA(StartsWith("Network.Shill.WiFi.RememberedUserNetworkCount."), 0,
                Metrics::kMetricRememberedWiFiNetworkCountMin,
                Metrics::kMetricRememberedWiFiNetworkCountMax,
                Metrics::kMetricRememberedWiFiNetworkCountNumBuckets))
      .Times(3);  // none, wep, 802.1x
  EXPECT_CALL(
      metrics_,
      SendToUMA(
          StartsWith("Network.Shill.WiFi.RememberedSystemNetworkCount.psk"), 1,
          Metrics::kMetricRememberedWiFiNetworkCountMin,
          Metrics::kMetricRememberedWiFiNetworkCountMax,
          Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  EXPECT_CALL(
      metrics_,
      SendToUMA(StartsWith("Network.Shill.WiFi.RememberedUserNetworkCount.psk"),
                1, Metrics::kMetricRememberedWiFiNetworkCountMin,
                Metrics::kMetricRememberedWiFiNetworkCountMax,
                Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  EXPECT_CALL(metrics_,
              SendToUMA(Metrics::kMetricHiddenSSIDNetworkCount, 0,
                        Metrics::kMetricRememberedWiFiNetworkCountMin,
                        Metrics::kMetricRememberedWiFiNetworkCountMax,
                        Metrics::kMetricRememberedWiFiNetworkCountNumBuckets));
  CreateServicesFromProfile(user_profile_.get());
}

TEST_F(WiFiProviderTest, GetServiceEmptyMode) {
  Error error;
  EXPECT_FALSE(
      GetService("foo", "", kSecurityNone, false, false, &error).get());
  EXPECT_EQ(Error::kNotSupported, error.type());
}

TEST_F(WiFiProviderTest, GetServiceNoMode) {
  Error error;
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  EXPECT_TRUE(
      GetService("foo", nullptr, kSecurityNone, false, false, &error).get());
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(WiFiProviderTest, GetServiceBadMode) {
  Error error;
  EXPECT_FALSE(
      GetService("foo", "BogoMesh", kSecurityNone, false, false, &error).get());
  EXPECT_EQ(Error::kNotSupported, error.type());
  EXPECT_EQ("service mode is unsupported", error.message());
}

TEST_F(WiFiProviderTest, GetServiceAdhocNotSupported) {
  Error error;
  EXPECT_FALSE(
      GetService("foo", "adhoc", kSecurityNone, false, false, &error).get());
  EXPECT_EQ(Error::kNotSupported, error.type());
  EXPECT_EQ("service mode is unsupported", error.message());
}

TEST_F(WiFiProviderTest, GetServiceNoSSID) {
  Error error;
  EXPECT_FALSE(
      GetService(nullptr, kModeManaged, kSecurityNone, false, false, &error)
          .get());
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("must specify SSID", error.message());
}

TEST_F(WiFiProviderTest, GetServiceEmptySSID) {
  Error error;
  EXPECT_FALSE(
      GetService("", kModeManaged, kSecurityNone, false, false, &error).get());
  EXPECT_EQ(Error::kInvalidNetworkName, error.type());
  EXPECT_EQ("SSID is too short", error.message());
}

TEST_F(WiFiProviderTest, GetServiceLongSSID) {
  Error error;
  string ssid(IEEE_80211::kMaxSSIDLen + 1, '0');
  EXPECT_FALSE(GetService(ssid.c_str(), kModeManaged, kSecurityNone, false,
                          false, &error)
                   .get());
  EXPECT_EQ(Error::kInvalidNetworkName, error.type());
  EXPECT_EQ("SSID is too long", error.message());
}

TEST_F(WiFiProviderTest, GetServiceJustLongEnoughSSID) {
  Error error;
  string ssid(IEEE_80211::kMaxSSIDLen, '0');
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  EXPECT_TRUE(GetService(ssid.c_str(), kModeManaged, kSecurityNone, false,
                         false, &error)
                  .get());
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(WiFiProviderTest, GetServiceBadSecurityClass) {
  Error error;
  EXPECT_FALSE(
      GetService("foo", kModeManaged, kSecurityRsn, false, false, &error)
          .get());
  EXPECT_EQ(Error::kNotSupported, error.type());
  EXPECT_EQ("security class is unsupported", error.message());
}

TEST_F(WiFiProviderTest, GetServiceMinimal) {
  Error error;
  const string kSSID("foo");
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  WiFiServiceRefPtr service =
      GetService(kSSID.c_str(), kModeManaged, nullptr, false, false, &error);
  EXPECT_NE(nullptr, service);
  EXPECT_TRUE(error.IsSuccess());
  const string service_ssid(service->ssid().begin(), service->ssid().end());
  EXPECT_EQ(kSSID, service_ssid);
  EXPECT_EQ(kModeManaged, service->mode());

  // These two should be set to their default values if not specified.
  EXPECT_TRUE(service->IsSecurityMatch(kSecurityNone));
  EXPECT_TRUE(service->hidden_ssid());
}

TEST_F(WiFiProviderTest, GetServiceFullySpecified) {
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  const string kSSID("bar");
  Error error;
  WiFiServiceRefPtr service0 = GetService(kSSID.c_str(), kModeManaged,
                                          kSecurityPsk, false, true, &error);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_TRUE(error.IsSuccess());
  const string service_ssid(service0->ssid().begin(), service0->ssid().end());
  EXPECT_EQ(kSSID, service_ssid);
  EXPECT_EQ(kModeManaged, service0->mode());
  EXPECT_TRUE(service0->IsSecurityMatch(kSecurityPsk));
  EXPECT_FALSE(service0->hidden_ssid());

  // Getting the same service parameters (even with a different hidden
  // parameter) should return the same service.
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  WiFiServiceRefPtr service1 =
      GetService(kSSID.c_str(), kModeManaged, kSecurityPsk, true, true, &error);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(service0, service1);
  EXPECT_EQ(1, GetServices().size());

  // Getting the same ssid with different other parameters should return
  // a different service.
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  WiFiServiceRefPtr service2 = GetService(kSSID.c_str(), kModeManaged,
                                          kSecurityNone, true, true, &error);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_NE(service0, service2);
  EXPECT_EQ(2, GetServices().size());
}

TEST_F(WiFiProviderTest, GetServiceByHexSsid) {
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  const string kSSID("bar");
  const string kHexSsid(base::HexEncode(kSSID.c_str(), kSSID.length()));

  KeyValueStore args;
  SetServiceParameters(nullptr, nullptr, kSecurityPsk, false, true, &args);
  args.Set<string>(kWifiHexSsid, kHexSsid);

  Error error;
  WiFiServiceRefPtr service = GetWiFiService(args, &error);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_TRUE(error.IsSuccess());
  const string service_ssid(service->ssid().begin(), service->ssid().end());
  EXPECT_EQ(kSSID, service_ssid);
  EXPECT_EQ(kModeManaged, service->mode());
  EXPECT_TRUE(service->IsSecurityMatch(kSecurityPsk));
  EXPECT_FALSE(service->hidden_ssid());

  // While here, make sure FindSimilarService also supports kWifiHexSsid.
  Error find_error;
  ServiceRefPtr find_service = provider_.FindSimilarService(args, &find_error);
  EXPECT_TRUE(find_error.IsSuccess());
  EXPECT_EQ(service, find_service);
}

TEST_F(WiFiProviderTest, GetServiceUnexpectedSecurityProperty) {
  const string kSSID("bar");
  KeyValueStore args;
  args.Set<string>(kTypeProperty, kTypeWifi);
  args.Set<string>(kSSIDProperty, kSSID);
  args.Set<string>(kSecurityProperty, kSecurityRsn);
  args.Set<bool>(kWifiHiddenSsid, false);

  Error error;
  WiFiServiceRefPtr service;
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  service = GetWiFiService(args, &error);
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("Unexpected Security property", error.message());
}

TEST_F(WiFiProviderTest, GetServiceBogusSecurityClass) {
  const string kSSID("bar");
  KeyValueStore args;
  args.Set<string>(kTypeProperty, kTypeWifi);
  args.Set<string>(kSSIDProperty, kSSID);
  args.Set<string>(kSecurityClassProperty, "rot-47");
  args.Set<bool>(kWifiHiddenSsid, false);

  Error error;
  WiFiServiceRefPtr service;
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  service = GetWiFiService(args, &error);
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_EQ(Error::kNotSupported, error.type());
}

TEST_F(WiFiProviderTest, GetServiceNonSecurityClass) {
  const string kSSID("bar");
  KeyValueStore args;
  args.Set<string>(kTypeProperty, kTypeWifi);
  args.Set<string>(kSSIDProperty, kSSID);
  // Using a non-class as a class should be rejected.
  args.Set<string>(kSecurityClassProperty, kSecurityRsn);
  args.Set<bool>(kWifiHiddenSsid, false);

  Error error;
  WiFiServiceRefPtr service;
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  service = GetWiFiService(args, &error);
  EXPECT_FALSE(error.IsSuccess());
  EXPECT_EQ(Error::kNotSupported, error.type());
}

TEST_F(WiFiProviderTest, FindSimilarService) {
  // Since CreateTemporyService uses exactly the same validation as
  // GetService, don't bother with testing invalid parameters.
  const string kSSID("foo");
  KeyValueStore args;
  SetServiceParameters(kSSID.c_str(), kModeManaged, kSecurityNone, true, true,
                       &args);
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  Error get_service_error;
  WiFiServiceRefPtr service = GetWiFiService(args, &get_service_error);
  EXPECT_EQ(1, GetServices().size());

  {
    Error error;
    ServiceRefPtr find_service = provider_.FindSimilarService(args, &error);
    EXPECT_EQ(service, find_service);
    EXPECT_TRUE(error.IsSuccess());
  }

  args.Set<bool>(kWifiHiddenSsid, false);

  {
    Error error;
    ServiceRefPtr find_service = provider_.FindSimilarService(args, &error);
    EXPECT_EQ(service, find_service);
    EXPECT_TRUE(error.IsSuccess());
  }

  args.Set<string>(kSecurityClassProperty, kSecurityPsk);

  {
    Error error;
    ServiceRefPtr find_service = provider_.FindSimilarService(args, &error);
    EXPECT_EQ(nullptr, find_service);
    EXPECT_EQ(Error::kNotFound, error.type());
  }
}

TEST_F(WiFiProviderTest, CreateTemporaryService) {
  // Since CreateTemporyService uses exactly the same validation as
  // GetService, don't bother with testing invalid parameters.
  const string kSSID("foo");
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  Error error;
  WiFiServiceRefPtr service0 = GetService(kSSID.c_str(), kModeManaged,
                                          kSecurityNone, true, true, &error);
  EXPECT_EQ(1, GetServices().size());
  Mock::VerifyAndClearExpectations(&manager_);

  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  ServiceRefPtr service1 = CreateTemporaryService(
      kSSID.c_str(), kModeManaged, kSecurityNone, true, true, &error);

  // Test that a new service was created, but not registered with the
  // manager or added to the provider's service list.
  EXPECT_EQ(1, GetServices().size());
  EXPECT_TRUE(service0 != service1);
  EXPECT_TRUE(service1->HasOneRef());
}

TEST_F(WiFiProviderTest, FindServicePSK) {
  const string kSSID("an_ssid");
  Error error;
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  KeyValueStore args;
  SetServiceParameters(kSSID.c_str(), kModeManaged, kSecurityPsk, false, false,
                       &args);
  WiFiServiceRefPtr service = GetWiFiService(args, &error);
  ASSERT_NE(nullptr, service);
  const vector<uint8_t> ssid_bytes(kSSID.begin(), kSSID.end());
  WiFiServiceRefPtr wpa_service(
      FindService(ssid_bytes, kModeManaged, kSecurityWpa));
  EXPECT_EQ(service, wpa_service);
  WiFiServiceRefPtr rsn_service(
      FindService(ssid_bytes, kModeManaged, kSecurityRsn));
  EXPECT_EQ(service, rsn_service);
  WiFiServiceRefPtr psk_service(
      FindService(ssid_bytes, kModeManaged, kSecurityPsk));
  EXPECT_EQ(service, psk_service);
  WiFiServiceRefPtr wep_service(
      FindService(ssid_bytes, kModeManaged, kSecurityWep));
  EXPECT_EQ(nullptr, wep_service);
}

TEST_F(WiFiProviderTest, FindServiceForEndpoint) {
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  Error error;
  const string kSSID("an_ssid");
  WiFiServiceRefPtr service = GetService(kSSID.c_str(), kModeManaged,
                                         kSecurityNone, false, true, &error);
  ASSERT_NE(nullptr, service);
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(kSSID, "00:00:00:00:00:00", 0, 0);
  WiFiServiceRefPtr endpoint_service =
      provider_.FindServiceForEndpoint(endpoint);
  // Just because a matching service exists, we shouldn't necessarily have
  // it returned.  We will test that this function returns the correct
  // service if the endpoint is added below.
  EXPECT_EQ(nullptr, endpoint_service);
}

TEST_F(WiFiProviderTest, OnEndpointAdded) {
  provider_.Start();
  const string ssid0("an_ssid");
  const vector<uint8_t> ssid0_bytes(ssid0.begin(), ssid0.end());
  EXPECT_FALSE(FindService(ssid0_bytes, kModeManaged, kSecurityNone));
  WiFiEndpointRefPtr endpoint0 =
      MakeOpenEndpoint(ssid0, "00:00:00:00:00:00", 0, 0);
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  EXPECT_CALL(manager_, UpdateService(_)).Times(1);
  provider_.OnEndpointAdded(endpoint0);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(1, GetServices().size());
  WiFiServiceRefPtr service0(
      FindService(ssid0_bytes, kModeManaged, kSecurityNone));
  EXPECT_NE(nullptr, service0);
  EXPECT_TRUE(service0->HasEndpoints());
  EXPECT_EQ(1, GetServiceByEndpoint().size());
  WiFiServiceRefPtr endpoint_service =
      provider_.FindServiceForEndpoint(endpoint0);
  EXPECT_EQ(service0, endpoint_service);

  WiFiEndpointRefPtr endpoint1 =
      MakeOpenEndpoint(ssid0, "00:00:00:00:00:01", 0, 0);
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(service0))).Times(1);
  provider_.OnEndpointAdded(endpoint1);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(1, GetServices().size());

  const string ssid1("another_ssid");
  const vector<uint8_t> ssid1_bytes(ssid1.begin(), ssid1.end());
  EXPECT_FALSE(FindService(ssid1_bytes, kModeManaged, kSecurityNone));
  WiFiEndpointRefPtr endpoint2 =
      MakeOpenEndpoint(ssid1, "00:00:00:00:00:02", 0, 0);
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  EXPECT_CALL(manager_, UpdateService(_)).Times(1);
  provider_.OnEndpointAdded(endpoint2);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(2, GetServices().size());

  WiFiServiceRefPtr service1(
      FindService(ssid1_bytes, kModeManaged, kSecurityNone));
  EXPECT_NE(nullptr, service1);
  EXPECT_TRUE(service1->HasEndpoints());
  EXPECT_TRUE(service1 != service0);
}

TEST_F(WiFiProviderTest, OnEndpointAddedWithSecurity) {
  provider_.Start();
  const string ssid0("an_ssid");
  const vector<uint8_t> ssid0_bytes(ssid0.begin(), ssid0.end());
  EXPECT_FALSE(FindService(ssid0_bytes, kModeManaged, kSecurityNone));
  WiFiEndpoint::SecurityFlags rsn_flags;
  rsn_flags.rsn_psk = true;
  WiFiEndpointRefPtr endpoint0 =
      MakeEndpoint(ssid0, "00:00:00:00:00:00", 0, 0, rsn_flags);
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  EXPECT_CALL(manager_, UpdateService(_)).Times(1);
  provider_.OnEndpointAdded(endpoint0);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(1, GetServices().size());
  WiFiServiceRefPtr service0(
      FindService(ssid0_bytes, kModeManaged, kSecurityWpa));
  EXPECT_NE(nullptr, service0);
  EXPECT_TRUE(service0->HasEndpoints());
  EXPECT_EQ(kSecurityRsn, service0->security());

  WiFiEndpoint::SecurityFlags wpa_flags;
  wpa_flags.wpa_psk = true;
  WiFiEndpointRefPtr endpoint1 =
      MakeEndpoint(ssid0, "00:00:00:00:00:01", 0, 0, wpa_flags);
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(service0))).Times(1);
  provider_.OnEndpointAdded(endpoint1);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(1, GetServices().size());

  const string ssid1("another_ssid");
  const vector<uint8_t> ssid1_bytes(ssid1.begin(), ssid1.end());
  EXPECT_FALSE(FindService(ssid1_bytes, kModeManaged, kSecurityNone));
  WiFiEndpointRefPtr endpoint2 =
      MakeEndpoint(ssid1, "00:00:00:00:00:02", 0, 0, wpa_flags);
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  EXPECT_CALL(manager_, UpdateService(_)).Times(1);
  provider_.OnEndpointAdded(endpoint2);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(2, GetServices().size());

  WiFiServiceRefPtr service1(
      FindService(ssid1_bytes, kModeManaged, kSecurityRsn));
  EXPECT_NE(nullptr, service1);
  EXPECT_TRUE(service1->HasEndpoints());
  EXPECT_EQ(kSecurityWpa, service1->security());
  EXPECT_TRUE(service1 != service0);
}

TEST_F(WiFiProviderTest, OnEndpointAddedMultiSecurity) {
  // Multiple security modes with the same SSID.
  provider_.Start();
  const string ssid0("an_ssid");
  const vector<uint8_t> ssid0_bytes(ssid0.begin(), ssid0.end());

  WiFiEndpoint::SecurityFlags rsn_flags;
  rsn_flags.rsn_psk = true;
  WiFiEndpointRefPtr endpoint0 =
      MakeEndpoint(ssid0, "00:00:00:00:00:00", 0, 0, rsn_flags);
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  EXPECT_CALL(manager_, UpdateService(_)).Times(1);
  provider_.OnEndpointAdded(endpoint0);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(1, GetServices().size());

  WiFiServiceRefPtr service0(
      FindService(ssid0_bytes, kModeManaged, kSecurityWpa));
  EXPECT_NE(nullptr, service0);
  EXPECT_TRUE(service0->HasEndpoints());
  EXPECT_EQ(kSecurityRsn, service0->security());

  WiFiEndpoint::SecurityFlags none_flags;
  WiFiEndpointRefPtr endpoint1 =
      MakeEndpoint(ssid0, "00:00:00:00:00:01", 0, 0, none_flags);
  EXPECT_CALL(manager_, RegisterService(_)).Times(1);
  EXPECT_CALL(manager_, UpdateService(_)).Times(1);
  provider_.OnEndpointAdded(endpoint1);
  Mock::VerifyAndClearExpectations(&manager_);
  EXPECT_EQ(2, GetServices().size());

  WiFiServiceRefPtr service1(
      FindService(ssid0_bytes, kModeManaged, kSecurityNone));
  EXPECT_NE(nullptr, service1);
  EXPECT_TRUE(service1->HasEndpoints());
  EXPECT_EQ(kSecurityNone, service1->security());
  EXPECT_EQ(kSecurityRsn, service0->security());
}

TEST_F(WiFiProviderTest, OnEndpointAddedWhileStopped) {
  // If we don't call provider_.Start(), OnEndpointAdded should have no effect.
  const string ssid("an_ssid");
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(ssid, "00:00:00:00:00:00", 0, 0);
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, UpdateService(_)).Times(0);
  provider_.OnEndpointAdded(endpoint);
  EXPECT_TRUE(GetServices().empty());
}

TEST_F(WiFiProviderTest, OnEndpointAddedToMockService) {
  // The previous test allowed the provider to create its own "real"
  // WiFiServices, which hides some of what we can test with mock
  // services.  Re-do an add-endpoint operation by seeding the provider
  // with a mock service.
  provider_.Start();
  const string ssid0("an_ssid");
  const vector<uint8_t> ssid0_bytes(ssid0.begin(), ssid0.end());
  MockWiFiServiceRefPtr service0 =
      AddMockService(ssid0_bytes, kModeManaged, kSecurityNone, false);
  const string ssid1("another_ssid");
  const vector<uint8_t> ssid1_bytes(ssid1.begin(), ssid1.end());
  MockWiFiServiceRefPtr service1 =
      AddMockService(ssid1_bytes, kModeManaged, kSecurityNone, false);
  EXPECT_EQ(service0, FindService(ssid0_bytes, kModeManaged, kSecurityNone));
  WiFiEndpointRefPtr endpoint0 =
      MakeOpenEndpoint(ssid0, "00:00:00:00:00:00", 0, 0);
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(service0))).Times(1);
  EXPECT_CALL(*service0, AddEndpoint(RefPtrMatch(endpoint0))).Times(1);
  EXPECT_CALL(*service1, AddEndpoint(_)).Times(0);
  provider_.OnEndpointAdded(endpoint0);
  Mock::VerifyAndClearExpectations(&manager_);
  Mock::VerifyAndClearExpectations(service0.get());
  Mock::VerifyAndClearExpectations(service1.get());

  WiFiEndpointRefPtr endpoint1 =
      MakeOpenEndpoint(ssid0, "00:00:00:00:00:01", 0, 0);
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(service0))).Times(1);
  EXPECT_CALL(*service0, AddEndpoint(RefPtrMatch(endpoint1))).Times(1);
  EXPECT_CALL(*service1, AddEndpoint(_)).Times(0);
  provider_.OnEndpointAdded(endpoint1);
  Mock::VerifyAndClearExpectations(&manager_);
  Mock::VerifyAndClearExpectations(service0.get());
  Mock::VerifyAndClearExpectations(service1.get());

  WiFiEndpointRefPtr endpoint2 =
      MakeOpenEndpoint(ssid1, "00:00:00:00:00:02", 0, 0);
  EXPECT_CALL(manager_, RegisterService(_)).Times(0);
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(service1))).Times(1);
  EXPECT_CALL(*service0, AddEndpoint(_)).Times(0);
  EXPECT_CALL(*service1, AddEndpoint(RefPtrMatch(endpoint2))).Times(1);
  provider_.OnEndpointAdded(endpoint2);
}

TEST_F(WiFiProviderTest, OnEndpointRemoved) {
  provider_.Start();
  const string ssid0("an_ssid");
  const vector<uint8_t> ssid0_bytes(ssid0.begin(), ssid0.end());
  MockWiFiServiceRefPtr service0 =
      AddMockService(ssid0_bytes, kModeManaged, kSecurityNone, false);
  const string ssid1("another_ssid");
  const vector<uint8_t> ssid1_bytes(ssid1.begin(), ssid1.end());
  MockWiFiServiceRefPtr service1 =
      AddMockService(ssid1_bytes, kModeManaged, kSecurityNone, false);
  EXPECT_EQ(2, GetServices().size());

  // Remove the last endpoint of a non-remembered service.
  WiFiEndpointRefPtr endpoint0 =
      MakeOpenEndpoint(ssid0, "00:00:00:00:00:00", 0, 0);
  AddEndpointToService(service0, endpoint0);
  EXPECT_EQ(1, GetServiceByEndpoint().size());

  EXPECT_CALL(*service0, RemoveEndpoint(RefPtrMatch(endpoint0))).Times(1);
  EXPECT_CALL(*service1, RemoveEndpoint(_)).Times(0);
  EXPECT_CALL(*service0, HasEndpoints()).WillOnce(Return(false));
  EXPECT_CALL(*service0, IsRemembered()).WillOnce(Return(false));
  EXPECT_CALL(*service0, ResetWiFi()).Times(1);
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(service0))).Times(0);
  EXPECT_CALL(manager_, DeregisterService(RefPtrMatch(service0))).Times(1);
  provider_.OnEndpointRemoved(endpoint0);
  // Verify now, so it's clear that this happened as a result of the call
  // above, and not anything in the destructor(s).
  Mock::VerifyAndClearExpectations(&manager_);
  Mock::VerifyAndClearExpectations(service0.get());
  Mock::VerifyAndClearExpectations(service1.get());
  EXPECT_EQ(1, GetServices().size());
  EXPECT_EQ(service1, GetServices().front());
  EXPECT_TRUE(GetServiceByEndpoint().empty());
}

TEST_F(WiFiProviderTest, OnEndpointRemovedButHasEndpoints) {
  provider_.Start();
  const string ssid0("an_ssid");
  const vector<uint8_t> ssid0_bytes(ssid0.begin(), ssid0.end());
  MockWiFiServiceRefPtr service0 =
      AddMockService(ssid0_bytes, kModeManaged, kSecurityNone, false);
  EXPECT_EQ(1, GetServices().size());

  // Remove an endpoint of a non-remembered service.
  WiFiEndpointRefPtr endpoint0 =
      MakeOpenEndpoint(ssid0, "00:00:00:00:00:00", 0, 0);
  AddEndpointToService(service0, endpoint0);
  EXPECT_EQ(1, GetServiceByEndpoint().size());

  EXPECT_CALL(*service0, RemoveEndpoint(RefPtrMatch(endpoint0))).Times(1);
  EXPECT_CALL(*service0, HasEndpoints()).WillOnce(Return(true));
  EXPECT_CALL(*service0, IsRemembered()).WillRepeatedly(Return(false));
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(service0))).Times(1);
  EXPECT_CALL(*service0, ResetWiFi()).Times(0);
  EXPECT_CALL(manager_, DeregisterService(_)).Times(0);
  provider_.OnEndpointRemoved(endpoint0);
  // Verify now, so it's clear that this happened as a result of the call
  // above, and not anything in the destructor(s).
  Mock::VerifyAndClearExpectations(&manager_);
  Mock::VerifyAndClearExpectations(service0.get());
  EXPECT_EQ(1, GetServices().size());
  EXPECT_TRUE(GetServiceByEndpoint().empty());
}

TEST_F(WiFiProviderTest, OnEndpointRemovedButIsRemembered) {
  provider_.Start();
  const string ssid0("an_ssid");
  const vector<uint8_t> ssid0_bytes(ssid0.begin(), ssid0.end());
  MockWiFiServiceRefPtr service0 =
      AddMockService(ssid0_bytes, kModeManaged, kSecurityNone, false);
  EXPECT_EQ(1, GetServices().size());

  // Remove the last endpoint of a remembered service.
  WiFiEndpointRefPtr endpoint0 =
      MakeOpenEndpoint(ssid0, "00:00:00:00:00:00", 0, 0);
  AddEndpointToService(service0, endpoint0);
  EXPECT_EQ(1, GetServiceByEndpoint().size());

  EXPECT_CALL(*service0, RemoveEndpoint(RefPtrMatch(endpoint0))).Times(1);
  EXPECT_CALL(*service0, HasEndpoints()).WillRepeatedly(Return(false));
  EXPECT_CALL(*service0, IsRemembered()).WillOnce(Return(true));
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(service0))).Times(1);
  EXPECT_CALL(*service0, ResetWiFi()).Times(0);
  EXPECT_CALL(manager_, DeregisterService(_)).Times(0);
  provider_.OnEndpointRemoved(endpoint0);
  // Verify now, so it's clear that this happened as a result of the call
  // above, and not anything in the destructor(s).
  Mock::VerifyAndClearExpectations(&manager_);
  Mock::VerifyAndClearExpectations(service0.get());
  EXPECT_EQ(1, GetServices().size());
  EXPECT_TRUE(GetServiceByEndpoint().empty());
}

TEST_F(WiFiProviderTest, OnEndpointRemovedWhileStopped) {
  // If we don't call provider_.Start(), OnEndpointRemoved should not
  // cause a crash even if a service matching the endpoint does not exist.
  const string ssid("an_ssid");
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(ssid, "00:00:00:00:00:00", 0, 0);
  provider_.OnEndpointRemoved(endpoint);
}

TEST_F(WiFiProviderTest, OnEndpointUpdated) {
  provider_.Start();

  // Create an endpoint and associate it with a mock service.
  const string ssid("an_ssid");
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(ssid, "00:00:00:00:00:00", 0, 0);

  const vector<uint8_t> ssid_bytes(ssid.begin(), ssid.end());
  MockWiFiServiceRefPtr open_service =
      AddMockService(ssid_bytes, kModeManaged, kSecurityNone, false);
  EXPECT_CALL(*open_service, AddEndpoint(RefPtrMatch(endpoint)));
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(open_service)));
  provider_.OnEndpointAdded(endpoint);
  Mock::VerifyAndClearExpectations(open_service.get());

  // WiFiProvider is running and endpoint matches this service.
  EXPECT_CALL(*open_service, NotifyEndpointUpdated(RefPtrMatch(endpoint)));
  EXPECT_CALL(*open_service, AddEndpoint(_)).Times(0);
  provider_.OnEndpointUpdated(endpoint);
  Mock::VerifyAndClearExpectations(open_service.get());

  // If the endpoint is changed in a way that causes it to match a different
  // service, the provider should transfer the endpoint from one service to
  // the other.
  MockWiFiServiceRefPtr rsn_service =
      AddMockService(ssid_bytes, kModeManaged, kSecurityPsk, false);
  EXPECT_CALL(*open_service, RemoveEndpoint(RefPtrMatch(endpoint)));
  // We are playing out a scenario where the open service is not removed
  // since it still claims to have more endpoints remaining.
  EXPECT_CALL(*open_service, HasEndpoints()).WillOnce(Return(true));
  EXPECT_CALL(*rsn_service, AddEndpoint(RefPtrMatch(endpoint)));
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(open_service)));
  EXPECT_CALL(manager_, UpdateService(RefPtrMatch(rsn_service)));
  endpoint->set_security_mode(kSecurityRsn);
  provider_.OnEndpointUpdated(endpoint);
}

TEST_F(WiFiProviderTest, OnEndpointUpdatedWhileStopped) {
  // If we don't call provider_.Start(), OnEndpointUpdated should not
  // cause a crash even if a service matching the endpoint does not exist.
  const string ssid("an_ssid");
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(ssid, "00:00:00:00:00:00", 0, 0);
  provider_.OnEndpointUpdated(endpoint);
}

TEST_F(WiFiProviderTest, OnServiceUnloaded) {
  // This function should never unregister services itself -- the Manager
  // will automatically deregister the service if OnServiceUnloaded()
  // returns true (via WiFiService::Unload()).
  EXPECT_CALL(manager_, DeregisterService(_)).Times(0);

  MockWiFiServiceRefPtr service = AddMockService(
      vector<uint8_t>(1, '0'), kModeManaged, kSecurityNone, false);
  EXPECT_EQ(1, GetServices().size());
  EXPECT_CALL(*service, HasEndpoints()).WillOnce(Return(true));
  EXPECT_CALL(*service, ResetWiFi()).Times(0);
  EXPECT_FALSE(provider_.OnServiceUnloaded(service));
  EXPECT_EQ(1, GetServices().size());
  Mock::VerifyAndClearExpectations(service.get());

  EXPECT_CALL(*service, HasEndpoints()).WillOnce(Return(false));
  EXPECT_CALL(*service, ResetWiFi()).Times(1);
  EXPECT_TRUE(provider_.OnServiceUnloaded(service));
  // Verify now, so it's clear that this happened as a result of the call
  // above, and not anything in the destructor(s).
  Mock::VerifyAndClearExpectations(service.get());
  EXPECT_TRUE(GetServices().empty());

  Mock::VerifyAndClearExpectations(&manager_);
}

TEST_F(WiFiProviderTest, GetHiddenSSIDList) {
  EXPECT_TRUE(provider_.GetHiddenSSIDList().empty());
  const vector<uint8_t> ssid0(1, '0');
  AddMockService(ssid0, kModeManaged, kSecurityNone, false);
  EXPECT_TRUE(provider_.GetHiddenSSIDList().empty());

  const vector<uint8_t> ssid1(1, '1');
  MockWiFiServiceRefPtr service1 =
      AddMockService(ssid1, kModeManaged, kSecurityNone, true);
  EXPECT_CALL(*service1, IsRemembered()).WillRepeatedly(Return(false));
  EXPECT_TRUE(provider_.GetHiddenSSIDList().empty());

  const vector<uint8_t> ssid2(1, '2');
  MockWiFiServiceRefPtr service2 =
      AddMockService(ssid2, kModeManaged, kSecurityNone, true);
  EXPECT_CALL(*service2, IsRemembered()).WillRepeatedly(Return(true));
  ByteArrays ssid_list = provider_.GetHiddenSSIDList();

  EXPECT_EQ(1, ssid_list.size());
  EXPECT_TRUE(ssid_list[0] == ssid2);

  const vector<uint8_t> ssid3(1, '3');
  MockWiFiServiceRefPtr service3 =
      AddMockService(ssid3, kModeManaged, kSecurityNone, false);
  EXPECT_CALL(*service2, IsRemembered()).WillRepeatedly(Return(true));

  ssid_list = provider_.GetHiddenSSIDList();
  EXPECT_EQ(1, ssid_list.size());
  EXPECT_TRUE(ssid_list[0] == ssid2);

  const vector<uint8_t> ssid4(1, '4');
  MockWiFiServiceRefPtr service4 =
      AddMockService(ssid4, kModeManaged, kSecurityNone, true);
  EXPECT_CALL(*service4, IsRemembered()).WillRepeatedly(Return(true));

  ssid_list = provider_.GetHiddenSSIDList();
  EXPECT_EQ(2, ssid_list.size());
  EXPECT_TRUE(ssid_list[0] == ssid2);
  EXPECT_TRUE(ssid_list[1] == ssid4);
}

TEST_F(WiFiProviderTest, ReportAutoConnectableServices) {
  MockWiFiServiceRefPtr service0 = AddMockService(
      vector<uint8_t>(1, '0'), kModeManaged, kSecurityNone, false);
  MockWiFiServiceRefPtr service1 = AddMockService(
      vector<uint8_t>(1, '1'), kModeManaged, kSecurityNone, false);
  service0->EnableAndRetainAutoConnect();
  service0->SetConnectable(true);
  service1->EnableAndRetainAutoConnect();
  service1->SetConnectable(true);

  EXPECT_CALL(*service0, IsAutoConnectable(_))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(*service1, IsAutoConnectable(_)).WillRepeatedly(Return(false));

  // With 1 auto connectable service.
  EXPECT_CALL(metrics_, NotifyWifiAutoConnectableServices(1));
  provider_.ReportAutoConnectableServices();

  // With no auto connectable service.
  EXPECT_CALL(metrics_, NotifyWifiAutoConnectableServices(_)).Times(0);
  provider_.ReportAutoConnectableServices();
}

TEST_F(WiFiProviderTest, NumAutoConnectableServices) {
  MockWiFiServiceRefPtr service0 = AddMockService(
      vector<uint8_t>(1, '0'), kModeManaged, kSecurityNone, false);
  MockWiFiServiceRefPtr service1 = AddMockService(
      vector<uint8_t>(1, '1'), kModeManaged, kSecurityNone, false);
  service0->EnableAndRetainAutoConnect();
  service0->SetConnectable(true);
  service1->EnableAndRetainAutoConnect();
  service1->SetConnectable(true);

  EXPECT_CALL(*service0, IsAutoConnectable(_))
      .WillOnce(Return(true))
      .WillOnce(Return(false));
  EXPECT_CALL(*service1, IsAutoConnectable(_)).WillRepeatedly(Return(true));

  // 2 auto-connectable services.
  EXPECT_EQ(2, provider_.NumAutoConnectableServices());

  // 1 auto-connectable service.
  EXPECT_EQ(1, provider_.NumAutoConnectableServices());
}

TEST_F(WiFiProviderTest, GetSsidsConfiguredForAutoConnect) {
  vector<uint8_t> ssid0(3, '0');
  vector<uint8_t> ssid1(5, '1');
  ByteString ssid0_bytes(ssid0);
  ByteString ssid1_bytes(ssid1);
  MockWiFiServiceRefPtr service0 =
      AddMockService(ssid0, kModeManaged, kSecurityNone, false);
  MockWiFiServiceRefPtr service1 =
      AddMockService(ssid1, kModeManaged, kSecurityNone, false);
  // 2 services configured for auto-connect.
  service0->SetAutoConnect(true);
  service1->SetAutoConnect(true);
  vector<ByteString> service_list_0 =
      provider_.GetSsidsConfiguredForAutoConnect();
  EXPECT_EQ(2, service_list_0.size());
  EXPECT_TRUE(ssid0_bytes.Equals(service_list_0[0]));
  EXPECT_TRUE(ssid1_bytes.Equals(service_list_0[1]));

  // 1 service configured for auto-connect.
  service0->SetAutoConnect(false);
  service1->SetAutoConnect(true);
  vector<ByteString> service_list_1 =
      provider_.GetSsidsConfiguredForAutoConnect();
  EXPECT_EQ(1, service_list_1.size());
  EXPECT_TRUE(ssid1_bytes.Equals(service_list_1[0]));
}

}  // namespace shill
