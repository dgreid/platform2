/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <memory>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/strings/strcat.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "hardware_verifier/hardware_verifier.pb.h"
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

void Observer::RecordHwVerificationReport(const HwVerificationReport& report) {
  {
    auto key = base::StrCat({kMetricVerifierReportPrefix, "IsCompliant"});
    LOG(INFO) << key << ": " << report.is_compliant();
    if (metrics_) {
      metrics_->SendBoolToUMA(key, report.is_compliant());
    }
  }

  for (auto i = 0; i < report.found_component_infos_size(); i++) {
    const auto& info = report.found_component_infos(i);
    const auto& name = runtime_probe::ProbeRequest_SupportCategory_Name(
        info.component_category());
    const auto& qualification_status = info.qualification_status();

    const std::string uma_key =
        base::StrCat({kMetricVerifierReportPrefix, name});

    LOG(INFO) << uma_key << ": "
              << QualificationStatus_Name(qualification_status);
    if (metrics_) {
      metrics_->SendEnumToUMA(uma_key, qualification_status,
                              QualificationStatus_ARRAYSIZE);
    }
  }
}

}  // namespace hardware_verifier
