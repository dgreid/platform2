// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_SERVER_ATTESTATION_SERVICE_METRICS_H_
#define ATTESTATION_SERVER_ATTESTATION_SERVICE_METRICS_H_

#include <string>

#include <metrics/metrics_library.h>

namespace attestation {

// List of generic results of attestation-related operations. These entries
// should not be renumbered and numeric values should never be reused.
enum class AttestationOpsStatus {
  kSuccess = 0,
  kFailure = 1,
  kInvalidPcr0Value = 2,
  kMaxValue,
};

// Attestation-related operations. These are used as suffixes to
// kAttestationStatusHistogramPrefix defined in the .cc.
constexpr char kAttestationEncryptDatabase[] = "EncryptDatabase";
constexpr char kAttestationDecryptDatabase[] = "DecryptDatabase";
constexpr char kAttestationActivateAttestationKey[] = "ActivateAttestationKey";
constexpr char kAttestationVerify[] = "AttestationVerify";
constexpr char kAttestationPrepareForEnrollment[] = "PrepareForEnrollment";

// This class provides helper functions to report attestation-related
// metrics.
class AttestationServiceMetrics : private MetricsLibrary {
 public:
  AttestationServiceMetrics() = default;
  virtual ~AttestationServiceMetrics() = default;

  AttestationServiceMetrics(const AttestationServiceMetrics&) = delete;
  AttestationServiceMetrics& operator=(const AttestationServiceMetrics&) =
      delete;

  virtual void ReportAttestationOpsStatus(const std::string& operation,
                                          AttestationOpsStatus status);

  void set_metrics_library_for_testing(
      MetricsLibraryInterface* metrics_library) {
    metrics_library_ = metrics_library;
  }

 private:
  MetricsLibraryInterface* metrics_library_{this};
};

}  // namespace attestation

#endif  // ATTESTATION_SERVER_ATTESTATION_SERVICE_METRICS_H_
