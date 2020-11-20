// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/logging.h>
#include <base/strings/string_util.h>

#include "shill/net/io_handler.h"
#include "shill/supplicant/wpa_supplicant.h"
#include "shill/wifi/wifi_endpoint.h"

namespace shill {

class WiFiIEsFuzz {
 public:
  static void Run(const uint8_t* data, size_t size) {
    std::vector<uint8_t> ies(data, data + size);
    KeyValueStore properties;
    properties.Set(WPASupplicant::kBSSPropertyIEs, ies);

    Metrics::WiFiNetworkPhyMode phy_mode;
    WiFiEndpoint::VendorInformation vendor_information;
    std::string country_code;
    WiFiEndpoint::Ap80211krvSupport krv_support;
    WiFiEndpoint::HS20Information hs20_information;

    WiFiEndpoint::ParseIEs(properties, &phy_mode, &vendor_information,
                           &country_code, &krv_support, &hs20_information);

    // D-Bus wants our strings UTF-8, and ISO 3166 says they should be ASCII.
    CHECK(base::IsStringASCII(country_code));
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Turn off logging.
  logging::SetMinLogLevel(logging::LOGGING_FATAL);

  WiFiIEsFuzz::Run(data, size);
  return 0;
}

}  // namespace shill
