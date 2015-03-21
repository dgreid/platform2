// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_metrics.h"

#include <base/logging.h>
#include <metrics/metrics_library.h>
#include <metrics/timer.h>

namespace {

struct TimerHistogramParams {
  const char* metric_name;
  int min_sample;
  int max_sample;
  int num_buckets;
};

const char kCryptohomeErrorHistogram[] = "Cryptohome.Errors";
const int kCryptohomeErrorNumBuckets = 50;
const char kDictionaryAttackResetStatusHistogram[] =
    "Platform.TPM.DictionaryAttackResetStatus";
const int kDictionaryAttackResetStatusNumBuckets = 10;
const char kDictionaryAttackCounterHistogram[] =
    "Platform.TPM.DictionaryAttackCounter";
const char kDictionaryAttackCounterNumBuckets = 100;

// Histogram parameters. This should match the order of 'TimerType'.
// Min and max samples are in milliseconds.
const TimerHistogramParams kTimerHistogramParams[cryptohome::kNumTimerTypes] = {
  {"Cryptohome.TimeToMountAsync", 0, 4000, 50},
  {"Cryptohome.TimeToMountSync", 0, 4000, 50},
  {"Cryptohome.TimeToMountGuestAsync", 0, 4000, 50},
  {"Cryptohome.TimeToMountGuestSync", 0, 4000, 50},
  {"Cryptohome.TimeToTakeTpmOwnership", 0, 100000, 50},
  // A note on the PKCS#11 initialization time:
  // Max sample for PKCS#11 initialization time is 100s; we are interested
  // in recording the very first PKCS#11 initialization time, which may be a
  // lengthy one. Subsequent initializations are fast (under 1s) because they
  // just check if PKCS#11 was previously initialized, returning immediately.
  // These will all fall into the first histogram bucket.
  {"Cryptohome.TimeToInitPkcs11", 1000, 100000, 50},
  {"Cryptohome.TimeToMountEx", 0, 4000, 50}
};

MetricsLibrary* g_metrics = NULL;
chromeos_metrics::TimerReporter* g_timers[cryptohome::kNumTimerTypes] = {NULL};

chromeos_metrics::TimerReporter* GetTimer(cryptohome::TimerType timer_type) {
  if (!g_timers[timer_type]) {
    g_timers[timer_type] = new chromeos_metrics::TimerReporter(
        kTimerHistogramParams[timer_type].metric_name,
        kTimerHistogramParams[timer_type].min_sample,
        kTimerHistogramParams[timer_type].max_sample,
        kTimerHistogramParams[timer_type].num_buckets);
  }
  return g_timers[timer_type];
}

}  // namespace

namespace cryptohome {

void InitializeMetrics() {
  g_metrics = new MetricsLibrary();
  g_metrics->Init();
  chromeos_metrics::TimerReporter::set_metrics_lib(g_metrics);
}

void TearDownMetrics() {
  if (g_metrics) {
    chromeos_metrics::TimerReporter::set_metrics_lib(NULL);
    delete g_metrics;
    g_metrics = NULL;
  }
  for (int i = 0; i < kNumTimerTypes; ++i) {
    if (g_timers[i]) {
      delete g_timers[i];
    }
  }
}

void ReportCryptohomeError(CryptohomeError error) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeErrorHistogram,
                           error,
                           kCryptohomeErrorNumBuckets);
}

void ReportCrosEvent(const char* event) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendCrosEventToUMA(event);
}

void ReportTimerStart(TimerType timer_type) {
  if (!g_metrics) {
    return;
  }
  chromeos_metrics::TimerReporter* timer = GetTimer(timer_type);
  if (!timer) {
    return;
  }
  timer->Start();
}

void ReportTimerStop(TimerType timer_type) {
  if (!g_metrics) {
    return;
  }
  chromeos_metrics::TimerReporter* timer = GetTimer(timer_type);
  bool success = (timer && timer->HasStarted() &&
                  timer->Stop() && timer->ReportMilliseconds());
  if (!success) {
    LOG(WARNING) << "Timer " << kTimerHistogramParams[timer_type].metric_name
                 << " failed to report.";
  }
}

void ReportDictionaryAttackResetStatus(DictionaryAttackResetStatus status) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kDictionaryAttackResetStatusHistogram,
                           status,
                           kDictionaryAttackResetStatusNumBuckets);
}

void ReportDictionaryAttackCounter(int counter) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kDictionaryAttackCounterHistogram,
                           counter,
                           kDictionaryAttackCounterNumBuckets);
}

}  // namespace cryptohome
