// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_PROBE_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_PROBE_SERVICE_H_

#include <cstdint>
#include <vector>

#include <base/macros.h>

#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Fake implementation of the CrosHealthdProbeService interface.
class FakeProbeService final
    : public chromeos::cros_healthd::mojom::CrosHealthdProbeService {
 public:
  using ProbeCategoryEnum = chromeos::cros_healthd::mojom::ProbeCategoryEnum;

  FakeProbeService();
  ~FakeProbeService() override;

  // chromeos::cros_healthd::mojom::CrosHealthdProbeService overrides:
  void ProbeTelemetryInfo(const std::vector<ProbeCategoryEnum>& categories,
                          ProbeTelemetryInfoCallback callback) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeProbeService);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_PROBE_SERVICE_H_
