// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/fake_probe_service.h"

#include <utility>

#include <base/logging.h>

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

using ProbeTelemetryInfoCallback =
    base::OnceCallback<void(chromeos::cros_healthd::mojom::TelemetryInfoPtr)>;

void MissingProbeTelemetryInfoCallback(
    std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>,
    ProbeTelemetryInfoCallback) {
  DCHECK(nullptr);
}
}  // namespace

FakeProbeService::FakeProbeService()
    : telemetry_callback_(base::Bind(MissingProbeTelemetryInfoCallback)) {}

FakeProbeService::~FakeProbeService() = default;

void FakeProbeService::SetProbeTelemetryInfoCallback(
    base::Callback<
        void(std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>,
             ProbeTelemetryInfoCallback)> callback) {
  telemetry_callback_ = callback;
}

void FakeProbeService::ProbeTelemetryInfo(
    std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum> categories,
    ProbeTelemetryInfoCallback callback) {
  telemetry_callback_.Run(categories, std::move(callback));
}

}  // namespace diagnostics
