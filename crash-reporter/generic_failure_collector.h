// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The generic failure collector collects issues that anomaly_detector catches
// that manifest as a single line logged to a log file.
// The flow looks like this:
// 1. One of the parsers in anomaly_detector flag an issue
// 2. anomaly_detector invokes crash_reporter with an appropriate flag
// 3. crash_reporter invokes a GenericFailureCollector instance

#ifndef CRASH_REPORTER_GENERIC_FAILURE_COLLECTOR_H_
#define CRASH_REPORTER_GENERIC_FAILURE_COLLECTOR_H_

#include <string>

#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "crash-reporter/crash_collector.h"

// Generic failure collector.
class GenericFailureCollector : public CrashCollector {
 public:
  GenericFailureCollector();
  explicit GenericFailureCollector(const std::string& exec_name);

  ~GenericFailureCollector() override;

  // Collects service failure.
  bool Collect();

  static const char* const kGenericFailure;
  static const char* const kSuspendFailure;

 protected:
  std::string failure_report_path_;
  std::string exec_name_;

 private:
  friend class GenericFailureCollectorTest;
  friend class ArcGenericFailureCollectorTest;

  // Generic failure dump consists only of the signature.
  bool LoadGenericFailure(std::string* content, std::string* signature);

  DISALLOW_COPY_AND_ASSIGN(GenericFailureCollector);
};

#endif  // CRASH_REPORTER_GENERIC_FAILURE_COLLECTOR_H_
