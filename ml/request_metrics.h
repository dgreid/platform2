// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_REQUEST_METRICS_H_
#define ML_REQUEST_METRICS_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/process/process_metrics.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "metrics/timer.h"
#include "ml/util.h"

namespace ml {

// Performs UMA metrics logging for model loading (LoadBuiltinModel or
// LoadFlatBufferModel), CreateGraphExecutor and Execute. Metrics includes
// events(enumerators defined by RequestEventEnum), memory_usage, and cpu_time.
// RequestEventEnum is an enum class which defines different events for some
// specific actions, currently we reuse the enum classes defined in mojoms. The
// enum class generally contains an OK and several different Errors, besides,
// there should be a kMax which shares the value of the highest enumerator.
class RequestMetrics {
 public:
  // Creates a RequestMetrics with the specified model and request names.
  // Records UMA metrics named with the prefix
  // "MachineLearningService.`model_name`.`request_name`."
  RequestMetrics(const std::string& model_name,
                 const std::string& request_name);

  // Logs (to UMA) the specified `event` associated with this request.
  template <class RequestEventEnum>
  void RecordRequestEvent(RequestEventEnum event);

  // When you want to record metrics of some action, call Start func at the
  // beginning of it.
  void StartRecordingPerformanceMetrics();

  // Send performance metrics(memory_usage, cpu_time) to UMA
  // This would usually be called only if the action completes successfully.
  void FinishRecordingPerformanceMetrics();

 private:
  MetricsLibrary metrics_library_;

  const std::string name_base_;
  std::unique_ptr<base::ProcessMetrics> process_metrics_;
  chromeos_metrics::Timer timer_;
  int64_t initial_memory_;

  DISALLOW_COPY_AND_ASSIGN(RequestMetrics);
};

// UMA metric names:
constexpr char kGlobalMetricsPrefix[] = "MachineLearningService.";
constexpr char kEventSuffix[] = ".Event";
constexpr char kTotalMemoryDeltaSuffix[] = ".TotalMemoryDeltaKb";
constexpr char kCpuTimeSuffix[] = ".CpuTimeMicrosec";

// UMA histogram ranges:
constexpr int kMemoryDeltaMinKb = 1;         // 1 KB
constexpr int kMemoryDeltaMaxKb = 10000000;  // 10 GB
constexpr int kMemoryDeltaBuckets = 100;
constexpr int kCpuTimeMinMicrosec = 1;           // 1 μs
constexpr int kCpuTimeMaxMicrosec = 1800000000;  // 30 min
constexpr int kCpuTimeBuckets = 100;

template <class RequestEventEnum>
void RequestMetrics::RecordRequestEvent(RequestEventEnum event) {
  metrics_library_.SendEnumToUMA(
      name_base_ + kEventSuffix, static_cast<int>(event),
      static_cast<int>(RequestEventEnum::kMaxValue) + 1);
  process_metrics_.reset(nullptr);
}

// Records a generic model specification error event during a model loading
// (LoadBuiltinModel or LoadFlatBufferModel) request.
void RecordModelSpecificationErrorEvent();

}  // namespace ml

#endif  // ML_REQUEST_METRICS_H_
