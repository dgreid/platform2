// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_WIFI_PROVIDER_H_
#define SHILL_MOCK_WIFI_PROVIDER_H_

#include <gmock/gmock.h>

#include "shill/wifi_endpoint.h"
#include "shill/wifi_provider.h"

namespace shill {

class MockWiFiProvider : public WiFiProvider {
 public:
  MockWiFiProvider();
  ~MockWiFiProvider() override;

  MOCK_METHOD0(Start, void());
  MOCK_METHOD0(Stop, void());
  MOCK_METHOD1(CreateServicesFromProfile, void(const ProfileRefPtr &profile));
  MOCK_CONST_METHOD2(FindSimilarService,
                     ServiceRefPtr(const KeyValueStore &args,
                                   Error *error));
  MOCK_METHOD2(CreateTemporaryService,
               ServiceRefPtr(const KeyValueStore &args, Error *error));
  MOCK_METHOD2(GetService, ServiceRefPtr(const KeyValueStore &args,
                                         Error *error));
  MOCK_METHOD1(FindServiceForEndpoint,
               WiFiServiceRefPtr(const WiFiEndpointConstRefPtr &endpoint));
  MOCK_METHOD1(OnEndpointAdded, void(const WiFiEndpointConstRefPtr &endpoint));
  MOCK_METHOD1(OnEndpointRemoved,
               WiFiServiceRefPtr(const WiFiEndpointConstRefPtr &endpoint));
  MOCK_METHOD1(OnEndpointUpdated,
               void(const WiFiEndpointConstRefPtr &endpoint));
  MOCK_METHOD1(OnServiceUnloaded, bool(const WiFiServiceRefPtr &service));
  MOCK_METHOD0(GetHiddenSSIDList, ByteArrays());
  MOCK_METHOD1(LoadAndFixupServiceEntries, void(Profile *storage));
  MOCK_CONST_METHOD1(Save, bool(StoreInterface *storage));
  MOCK_METHOD1(IncrementConnectCount, void(uint16_t frequency));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockWiFiProvider);
};

}  // namespace shill

#endif  // SHILL_MOCK_WIFI_PROVIDER_H_
