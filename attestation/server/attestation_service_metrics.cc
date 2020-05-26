// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/server/attestation_service_metrics.h"

namespace attestation {

namespace {

constexpr char kAttestationStatusHistogramPrefix[] = "Hwsec.Attestation.Status";

}  // namespace

void AttestationServiceMetrics::ReportAttestationOpsStatus(
    const std::string& operation, AttestationOpsStatus status) {
  if (!metrics_library_) {
    return;
  }

  const std::string histogram =
      std::string(kAttestationStatusHistogramPrefix) + "." + operation;
  metrics_library_->SendEnumToUMA(
      histogram, static_cast<int>(status),
      static_cast<int>(AttestationOpsStatus::kMaxValue));
}

}  // namespace attestation
