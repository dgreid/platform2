/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "hardware_verifier/observer.h"

namespace hardware_verifier {

Observer* Observer::GetInstance() {
  static base::NoDestructor<Observer> instance;
  return instance.get();
}

void Observer::StartTimer(const std::string& timer_name) {
  VLOG(1) << "Start timer |" << timer_name << "|";
  timers_[timer_name] = base::TimeTicks::Now();
}

void Observer::StopTimer(const std::string& timer_name) {
  auto it = timers_.find(timer_name);

  DCHECK(it != timers_.end());

  auto start = it->second;
  timers_.erase(it);
  auto now = base::TimeTicks::Now();
  auto duration_ms = (now - start).InMilliseconds();

  VLOG(1) << "Stop timer |" << timer_name << "|, time elapsed: " << duration_ms
          << "ms.\n";

  if (metrics_) {
    metrics_->SendToUMA(timer_name, duration_ms, kTimerMinMs_, kTimerMaxMs_,
                        kTimerBuckets_);
  }
}

void Observer::SetMetricsLibrary(
    std::unique_ptr<MetricsLibraryInterface> metrics) {
  metrics_ = std::move(metrics);
}

}  // namespace hardware_verifier
