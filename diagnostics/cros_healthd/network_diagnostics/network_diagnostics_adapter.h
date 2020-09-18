// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_H_

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "mojo/network_diagnostics.mojom.h"

namespace diagnostics {

// Interface which allows cros_healthd to access the browser's
// NetworkDiagnosticsRoutines interface.
class NetworkDiagnosticsAdapter {
 public:
  using MojomLanConnectivityCallback = chromeos::network_diagnostics::mojom::
      NetworkDiagnosticsRoutines::LanConnectivityCallback;

  virtual ~NetworkDiagnosticsAdapter() = default;

  // Sets the NetworkDiagnosticsRoutines remote sent by the browser.
  virtual void SetNetworkDiagnosticsRoutines(
      mojo::PendingRemote<
          chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
          network_diagnostics_routines) = 0;

  // Requests that the browser invokes the LanConnectivity routine.
  virtual void RunLanConnectivityRoutine(MojomLanConnectivityCallback) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_NETWORK_DIAGNOSTICS_NETWORK_DIAGNOSTICS_ADAPTER_H_
