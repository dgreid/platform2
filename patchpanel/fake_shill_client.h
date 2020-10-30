// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_FAKE_SHILL_CLIENT_H_
#define PATCHPANEL_FAKE_SHILL_CLIENT_H_

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/memory/ref_counted.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/shill_client.h"

using testing::_;
using testing::AnyNumber;
using testing::Return;

namespace patchpanel {

class FakeShillClient : public ShillClient {
 public:
  explicit FakeShillClient(scoped_refptr<dbus::Bus> bus) : ShillClient(bus) {}

  Device GetDefaultDevice() override {
    if (fake_default_ifname_.empty())
      return {};
    return {.type = Device::Type::kUnknown, .ifname = fake_default_ifname_};
  }
  const std::string& default_interface() const override {
    return fake_default_ifname_;
  }

  void SetFakeDefaultDevice(const std::string& ifname) {
    fake_default_ifname_ = ifname;
  }

  void NotifyManagerPropertyChange(const std::string& name,
                                   const brillo::Any& value) {
    OnManagerPropertyChange(name, value);
  }

  void NotifyDevicePropertyChange(const std::string& device,
                                  const std::string& name,
                                  const brillo::Any& value) {
    OnDevicePropertyChange(device, name, value);
  }

  bool GetDeviceProperties(const std::string& device, Device* output) override {
    get_device_properties_calls_.insert(device);
    return true;
  }

  const std::set<std::string>& get_device_properties_calls() {
    return get_device_properties_calls_;
  }

 private:
  std::string fake_default_ifname_;
  std::set<std::string> get_device_properties_calls_;
};

class FakeShillClientHelper {
 public:
  FakeShillClientHelper() {
    mock_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), "org.chromium.flimflam", dbus::ObjectPath("/path"));
    // Set these expectations rather than just ignoring them to confirm
    // the ShillClient obtains the expected proxy and registers for
    // property changes.
    EXPECT_CALL(*mock_bus_, GetObjectProxy("org.chromium.flimflam", _))
        .WillRepeatedly(Return(mock_proxy_.get()));
    EXPECT_CALL(*mock_proxy_, DoConnectToSignal("org.chromium.flimflam.Manager",
                                                "PropertyChanged", _, _))
        .Times(AnyNumber());
    EXPECT_CALL(*mock_proxy_, DoConnectToSignal("org.chromium.flimflam.Device",
                                                "PropertyChanged", _, _))
        .Times(AnyNumber());

    client_ = std::make_unique<FakeShillClient>(mock_bus_);
  }

  std::unique_ptr<ShillClient> Client() { return std::move(client_); }

  std::unique_ptr<FakeShillClient> FakeClient() { return std::move(client_); }

  dbus::MockObjectProxy* mock_proxy() { return mock_proxy_.get(); }

 private:
  scoped_refptr<dbus::MockBus> mock_bus_{
      new dbus::MockBus{dbus::Bus::Options{}}};
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;

  std::unique_ptr<FakeShillClient> client_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_FAKE_SHILL_CLIENT_H_
