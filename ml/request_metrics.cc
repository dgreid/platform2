// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/request_metrics.h"

#include <base/logging.h>

#include "ml/mojom/machine_learning_service.mojom.h"

namespace ml {

using chromeos::machine_learning::mojom::LoadModelResult;

RequestMetrics::RequestMetrics(
    const std::string& model_name, const std::string& request_name)
    : name_base_(std::string(kGlobalMetricsPrefix) + model_name + "." +
                 request_name),
      process_metrics_(nullptr) {}

void RequestMetrics::StartRecordingPerformanceMetrics() {
  DCHECK(process_metrics_ == nullptr);
  process_metrics_ = base::ProcessMetrics::CreateCurrentProcessMetrics();
  // Call GetPlatformIndependentCPUUsage in order to set the "zero" point of the
  // CPU usage counter of process_metrics_.
  process_metrics_->GetPlatformIndependentCPUUsage();
  timer_.Start();
  // Query memory usage.
  size_t usage = 0;
  if (!GetTotalProcessMemoryUsage(&usage)) {
    LOG(DFATAL) << "Getting process memory usage failed.";
    return;
  }
  initial_memory_ = static_cast<int64_t>(usage);
}

void RequestMetrics::FinishRecordingPerformanceMetrics() {
  DCHECK(process_metrics_ != nullptr);
  // To get CPU time, we multiply elapsed (wall) time by CPU usage percentage.
  timer_.Stop();
  base::TimeDelta elapsed_time;
  DCHECK(timer_.GetElapsedTime(&elapsed_time));
  const int64_t elapsed_time_microsec = elapsed_time.InMicroseconds();

  // CPU usage, 12.34 means 12.34%, and the range is 0 to 100 * numCPUCores.
  // That's to say it can exceed 100 when there're multi CPUs.
  // For example, if the device has 4 CPUs and the process fully uses 2 of
  // them, the percent will be 200%.
  const double cpu_usage_percent =
      process_metrics_->GetPlatformIndependentCPUUsage();

  // CPU time, as mentioned above, "100 microseconds" means "1 CPU core fully
  // utilized for 100 microseconds".
  const int64_t cpu_time_microsec =
      static_cast<int64_t>(cpu_usage_percent * elapsed_time_microsec / 100.);

  // Memory usage
  size_t usage = 0;
  if (!GetTotalProcessMemoryUsage(&usage)) {
    LOG(DFATAL) << "Getting process memory usage failed.";
    return;
  }
  const int64_t memory_usage_kb =
      static_cast<int64_t>(usage) - initial_memory_;

  metrics_library_.SendToUMA(name_base_ + kTotalMemoryDeltaSuffix,
                             memory_usage_kb,
                             kMemoryDeltaMinKb,
                             kMemoryDeltaMaxKb,
                             kMemoryDeltaBuckets);
  metrics_library_.SendToUMA(name_base_ + kCpuTimeSuffix,
                             cpu_time_microsec,
                             kCpuTimeMinMicrosec,
                             kCpuTimeMaxMicrosec,
                             kCpuTimeBuckets);
}

// Records in MachineLearningService.LoadModelResult rather than a
// model-specific enum histogram because the model name is unknown.
void RecordModelSpecificationErrorEvent() {
  MetricsLibrary().SendEnumToUMA(
      "MachineLearningService.LoadModelResult",
      static_cast<int>(LoadModelResult::MODEL_SPEC_ERROR),
      static_cast<int>(LoadModelResult::kMaxValue) + 1);
}

}  // namespace ml
