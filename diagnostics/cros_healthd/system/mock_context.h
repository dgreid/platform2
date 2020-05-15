// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_CONTEXT_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_CONTEXT_H_

#include <base/memory/scoped_refptr.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>

#include "debugd/dbus-proxy-mocks.h"
#include "diagnostics/common/system/fake_bluetooth_client.h"
#include "diagnostics/common/system/fake_powerd_adapter.h"
#include "diagnostics/common/system/mock_debugd_adapter.h"
#include "diagnostics/cros_healthd/system/context.h"

namespace diagnostics {

// A mock context class for testing.
class MockContext final : public Context {
 public:
  MockContext();
  MockContext(const MockContext&) = delete;
  MockContext& operator=(const MockContext&) = delete;
  ~MockContext() override;

  // Context overrides:
  bool Initialize() override;

  // Accessors to the fake and mock objects held by MockContext:
  FakeBluetoothClient* fake_bluetooth_client() const;
  org::chromium::debugdProxyMock* mock_debugd_proxy() const;
  MockDebugdAdapter* mock_debugd_adapter() const;
  dbus::MockObjectProxy* mock_power_manager_proxy() const;
  FakePowerdAdapter* fake_powerd_adapter() const;
  brillo::FakeCrosConfig* fake_cros_config() const;

 private:
  // Used to create a mock power manager proxy.
  dbus::Bus::Options options_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_power_manager_proxy_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOCK_CONTEXT_H_
