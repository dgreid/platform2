// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_DBUS_PROPERTIES_PROXY_H_
#define SHILL_MOCK_DBUS_PROPERTIES_PROXY_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/dbus_properties_proxy_interface.h"

namespace shill {

class MockDBusPropertiesProxy : public DBusPropertiesProxyInterface {
 public:
  MockDBusPropertiesProxy();
  MockDBusPropertiesProxy(const MockDBusPropertiesProxy&) = delete;
  MockDBusPropertiesProxy& operator=(const MockDBusPropertiesProxy&) = delete;

  ~MockDBusPropertiesProxy() override;

  MOCK_METHOD(KeyValueStore, GetAll, (const std::string&), (override));
  MOCK_METHOD(void,
              GetAllAsync,
              (const std::string&,
               const base::Callback<void(const KeyValueStore&)>&,
               const base::Callback<void(const Error&)>&),
              (override));
  MOCK_METHOD(brillo::Any,
              Get,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(void,
              GetAsync,
              (const std::string&,
               const std::string&,
               const base::Callback<void(const brillo::Any&)>&,
               const base::Callback<void(const Error&)>&),
              (override));
  MOCK_METHOD(void,
              set_properties_changed_callback,
              (const PropertiesChangedCallback&),
              (override));
  MOCK_METHOD(void,
              set_modem_manager_properties_changed_callback,
              (const ModemManagerPropertiesChangedCallback&),
              (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_DBUS_PROPERTIES_PROXY_H_
