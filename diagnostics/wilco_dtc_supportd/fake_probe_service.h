// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_PROBE_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_PROBE_SERVICE_H_

#include <vector>

#include <base/callback.h>

#include "diagnostics/wilco_dtc_supportd/probe_service.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

class FakeProbeService : public ProbeService {
 public:
  FakeProbeService();
  ~FakeProbeService() override;
  FakeProbeService(const FakeProbeService&) = delete;
  FakeProbeService& operator=(const FakeProbeService&) = delete;
  FakeProbeService(FakeProbeService&&) = delete;
  FakeProbeService& operator=(FakeProbeService&&) = delete;

  void SetProbeTelemetryInfoCallback(
      base::Callback<
          void(std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>,
               ProbeTelemetryInfoCallback)> callback);

 private:
  void ProbeTelemetryInfo(
      std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum> categories,
      ProbeTelemetryInfoCallback callback) override;

  base::Callback<void(
      std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>,
      ProbeTelemetryInfoCallback)>
      telemetry_callback_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_PROBE_SERVICE_H_
