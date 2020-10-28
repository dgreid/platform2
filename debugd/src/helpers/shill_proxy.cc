// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/helpers/shill_proxy.h"

#include <chromeos/dbus/service_constants.h>

namespace debugd {

std::unique_ptr<ShillProxy> ShillProxy::Create() {
  scoped_refptr<dbus::Bus> bus = ConnectToSystemBus();
  if (!bus)
    return nullptr;

  return std::unique_ptr<ShillProxy>(new ShillProxy(bus));
}

ShillProxy::ShillProxy(scoped_refptr<dbus::Bus> bus)
    : SystemServiceProxy(bus, shill::kFlimflamServiceName) {}

base::Optional<base::Value> ShillProxy::GetProperties(
    const std::string& interface_name, const dbus::ObjectPath& object_path) {
  dbus::MethodCall method_call(interface_name, shill::kGetPropertiesFunction);
  return CallMethodAndGetResponse(object_path, &method_call);
}

}  // namespace debugd
