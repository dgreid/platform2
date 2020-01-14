/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "hardware_verifier/metrics.h"

namespace hardware_verifier {

void Metrics::StartTimer(const std::string& timer_name) {
  VLOG(1) << "Start timer |" << timer_name << "|";
  timers_[timer_name] = base::TimeTicks::Now();
}

void Metrics::StopTimer(const std::string& timer_name) {
  auto it = timers_.find(timer_name);

  DCHECK(it != timers_.end());

  auto start = it->second;
  timers_.erase(it);
  auto now = base::TimeTicks::Now();
  auto duration_ms = (now - start).InMilliseconds();

  VLOG(1) << "Stop timer |" << timer_name << "|, time elapsed: " << duration_ms
          << "ms.\n";

  SendTimerSample(timer_name, duration_ms);
}

UMAMetrics::UMAMetrics()
    : metrics_library_(std::make_unique<MetricsLibrary>()) {}

void UMAMetrics::SendTimerSample(const std::string& timer_name, int sample_ms) {
  metrics_library_->SendToUMA(timer_name, sample_ms, kTimerMinMs_, kTimerMaxMs_,
                              kTimerBuckets_);
}

}  // namespace hardware_verifier
