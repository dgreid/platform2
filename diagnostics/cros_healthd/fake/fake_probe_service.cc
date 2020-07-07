// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_probe_service.h"

#include <base/logging.h>

namespace diagnostics {

FakeProbeService::FakeProbeService() = default;
FakeProbeService::~FakeProbeService() = default;

void FakeProbeService::ProbeProcessInfo(uint32_t process_id,
                                        ProbeProcessInfoCallback callback) {
  NOTIMPLEMENTED();
}

void FakeProbeService::ProbeTelemetryInfo(
    const std::vector<ProbeCategoryEnum>& categories,
    ProbeTelemetryInfoCallback callback) {
  NOTIMPLEMENTED();
}

}  // namespace diagnostics
