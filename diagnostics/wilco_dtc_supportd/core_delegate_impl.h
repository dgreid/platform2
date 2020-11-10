// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_CORE_DELEGATE_IMPL_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_CORE_DELEGATE_IMPL_H_

#include <memory>

#include <base/macros.h>

#include "diagnostics/common/system/bluetooth_client.h"
#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/common/system/powerd_adapter.h"
#include "diagnostics/wilco_dtc_supportd/core.h"
#include "diagnostics/wilco_dtc_supportd/probe_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/bluetooth_event_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/ec_service.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/powerd_event_service.h"

namespace diagnostics {

// Production implementation of Core's delegate.
class CoreDelegateImpl final : public Core::Delegate {
 public:
  CoreDelegateImpl();
  CoreDelegateImpl(const CoreDelegateImpl&) = delete;
  CoreDelegateImpl& operator=(const CoreDelegateImpl&) = delete;

  ~CoreDelegateImpl() override;

  // Core::Delegate overrides:
  std::unique_ptr<BluetoothClient> CreateBluetoothClient(
      const scoped_refptr<dbus::Bus>& bus) override;
  std::unique_ptr<DebugdAdapter> CreateDebugdAdapter(
      const scoped_refptr<dbus::Bus>& bus) override;
  std::unique_ptr<PowerdAdapter> CreatePowerdAdapter(
      const scoped_refptr<dbus::Bus>& bus) override;
  std::unique_ptr<BluetoothEventService> CreateBluetoothEventService(
      BluetoothClient* bluetooth_client) override;
  std::unique_ptr<EcService> CreateEcService() override;
  std::unique_ptr<PowerdEventService> CreatePowerdEventService(
      PowerdAdapter* powerd_adapter) override;
  std::unique_ptr<ProbeService> CreateProbeService(
      ProbeService::Delegate* delegate) override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_CORE_DELEGATE_IMPL_H_
