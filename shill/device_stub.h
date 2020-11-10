// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DEVICE_STUB_H_
#define SHILL_DEVICE_STUB_H_

#include <base/memory/ref_counted.h>

#include <string>

#include "shill/device.h"
#include "shill/event_dispatcher.h"
#include "shill/service.h"

namespace shill {

// Non-functional Device subclass used for non-operable or blocked devices
class DeviceStub : public Device {
 public:
  DeviceStub(Manager* manager,
             const std::string& link_name,
             const std::string& address,
             int interface_index,
             Technology technology)
      : Device(manager, link_name, address, interface_index, technology) {}
  DeviceStub(const DeviceStub&) = delete;
  DeviceStub& operator=(const DeviceStub&) = delete;

  void Start(Error* /*error*/,
             const EnabledStateChangedCallback& /*callback*/) override {}
  void Stop(Error* /*error*/,
            const EnabledStateChangedCallback& /*callback*/) override {}
  void Initialize() override {}

  void OnIPConfigUpdated(const IPConfigRefPtr& /*ipconfig*/,
                         bool /*new_lease_acquired*/) override {}
  void OnIPv6ConfigUpdated() override {}
};

}  // namespace shill

#endif  // SHILL_DEVICE_STUB_H_
