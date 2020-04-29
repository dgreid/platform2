// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_PROBE_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_PROBE_SERVICE_H_

#include <vector>

#include <base/callback.h>

#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The probe service is responsible for getting telemetry information.
class ProbeService {
 public:
  using ProbeTelemetryInfoCallback =
      base::OnceCallback<void(chromeos::cros_healthd::mojom::TelemetryInfoPtr)>;

  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Binds |service| to an implementation of CrosHealthdProbeService. In
    // production, the implementation is provided by cros_healthd. Returns
    // whether binding is successful.
    virtual bool BindCrosHealthdProbeService(
        chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest
            service) = 0;
  };

  virtual ~ProbeService() = default;

  // Requests telemetry info for categories.
  virtual void ProbeTelemetryInfo(
      std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum> categories,
      ProbeTelemetryInfoCallback callback) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_PROBE_SERVICE_H_
