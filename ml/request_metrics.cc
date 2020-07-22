// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/request_metrics.h"

#include <base/logging.h>

#include "ml/mojom/machine_learning_service.mojom.h"

namespace ml {

using chromeos::machine_learning::mojom::LoadModelResult;

RequestMetrics::RequestMetrics(const std::string& model_name,
                               const std::string& request_name)
    : name_base_(std::string(kGlobalMetricsPrefix) + model_name + "." +
                 request_name),
      initial_cpu_clock_(0),
      initial_memory_(0), status_(Status::kNotStarted) {}

void RequestMetrics::StartRecordingPerformanceMetrics() {
  DCHECK(status_ == Status::kNotStarted);
  // Get initial CPU clock in order to set the "zero" point of the CPU usage
  // counter.
  initial_cpu_clock_ = std::clock();
  DCHECK(initial_cpu_clock_ != static_cast<std::clock_t>(-1));
  // Query memory usage.
  size_t usage = 0;
  if (!GetTotalProcessMemoryUsage(&usage)) {
    LOG(DFATAL) << "Getting process memory usage failed.";
    return;
  }
  initial_memory_ = static_cast<int64_t>(usage);

  status_ = Status::kRecording;
}

void RequestMetrics::FinishRecordingPerformanceMetrics() {
  DCHECK(status_ == Status::kRecording);
  status_ = Status::kFinished;
  // Get CPU time by `clock()`.
  const int64_t cpu_time_microsec = static_cast<int64_t>(
      (std::clock() - initial_cpu_clock_) * 1000000.0 / CLOCKS_PER_SEC);

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
