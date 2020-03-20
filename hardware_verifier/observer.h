/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef HARDWARE_VERIFIER_OBSERVER_H_
#define HARDWARE_VERIFIER_OBSERVER_H_

#include <map>
#include <memory>
#include <string>

#include <base/time/time.h>
#include <metrics/metrics_library.h>

namespace hardware_verifier {

// Total time to finish execution (initialization + probing + verification).
const char kMetricTimeToFinish[] = "ChromeOS.HardwareVerifier.TimeToFinish";

// Total time to finish probing.
const char kMetricTimeToProbe[] = "ChromeOS.HardwareVerifier.TimeToProbe";

// The entire program should end within one minutes, so it should be safe to
// assume that all timer samples should be a value in range [0, 60 * 1000] ms.
const int kTimerMinMs_ = 0;
const int kTimerMaxMs_ = 60 * 1000;
// Maximum recommended value.
const int kTimerBuckets_ = 50;

// Observe and potentially logs the behavior of hardware_verifier.
class Observer {
 public:
  Observer() = default;
  ~Observer() = default;

  void StartTimer(const std::string& timer_name);
  void StopTimer(const std::string& timer_name);

  void SetMetricsLibrary(std::unique_ptr<MetricsLibraryInterface> metrics);

 private:
  std::map<std::string, base::TimeTicks> timers_;
  std::unique_ptr<MetricsLibraryInterface> metrics_;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_OBSERVER_H_
