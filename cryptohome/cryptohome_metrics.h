// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_METRICS_H_
#define CRYPTOHOME_CRYPTOHOME_METRICS_H_

namespace cryptohome {

enum CryptohomeError {
  kTpmFail = 1,
  kTcsKeyLoadFailed = 2,
  kTpmDefendLockRunning = 3,
  kDecryptAttemptButTpmKeyMissing = 4,
  kDecryptAttemptButTpmNotOwned = 5,
  kDecryptAttemptButTpmNotAvailable = 6,
  kDecryptAttemptButTpmKeyMismatch = 7,
  kDecryptAttemptWithTpmKeyFailed = 8,
  kCannotLoadTpmSrk = 9,
  kCannotReadTpmSrkPublic = 10,
  kCannotLoadTpmKey = 11,
  kCannotReadTpmPublicKey = 12,
  kTpmBadKeyProperty = 13,
  kLoadPkcs11TokenFailed = 14,
  kEncryptWithTpmFailed = 15,
};

enum TimerType {
  kAsyncMountTimer,
  kSyncMountTimer,
  kAsyncGuestMountTimer,
  kSyncGuestMountTimer,
  kTpmTakeOwnershipTimer,
  kPkcs11InitTimer,
  kMountExTimer,
  kNumTimerTypes  // For the number of timer types.
};

enum DictionaryAttackResetStatus {
  kResetNotNecessary,
  kResetAttemptSucceeded,
  kResetAttemptFailed,
  kDelegateNotAllowed,
  kDelegateNotAvailable,
  kCounterQueryFailed,
};

// Cros events emitted by cryptohome.
const char kAttestationOriginSpecificIdentifiersExhausted[] =
    "Attestation.OriginSpecificExhausted";

// Initializes cryptohome metrics. If this is not called, all calls to Report*
// will have no effect.
void InitializeMetrics();

// Cleans up and returns cryptohome metrics to an uninitialized state.
void TearDownMetrics();

// The |error| value is reported to the "Cryptohome.Errors" enum histogram.
void ReportCryptohomeError(CryptohomeError error);

// Cros events are translated to an enum and reported to the generic
// "Platform.CrOSEvent" enum histogram. The |event| string must be registered in
// metrics/metrics_library.cc:kCrosEventNames.
void ReportCrosEvent(const char* event);

// Starts a timer for the given |timer_type|.
void ReportTimerStart(TimerType timer_type);

// Stops a timer and reports in milliseconds. Timers are reported to the
// "Cryptohome.TimeTo*" histograms.
void ReportTimerStop(TimerType timer_type);

// Reports a status value on the "Platform.TPM.DictionaryAttackResetStatus"
// histogram.
void ReportDictionaryAttackResetStatus(DictionaryAttackResetStatus status);

// Reports a dictionary attack counter value to the
// "Platform.TPM.DictionaryAttackCounter" histogram.
void ReportDictionaryAttackCounter(int counter);

// Initialization helper.
class ScopedMetricsInitializer {
 public:
  ScopedMetricsInitializer() { InitializeMetrics(); }
  ~ScopedMetricsInitializer() { TearDownMetrics(); }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_METRICS_H_
