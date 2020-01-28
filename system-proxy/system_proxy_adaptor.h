// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_SYSTEM_PROXY_ADAPTOR_H_
#define SYSTEM_PROXY_SYSTEM_PROXY_ADAPTOR_H_

#include <memory>
#include <vector>

#include <brillo/dbus/async_event_sequencer.h>

#include "system_proxy/org.chromium.SystemProxy.h"

namespace brillo {
namespace dbus_utils {
class DBusObject;
}

}  // namespace brillo

namespace system_proxy {
// Implementation of the SystemProxy D-Bus interface.
class SystemProxyAdaptor : public org::chromium::SystemProxyAdaptor,
                           public org::chromium::SystemProxyInterface {
 public:
  explicit SystemProxyAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object);
  SystemProxyAdaptor(const SystemProxyAdaptor&) = delete;
  SystemProxyAdaptor& operator=(const SystemProxyAdaptor&) = delete;
  ~SystemProxyAdaptor();

  // Registers the D-Bus object and interfaces.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
          completion_callback);

  // org::chromium::SystemProxyInterface: (see org.chromium.SystemProxy.xml).
  std::vector<uint8_t> SetSystemTrafficCredentials(
      const std::vector<uint8_t>& request_blob) override;
  std::vector<uint8_t> ShutDown() override;

 private:
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace system_proxy
#endif  // SYSTEM_PROXY_SYSTEM_PROXY_ADAPTOR_H_
