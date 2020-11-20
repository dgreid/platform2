// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_endpoint.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/stl_util.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_log.h"
#include "shill/net/ieee80211.h"
#include "shill/net/mock_netlink_manager.h"
#include "shill/property_store_test.h"
#include "shill/refptr_types.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/tethering.h"
#include "shill/wifi/mock_wake_on_wifi.h"
#include "shill/wifi/mock_wifi.h"

using std::map;
using std::set;
using std::string;
using std::vector;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Mock;
using ::testing::NiceMock;

namespace shill {

// Fake MAC address.
constexpr char kDeviceAddress[] = "aabbccddeeff";

class WiFiEndpointTest : public PropertyStoreTest {
 public:
  WiFiEndpointTest()
      : wifi_(new NiceMock<MockWiFi>(
            manager(), "wifi", kDeviceAddress, 0, new MockWakeOnWiFi())) {}
  ~WiFiEndpointTest() override = default;

 protected:
  KeyValueStore MakeKeyManagementArgs(
      vector<string> key_management_method_strings) {
    KeyValueStore args;
    args.Set<Strings>(WPASupplicant::kSecurityMethodPropertyKeyManagement,
                      key_management_method_strings);
    return args;
  }

  KeyValueStore MakePrivacyArgs(bool is_private) {
    KeyValueStore props;
    props.Set<bool>(WPASupplicant::kPropertyPrivacy, is_private);
    return props;
  }

  KeyValueStore MakeSecurityArgs(const string& security_protocol,
                                 const string& key_management_method) {
    KeyValueStore args;
    vector<string> key_management_method_vector;
    if (!key_management_method.empty()) {
      key_management_method_vector = {key_management_method};
    }
    args.Set<KeyValueStore>(
        security_protocol, MakeKeyManagementArgs(key_management_method_vector));
    return args;
  }

  const char* ParseSecurity(const KeyValueStore& properties) {
    WiFiEndpoint::SecurityFlags security_flags;
    return WiFiEndpoint::ParseSecurity(properties, &security_flags);
  }

  void AddIEWithData(uint8_t type, vector<uint8_t> data, vector<uint8_t>* ies) {
    ies->push_back(type);         // type
    ies->push_back(data.size());  // length
    ies->insert(ies->end(), data.begin(), data.end());
  }

  void AddIE(uint8_t type, vector<uint8_t>* ies) {
    AddIEWithData(type, vector<uint8_t>(1), ies);
  }

  void AddVendorIE(uint32_t oui,
                   uint8_t vendor_type,
                   const vector<uint8_t>& data,
                   vector<uint8_t>* ies) {
    ies->push_back(IEEE_80211::kElemIdVendor);  // type
    ies->push_back(4 + data.size());            // length
    ies->push_back((oui >> 16) & 0xff);         // OUI MSByte
    ies->push_back((oui >> 8) & 0xff);          // OUI middle octet
    ies->push_back(oui & 0xff);                 // OUI LSByte
    ies->push_back(vendor_type);                // OUI Type
    ies->insert(ies->end(), data.begin(), data.end());
  }

  void AddWPSElement(uint16_t type, const string& value, vector<uint8_t>* wps) {
    wps->push_back(type >> 8);  // type MSByte
    wps->push_back(type);       // type LSByte
    CHECK(value.size() < std::numeric_limits<uint16_t>::max());
    wps->push_back((value.size() >> 8) & 0xff);  // length MSByte
    wps->push_back(value.size() & 0xff);         // length LSByte
    wps->insert(wps->end(), value.begin(), value.end());
  }

  KeyValueStore MakeBSSPropertiesWithIEs(const vector<uint8_t>& ies) {
    KeyValueStore properties;
    properties.Set<vector<uint8_t>>(WPASupplicant::kBSSPropertyIEs, ies);
    return properties;
  }

  // Creates the RSN properties string (which still requires an information
  // element prefix).
  vector<uint8_t> MakeRSNProperties(uint16_t pairwise_count,
                                    uint16_t authkey_count,
                                    const vector<uint32_t>& ciphers) {
    vector<uint8_t> rsn(IEEE_80211::kRSNIECipherCountOffset +
                        IEEE_80211::kRSNIECipherCountLen * 2 +
                        IEEE_80211::kRSNIESelectorLen *
                            (pairwise_count + authkey_count) +
                        IEEE_80211::kRSNIECapabilitiesLen);

    // Set both cipher counts in little endian.
    rsn[IEEE_80211::kRSNIECipherCountOffset] = pairwise_count & 0xff;
    rsn[IEEE_80211::kRSNIECipherCountOffset + 1] = pairwise_count >> 8;
    size_t authkey_offset = IEEE_80211::kRSNIECipherCountOffset +
                            IEEE_80211::kRSNIECipherCountLen +
                            pairwise_count * IEEE_80211::kRSNIESelectorLen;
    rsn[authkey_offset] = authkey_count & 0xff;
    rsn[authkey_offset + 1] = authkey_count >> 8;

    if (authkey_count > 0 && authkey_count == ciphers.size()) {
      vector<uint8_t>::iterator rsn_authkeys =
          rsn.begin() + authkey_offset + IEEE_80211::kRSNIECipherCountLen;
      const uint8_t* authkeys = reinterpret_cast<const uint8_t*>(&ciphers[0]);
      std::copy(authkeys,
                authkeys + authkey_count * IEEE_80211::kRSNIESelectorLen,
                rsn_authkeys);
    }

    return rsn;
  }

  bool ParseIEs(const KeyValueStore& properties,
                Metrics::WiFiNetworkPhyMode* phy_mode,
                WiFiEndpoint::VendorInformation* vendor_information,
                std::string* country_code,
                WiFiEndpoint::Ap80211krvSupport* krv_support,
                WiFiEndpoint::HS20Information* hs20_information) {
    return WiFiEndpoint::ParseIEs(properties, phy_mode, vendor_information,
                                  country_code, krv_support, hs20_information);
  }

  void SetVendorInformation(
      const WiFiEndpointRefPtr& endpoint,
      const WiFiEndpoint::VendorInformation& vendor_information) {
    endpoint->vendor_information_ = vendor_information;
  }

  WiFiEndpointRefPtr MakeEndpoint(
      ControlInterface* control_interface,
      const WiFiRefPtr& wifi,
      const std::string& ssid,
      const std::string& bssid,
      const WiFiEndpoint::SecurityFlags& security_flags) {
    return WiFiEndpoint::MakeEndpoint(control_interface, wifi, ssid, bssid,
                                      WPASupplicant::kNetworkModeInfrastructure,
                                      0, 0, security_flags);
  }

  WiFiEndpointRefPtr MakeOpenEndpoint(ControlInterface* control_interface,
                                      const WiFiRefPtr& wifi,
                                      const std::string& ssid,
                                      const std::string& bssid) {
    return WiFiEndpoint::MakeOpenEndpoint(
        control_interface, wifi, ssid, bssid,
        WPASupplicant::kNetworkModeInfrastructure, 0, 0);
  }

  scoped_refptr<MockWiFi> wifi() { return wifi_; }

 private:
  MockNetlinkManager netlink_manager_;
  scoped_refptr<MockWiFi> wifi_;
};

TEST_F(WiFiEndpointTest, ParseKeyManagementMethodsEAP) {
  set<WiFiEndpoint::KeyManagement> parsed_methods;
  WiFiEndpoint::ParseKeyManagementMethods(
      MakeKeyManagementArgs({"something-eap"}), &parsed_methods);
  EXPECT_TRUE(
      base::Contains(parsed_methods, WiFiEndpoint::kKeyManagement802_1x));
  EXPECT_FALSE(base::Contains(parsed_methods, WiFiEndpoint::kKeyManagementPSK));
}

TEST_F(WiFiEndpointTest, ParseKeyManagementMethodsPSK) {
  set<WiFiEndpoint::KeyManagement> parsed_methods;
  WiFiEndpoint::ParseKeyManagementMethods(
      MakeKeyManagementArgs({"something-psk"}), &parsed_methods);
  EXPECT_TRUE(base::Contains(parsed_methods, WiFiEndpoint::kKeyManagementPSK));
  EXPECT_FALSE(
      base::Contains(parsed_methods, WiFiEndpoint::kKeyManagement802_1x));
}

TEST_F(WiFiEndpointTest, ParseKeyManagementMethodsEAPAndPSK) {
  set<WiFiEndpoint::KeyManagement> parsed_methods;
  WiFiEndpoint::ParseKeyManagementMethods(
      MakeKeyManagementArgs({"something-eap", "something-psk"}),
      &parsed_methods);
  EXPECT_TRUE(
      base::Contains(parsed_methods, WiFiEndpoint::kKeyManagement802_1x));
  EXPECT_TRUE(base::Contains(parsed_methods, WiFiEndpoint::kKeyManagementPSK));
}

TEST_F(WiFiEndpointTest, ParseSecurityRSN802_1x) {
  EXPECT_STREQ(kSecurity8021x,
               ParseSecurity(MakeSecurityArgs("RSN", "something-eap")));
}

TEST_F(WiFiEndpointTest, ParseSecurityWPA802_1x) {
  EXPECT_STREQ(kSecurity8021x,
               ParseSecurity(MakeSecurityArgs("WPA", "something-eap")));
}

TEST_F(WiFiEndpointTest, ParseSecurityRSNSAE) {
  EXPECT_STREQ(kSecurityWpa3, ParseSecurity(MakeSecurityArgs("RSN", "sae")));
}

TEST_F(WiFiEndpointTest, ParseSecurityRSNPSK) {
  EXPECT_STREQ(kSecurityRsn,
               ParseSecurity(MakeSecurityArgs("RSN", "something-psk")));
}

TEST_F(WiFiEndpointTest, ParseSecurityWPAPSK) {
  EXPECT_STREQ(kSecurityWpa,
               ParseSecurity(MakeSecurityArgs("WPA", "something-psk")));
}

TEST_F(WiFiEndpointTest, ParseSecurityWEP) {
  EXPECT_STREQ(kSecurityWep, ParseSecurity(MakePrivacyArgs(true)));
}

TEST_F(WiFiEndpointTest, ParseSecurityNone) {
  KeyValueStore top_params;
  EXPECT_STREQ(kSecurityNone, ParseSecurity(top_params));
}

TEST_F(WiFiEndpointTest, SSIDAndBSSIDString) {
  const char kSSID[] = "The SSID";
  const char kBSSID[] = "00:01:02:03:04:05";

  // The MakeOpenEndpoint method translates both of the above parameters into
  // binary equivalents before calling the Endpoint constructor.  Let's make
  // sure the Endpoint can translate them back losslessly to strings.
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(nullptr, nullptr, kSSID, kBSSID);
  EXPECT_EQ(kSSID, endpoint->ssid_string());
  EXPECT_EQ(kBSSID, endpoint->bssid_string());
}

TEST_F(WiFiEndpointTest, SSIDWithNull) {
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(nullptr, nullptr, string(1, 0), "00:00:00:00:00:01");
  EXPECT_EQ("?", endpoint->ssid_string());
}

TEST_F(WiFiEndpointTest, DeterminePhyModeFromFrequency) {
  {
    KeyValueStore properties;
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11a,
              WiFiEndpoint::DeterminePhyModeFromFrequency(properties, 3200));
  }
  {
    KeyValueStore properties;
    vector<uint32_t> rates(1, 22000000);
    properties.Set<vector<uint32_t>>(WPASupplicant::kBSSPropertyRates, rates);
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11b,
              WiFiEndpoint::DeterminePhyModeFromFrequency(properties, 2400));
  }
  {
    KeyValueStore properties;
    vector<uint32_t> rates(1, 54000000);
    properties.Set<vector<uint32_t>>(WPASupplicant::kBSSPropertyRates, rates);
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11g,
              WiFiEndpoint::DeterminePhyModeFromFrequency(properties, 2400));
  }
  {
    KeyValueStore properties;
    vector<uint32_t> rates;
    properties.Set<vector<uint32_t>>(WPASupplicant::kBSSPropertyRates, rates);
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11b,
              WiFiEndpoint::DeterminePhyModeFromFrequency(properties, 2400));
  }
}

TEST_F(WiFiEndpointTest, ParseIEs) {
  {
    vector<uint8_t> ies;
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::Ap80211krvSupport krv_support;
    EXPECT_FALSE(ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode,
                          &vendor_information, nullptr, &krv_support, nullptr));
    EXPECT_EQ(Metrics::kWiFiNetworkPhyModeUndef, phy_mode);
    EXPECT_FALSE(krv_support.neighbor_list_supported);
    EXPECT_FALSE(krv_support.ota_ft_supported);
    EXPECT_FALSE(krv_support.otds_ft_supported);
    EXPECT_FALSE(krv_support.dms_supported);
    EXPECT_FALSE(krv_support.bss_max_idle_period_supported);
    EXPECT_FALSE(krv_support.bss_transition_supported);
  }
  {
    vector<uint8_t> ies;
    AddIE(IEEE_80211::kElemIdErp, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    EXPECT_TRUE(ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode,
                         &vendor_information, nullptr, nullptr, nullptr));
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11g, phy_mode);
  }
  {
    vector<uint8_t> ies;
    AddIE(IEEE_80211::kElemIdHTCap, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    EXPECT_TRUE(ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode,
                         &vendor_information, nullptr, nullptr, nullptr));
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11n, phy_mode);
  }
  {
    vector<uint8_t> ies;
    AddIE(IEEE_80211::kElemIdHTInfo, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    EXPECT_TRUE(ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode,
                         &vendor_information, nullptr, nullptr, nullptr));
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11n, phy_mode);
  }
  {
    vector<uint8_t> ies;
    AddIE(IEEE_80211::kElemIdErp, &ies);
    AddIE(IEEE_80211::kElemIdHTCap, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    EXPECT_TRUE(ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode,
                         &vendor_information, nullptr, nullptr, nullptr));
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11n, phy_mode);
  }
  {
    vector<uint8_t> ies;
    AddIE(IEEE_80211::kElemIdVHTCap, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    EXPECT_TRUE(ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode,
                         &vendor_information, nullptr, nullptr, nullptr));
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11ac, phy_mode);
  }
  {
    vector<uint8_t> ies;
    AddIE(IEEE_80211::kElemIdVHTOperation, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    EXPECT_TRUE(ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode,
                         &vendor_information, nullptr, nullptr, nullptr));
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11ac, phy_mode);
  }
  {
    vector<uint8_t> ies;
    AddIE(IEEE_80211::kElemIdErp, &ies);
    AddIE(IEEE_80211::kElemIdHTCap, &ies);
    AddIE(IEEE_80211::kElemIdVHTCap, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    EXPECT_TRUE(ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode,
                         &vendor_information, nullptr, nullptr, nullptr));
    EXPECT_EQ(Metrics::kWiFiNetworkPhyMode11ac, phy_mode);
  }
  {
    vector<uint8_t> ies;
    const vector<uint8_t> kRmEnabledCap(5, 0);
    const string kCountryCode("GO");
    const vector<uint8_t> kCountryCodeAsVector(kCountryCode.begin(),
                                               kCountryCode.end());
    AddIE(IEEE_80211::kElemIdPowerConstraint, &ies);
    AddIEWithData(IEEE_80211::kElemIdRmEnabledCap, kRmEnabledCap, &ies);
    AddIEWithData(IEEE_80211::kElemIdCountry, kCountryCodeAsVector, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::Ap80211krvSupport krv_support;
    string country_code;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             &country_code, &krv_support, nullptr);
    EXPECT_TRUE(krv_support.neighbor_list_supported);
  }
  {
    vector<uint8_t> ies;
    const vector<uint8_t> kMDE{0x00, 0x00, 0x01};
    vector<uint32_t> authkeys(4, 0);
    authkeys[3] = IEEE_80211::kRSNAuthType8021XFT;
    vector<uint8_t> rsn = MakeRSNProperties(1, 4, authkeys);
    AddIEWithData(IEEE_80211::kElemIdRSN, rsn, &ies);
    AddIEWithData(IEEE_80211::kElemIdMDE, kMDE, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::Ap80211krvSupport krv_support;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, &krv_support, nullptr);
    EXPECT_TRUE(krv_support.ota_ft_supported);
    EXPECT_TRUE(krv_support.otds_ft_supported);
  }
  {
    vector<uint8_t> ies;
    const vector<uint8_t> kExtendedCapabilities{0x00, 0x00, 0x08, 0x04};
    AddIEWithData(IEEE_80211::kElemIdExtendedCap, kExtendedCapabilities, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::Ap80211krvSupport krv_support;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, &krv_support, nullptr);
    EXPECT_TRUE(krv_support.dms_supported);
    EXPECT_TRUE(krv_support.bss_transition_supported);
  }
  {
    vector<uint8_t> ies;
    const vector<uint8_t> kBSSMaxIdlePeriod(3, 0);
    AddIEWithData(IEEE_80211::kElemIdBSSMaxIdlePeriod, kBSSMaxIdlePeriod, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::Ap80211krvSupport krv_support;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, &krv_support, nullptr);
    EXPECT_TRUE(krv_support.bss_max_idle_period_supported);
  }
}

TEST_F(WiFiEndpointTest, ParseVendorIEs) {
  {
    ScopedMockLog log;
    EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                         HasSubstr("no room in IE for OUI and type field.")))
        .Times(1);
    vector<uint8_t> ies;
    AddIE(IEEE_80211::kElemIdVendor, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, nullptr);
  }
  {
    vector<uint8_t> ies;
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, nullptr);
    EXPECT_EQ("", vendor_information.wps_manufacturer);
    EXPECT_EQ("", vendor_information.wps_model_name);
    EXPECT_EQ("", vendor_information.wps_model_number);
    EXPECT_EQ("", vendor_information.wps_device_name);
    EXPECT_EQ(0, vendor_information.oui_set.size());
  }
  {
    ScopedMockLog log;
    EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                         HasSubstr("IE extends past containing PDU")))
        .Times(1);
    vector<uint8_t> ies;
    AddVendorIE(0, 0, vector<uint8_t>(), &ies);
    ies.resize(ies.size() - 1);  // Cause an underrun in the data.
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, nullptr);
  }
  {
    vector<uint8_t> ies;
    const uint32_t kVendorOUI = 0xaabbcc;
    AddVendorIE(kVendorOUI, 0, vector<uint8_t>(), &ies);
    AddVendorIE(IEEE_80211::kOUIVendorMicrosoft, 0, vector<uint8_t>(), &ies);
    AddVendorIE(IEEE_80211::kOUIVendorEpigram, 0, vector<uint8_t>(), &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, nullptr);
    EXPECT_EQ("", vendor_information.wps_manufacturer);
    EXPECT_EQ("", vendor_information.wps_model_name);
    EXPECT_EQ("", vendor_information.wps_model_number);
    EXPECT_EQ("", vendor_information.wps_device_name);
    EXPECT_EQ(1, vendor_information.oui_set.size());
    EXPECT_FALSE(vendor_information.oui_set.find(kVendorOUI) ==
                 vendor_information.oui_set.end());

    WiFiEndpointRefPtr endpoint =
        MakeOpenEndpoint(nullptr, nullptr, string(1, 0), "00:00:00:00:00:01");
    SetVendorInformation(endpoint, vendor_information);
    map<string, string> vendor_stringmap(endpoint->GetVendorInformation());
    EXPECT_FALSE(
        base::Contains(vendor_stringmap, kVendorWPSManufacturerProperty));
    EXPECT_FALSE(base::Contains(vendor_stringmap, kVendorWPSModelNameProperty));
    EXPECT_FALSE(
        base::Contains(vendor_stringmap, kVendorWPSModelNumberProperty));
    EXPECT_FALSE(
        base::Contains(vendor_stringmap, kVendorWPSDeviceNameProperty));
    EXPECT_EQ("aa-bb-cc", vendor_stringmap[kVendorOUIListProperty]);
  }
  {
    ScopedMockLog log;
    EXPECT_CALL(log, Log(logging::LOGGING_ERROR, _,
                         HasSubstr("WPS element extends past containing PDU")))
        .Times(1);
    vector<uint8_t> ies;
    vector<uint8_t> wps;
    AddWPSElement(IEEE_80211::kWPSElementManufacturer, "foo", &wps);
    wps.resize(wps.size() - 1);  // Cause an underrun in the data.
    AddVendorIE(IEEE_80211::kOUIVendorMicrosoft, IEEE_80211::kOUIMicrosoftWPS,
                wps, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, nullptr);
    EXPECT_EQ("", vendor_information.wps_manufacturer);
  }
  {
    vector<uint8_t> ies;
    vector<uint8_t> wps;
    const string kManufacturer("manufacturer");
    const string kModelName("modelname");
    const string kModelNumber("modelnumber");
    const string kDeviceName("devicename");
    AddWPSElement(IEEE_80211::kWPSElementManufacturer, kManufacturer, &wps);
    AddWPSElement(IEEE_80211::kWPSElementModelName, kModelName, &wps);
    AddWPSElement(IEEE_80211::kWPSElementModelNumber, kModelNumber, &wps);
    AddWPSElement(IEEE_80211::kWPSElementDeviceName, kDeviceName, &wps);
    AddVendorIE(IEEE_80211::kOUIVendorMicrosoft, IEEE_80211::kOUIMicrosoftWPS,
                wps, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, nullptr);
    EXPECT_EQ(kManufacturer, vendor_information.wps_manufacturer);
    EXPECT_EQ(kModelName, vendor_information.wps_model_name);
    EXPECT_EQ(kModelNumber, vendor_information.wps_model_number);
    EXPECT_EQ(kDeviceName, vendor_information.wps_device_name);

    WiFiEndpointRefPtr endpoint =
        MakeOpenEndpoint(nullptr, nullptr, string(1, 0), "00:00:00:00:00:01");
    SetVendorInformation(endpoint, vendor_information);
    map<string, string> vendor_stringmap(endpoint->GetVendorInformation());
    EXPECT_EQ(kManufacturer, vendor_stringmap[kVendorWPSManufacturerProperty]);
    EXPECT_EQ(kModelName, vendor_stringmap[kVendorWPSModelNameProperty]);
    EXPECT_EQ(kModelNumber, vendor_stringmap[kVendorWPSModelNumberProperty]);
    EXPECT_EQ(kDeviceName, vendor_stringmap[kVendorWPSDeviceNameProperty]);
    EXPECT_FALSE(base::Contains(vendor_stringmap, kVendorOUIListProperty));
  }
  {
    vector<uint8_t> ies;
    vector<uint8_t> wps;
    const string kManufacturer("manufacturer");
    const string kModelName("modelname");
    AddWPSElement(IEEE_80211::kWPSElementManufacturer, kManufacturer, &wps);
    wps.resize(wps.size() - 1);  // Insert a non-ASCII character in the WPS.
    wps.push_back(0x80);
    AddWPSElement(IEEE_80211::kWPSElementModelName, kModelName, &wps);
    AddVendorIE(IEEE_80211::kOUIVendorMicrosoft, IEEE_80211::kOUIMicrosoftWPS,
                wps, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, nullptr);
    EXPECT_EQ("", vendor_information.wps_manufacturer);
    EXPECT_EQ(kModelName, vendor_information.wps_model_name);
  }
  {
    vector<uint8_t> ies;
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::HS20Information hs20_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, &hs20_information);
    EXPECT_FALSE(hs20_information.supported);
  }
  {
    vector<uint8_t> ies;
    AddVendorIE(IEEE_80211::kOUIVendorWiFiAlliance,
                IEEE_80211::kOUITypeWiFiAllianceHS20Indicator,
                vector<uint8_t>(), &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::HS20Information hs20_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, &hs20_information);
    EXPECT_FALSE(hs20_information.supported);
  }
  {
    vector<uint8_t> ies;
    vector<uint8_t> data = {0x20};
    AddVendorIE(IEEE_80211::kOUIVendorWiFiAlliance,
                IEEE_80211::kOUITypeWiFiAllianceHS20Indicator, data, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::HS20Information hs20_information;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, nullptr, &hs20_information);
    EXPECT_TRUE(hs20_information.supported);
    EXPECT_EQ(2, hs20_information.version);
  }
}

TEST_F(WiFiEndpointTest, ParseWPACapabilities) {
  {
    vector<uint8_t> ies;
    vector<uint32_t> authkeys(4, 0);
    authkeys[3] = IEEE_80211::kRSNAuthType8021XFT;
    vector<uint8_t> rsn = MakeRSNProperties(1, 4, authkeys);
    AddIEWithData(IEEE_80211::kElemIdRSN, rsn, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::Ap80211krvSupport krv_support;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, &krv_support, nullptr);
    EXPECT_FALSE(krv_support.ota_ft_supported);
    EXPECT_FALSE(krv_support.otds_ft_supported);
  }
  {
    vector<uint8_t> ies;
    vector<uint32_t> authkeys(3, 0);
    authkeys[0] = IEEE_80211::kRSNAuthTypeSAEFT;
    authkeys[1] = IEEE_80211::kRSNAuthTypePSKFT;
    vector<uint8_t> rsn = MakeRSNProperties(4, 3, authkeys);
    AddIEWithData(IEEE_80211::kElemIdRSN, rsn, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::Ap80211krvSupport krv_support;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, &krv_support, nullptr);
    EXPECT_FALSE(krv_support.ota_ft_supported);
    EXPECT_FALSE(krv_support.otds_ft_supported);
  }
  {
    vector<uint8_t> ies;
    vector<uint32_t> authkeys(1, 4);
    vector<uint8_t> rsn = MakeRSNProperties(2, 4, authkeys);
    AddIEWithData(IEEE_80211::kElemIdRSN, rsn, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    WiFiEndpoint::Ap80211krvSupport krv_support;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             nullptr, &krv_support, nullptr);
    EXPECT_FALSE(krv_support.ota_ft_supported);
    EXPECT_FALSE(krv_support.otds_ft_supported);
  }
}

TEST_F(WiFiEndpointTest, ParseCountryCode) {
  {
    vector<uint8_t> ies;
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    string country_code;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             &country_code, nullptr, nullptr);
    EXPECT_TRUE(country_code.empty());
  }
  {
    const string kCountryCode("G");
    const vector<uint8_t> kCountryCodeAsVector(kCountryCode.begin(),
                                               kCountryCode.end());
    vector<uint8_t> ies;
    AddIEWithData(IEEE_80211::kElemIdCountry, kCountryCodeAsVector, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    string country_code;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             &country_code, nullptr, nullptr);
    EXPECT_TRUE(country_code.empty());
  }
  {
    const string kCountryCode("GO");
    const vector<uint8_t> kCountryCodeAsVector(kCountryCode.begin(),
                                               kCountryCode.end());
    vector<uint8_t> ies;
    AddIEWithData(IEEE_80211::kElemIdCountry, kCountryCodeAsVector, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    string country_code;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             &country_code, nullptr, nullptr);
    EXPECT_EQ(kCountryCode, country_code);
  }
  {
    const string kCountryCode("GOO");
    const vector<uint8_t> kCountryCodeAsVector(kCountryCode.begin(),
                                               kCountryCode.end());
    vector<uint8_t> ies;
    AddIEWithData(IEEE_80211::kElemIdCountry, kCountryCodeAsVector, &ies);
    Metrics::WiFiNetworkPhyMode phy_mode = Metrics::kWiFiNetworkPhyModeUndef;
    WiFiEndpoint::VendorInformation vendor_information;
    string country_code;
    ParseIEs(MakeBSSPropertiesWithIEs(ies), &phy_mode, &vendor_information,
             &country_code, nullptr, nullptr);
    EXPECT_EQ(string(kCountryCode, 0, 2), country_code);
  }
}

TEST_F(WiFiEndpointTest, PropertiesChangedNone) {
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01");
  EXPECT_EQ(kModeManaged, endpoint->network_mode());
  EXPECT_EQ(kSecurityNone, endpoint->security_mode());
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(0);
  KeyValueStore no_changed_properties;
  endpoint->PropertiesChanged(no_changed_properties);
  EXPECT_EQ(kModeManaged, endpoint->network_mode());
  EXPECT_EQ(kSecurityNone, endpoint->security_mode());
}

TEST_F(WiFiEndpointTest, PropertiesChangedStrength) {
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01");
  KeyValueStore changed_properties;
  int16_t signal_strength = 10;

  EXPECT_NE(signal_strength, endpoint->signal_strength());
  changed_properties.Set<int16_t>(WPASupplicant::kBSSPropertySignal,
                                  signal_strength);

  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_));
  endpoint->PropertiesChanged(changed_properties);
  EXPECT_EQ(signal_strength, endpoint->signal_strength());
}

TEST_F(WiFiEndpointTest, PropertiesChangedNetworkMode) {
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01");
  EXPECT_EQ(kModeManaged, endpoint->network_mode());
  // AdHoc mode is not supported. Mode should not change.
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(0);
  KeyValueStore changed_properties;
  changed_properties.Set<string>(WPASupplicant::kBSSPropertyMode,
                                 WPASupplicant::kNetworkModeAdHoc);
  endpoint->PropertiesChanged(changed_properties);
  EXPECT_EQ(kModeManaged, endpoint->network_mode());
}

TEST_F(WiFiEndpointTest, PropertiesChangedFrequency) {
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01");
  KeyValueStore changed_properties;
  uint16_t frequency = 2412;

  EXPECT_NE(frequency, endpoint->frequency());
  changed_properties.Set<uint16_t>(WPASupplicant::kBSSPropertyFrequency,
                                   frequency);

  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_));
  endpoint->PropertiesChanged(changed_properties);
  EXPECT_EQ(frequency, endpoint->frequency());
}

TEST_F(WiFiEndpointTest, PropertiesChangedSecurityMode) {
  WiFiEndpointRefPtr endpoint =
      MakeOpenEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01");
  EXPECT_EQ(kSecurityNone, endpoint->security_mode());

  // Upgrade to WEP if privacy flag is added.
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(1);
  endpoint->PropertiesChanged(MakePrivacyArgs(true));
  Mock::VerifyAndClearExpectations(wifi().get());
  EXPECT_EQ(kSecurityWep, endpoint->security_mode());

  // Make sure we don't downgrade if no interesting arguments arrive.
  KeyValueStore no_changed_properties;
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(0);
  endpoint->PropertiesChanged(no_changed_properties);
  Mock::VerifyAndClearExpectations(wifi().get());
  EXPECT_EQ(kSecurityWep, endpoint->security_mode());

  // Another upgrade to 802.1x.
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(1);
  endpoint->PropertiesChanged(MakeSecurityArgs("RSN", "something-eap"));
  Mock::VerifyAndClearExpectations(wifi().get());
  EXPECT_EQ(kSecurity8021x, endpoint->security_mode());

  // Add WPA-PSK, however this is trumped by RSN 802.1x above, so we don't
  // change our security nor do we notify anyone.
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(0);
  endpoint->PropertiesChanged(MakeSecurityArgs("WPA", "something-psk"));
  Mock::VerifyAndClearExpectations(wifi().get());
  EXPECT_EQ(kSecurity8021x, endpoint->security_mode());

  // If nothing changes, we should stay the same.
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(0);
  endpoint->PropertiesChanged(no_changed_properties);
  Mock::VerifyAndClearExpectations(wifi().get());
  EXPECT_EQ(kSecurity8021x, endpoint->security_mode());

  // However, if the BSS updates to no longer support 802.1x, we degrade
  // to WPA.
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(1);
  endpoint->PropertiesChanged(MakeSecurityArgs("RSN", ""));
  Mock::VerifyAndClearExpectations(wifi().get());
  EXPECT_EQ(kSecurityWpa, endpoint->security_mode());

  // Losing WPA brings us back to WEP (since the privacy flag hasn't changed).
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(1);
  endpoint->PropertiesChanged(MakeSecurityArgs("WPA", ""));
  Mock::VerifyAndClearExpectations(wifi().get());
  EXPECT_EQ(kSecurityWep, endpoint->security_mode());

  // From WEP to open security.
  EXPECT_CALL(*wifi(), NotifyEndpointChanged(_)).Times(1);
  endpoint->PropertiesChanged(MakePrivacyArgs(false));
  Mock::VerifyAndClearExpectations(wifi().get());
  EXPECT_EQ(kSecurityNone, endpoint->security_mode());
}

TEST_F(WiFiEndpointTest, HasRsnWpaProperties) {
  {
    WiFiEndpointRefPtr endpoint =
        MakeEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01",
                     WiFiEndpoint::SecurityFlags());
    EXPECT_FALSE(endpoint->has_wpa_property());
    EXPECT_FALSE(endpoint->has_rsn_property());
  }
  {
    WiFiEndpoint::SecurityFlags flags;
    flags.wpa_psk = true;
    WiFiEndpointRefPtr endpoint =
        MakeEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01", flags);
    EXPECT_TRUE(endpoint->has_wpa_property());
    EXPECT_FALSE(endpoint->has_rsn_property());
  }
  {
    WiFiEndpoint::SecurityFlags flags;
    flags.rsn_8021x = true;
    WiFiEndpointRefPtr endpoint =
        MakeEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01", flags);
    EXPECT_FALSE(endpoint->has_wpa_property());
    EXPECT_TRUE(endpoint->has_rsn_property());
  }
  {
    WiFiEndpoint::SecurityFlags flags;
    // Both can be true.
    flags.wpa_psk = true;
    flags.rsn_psk = true;
    WiFiEndpointRefPtr endpoint =
        MakeEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01", flags);
    EXPECT_TRUE(endpoint->has_wpa_property());
    EXPECT_TRUE(endpoint->has_rsn_property());
  }
  {
    WiFiEndpoint::SecurityFlags flags;
    flags.rsn_sae = true;
    WiFiEndpointRefPtr endpoint =
        MakeEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01", flags);
    EXPECT_FALSE(endpoint->has_wpa_property());
    EXPECT_TRUE(endpoint->has_rsn_property());
  }
}

TEST_F(WiFiEndpointTest, HasTetheringSignature) {
  {
    WiFiEndpointRefPtr endpoint =
        MakeEndpoint(nullptr, wifi(), "ssid", "02:1a:11:00:00:01",
                     WiFiEndpoint::SecurityFlags());
    EXPECT_TRUE(endpoint->has_tethering_signature());
  }
  {
    WiFiEndpointRefPtr endpoint =
        MakeEndpoint(nullptr, wifi(), "ssid", "02:1a:10:00:00:01",
                     WiFiEndpoint::SecurityFlags());
    EXPECT_FALSE(endpoint->has_tethering_signature());
    endpoint->vendor_information_.oui_set.insert(Tethering::kIosOui);
    endpoint->CheckForTetheringSignature();
    EXPECT_TRUE(endpoint->has_tethering_signature());
  }
  {
    WiFiEndpointRefPtr endpoint =
        MakeEndpoint(nullptr, wifi(), "ssid", "04:1a:10:00:00:01",
                     WiFiEndpoint::SecurityFlags());
    EXPECT_FALSE(endpoint->has_tethering_signature());
    endpoint->vendor_information_.oui_set.insert(Tethering::kIosOui);
    endpoint->CheckForTetheringSignature();
    EXPECT_FALSE(endpoint->has_tethering_signature());
  }
}

TEST_F(WiFiEndpointTest, Ap80211krvSupported) {
  WiFiEndpointRefPtr endpoint =
      MakeEndpoint(nullptr, wifi(), "ssid", "00:00:00:00:00:01",
                   WiFiEndpoint::SecurityFlags());
  EXPECT_FALSE(endpoint->krv_support().neighbor_list_supported);
  endpoint->krv_support_.neighbor_list_supported = true;
  EXPECT_TRUE(endpoint->krv_support().neighbor_list_supported);

  EXPECT_FALSE(endpoint->krv_support().ota_ft_supported);
  endpoint->krv_support_.ota_ft_supported = true;
  EXPECT_TRUE(endpoint->krv_support().ota_ft_supported);

  EXPECT_FALSE(endpoint->krv_support().otds_ft_supported);
  endpoint->krv_support_.otds_ft_supported = true;
  EXPECT_TRUE(endpoint->krv_support().otds_ft_supported);

  EXPECT_FALSE(endpoint->krv_support().dms_supported);
  endpoint->krv_support_.dms_supported = true;
  EXPECT_TRUE(endpoint->krv_support().dms_supported);

  EXPECT_FALSE(endpoint->krv_support().bss_max_idle_period_supported);
  endpoint->krv_support_.bss_max_idle_period_supported = true;
  EXPECT_TRUE(endpoint->krv_support().bss_max_idle_period_supported);

  EXPECT_FALSE(endpoint->krv_support().bss_transition_supported);
  endpoint->krv_support_.bss_transition_supported = true;
  EXPECT_TRUE(endpoint->krv_support().bss_transition_supported);
}

}  // namespace shill
