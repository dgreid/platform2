/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef HARDWARE_VERIFIER_METRICS_H_
#define HARDWARE_VERIFIER_METRICS_H_

#include <map>
#include <memory>
#include <string>

#include <base/time/time.h>
#include <metrics/metrics_library.h>

namespace hardware_verifier {

// Total time to finish execution (initialization + probing + verification).
const char kMetricTimeToFinish[] = "HardwareVerifier.TimeToFinish";

// Total time to finish probing.
const char kMetricTimeToProbe[] = "HardwareVerifier.TimeToProbe";

class Metrics {
 public:
  void StartTimer(const std::string& timer_name);
  void StopTimer(const std::string& timer_name);

  virtual ~Metrics() = default;

 protected:
  virtual void SendTimerSample(const std::string& timer_name,
                               int sample_ms) = 0;

 private:
  std::map<std::string, base::TimeTicks> timers_;
};

// A dummy implementation, all records logged by VLOG().
class DummyMetrics : public Metrics {
 public:
  DummyMetrics() = default;
  ~DummyMetrics() = default;

 protected:
  void SendTimerSample(const std::string& timer_name, int sample_ms) {
    // Do nothing.
  }
};

// Metrics implementation that sends data to Chrome UMA backend.
class UMAMetrics : public Metrics {
 public:
  UMAMetrics();
  ~UMAMetrics() = default;

 protected:
  void SendTimerSample(const std::string& timer_name, int sample_ms);

 private:
  std::unique_ptr<MetricsLibrary> metrics_library_;

  // The entire program should end within one minutes, so it should be safe to
  // assume that all timer samples should be a value in range [0, 60 * 1000] ms.
  static const int kTimerMinMs_ = 0;
  static const int kTimerMaxMs_ = 60 * 1000;
  // Maximum recommended value.
  static const int kTimerBuckets_ = 50;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_METRICS_H_
