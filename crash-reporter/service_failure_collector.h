// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The service failure collector gathers information about upstart services that
// exit with a non-zero status.
// Anomaly detector monitors the system journal for log lines that indicate
// non-zero exit and invokes this collector when it sees them.

#ifndef CRASH_REPORTER_SERVICE_FAILURE_COLLECTOR_H_
#define CRASH_REPORTER_SERVICE_FAILURE_COLLECTOR_H_

#include <string>

#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "crash-reporter/crash_collector.h"

// Service failure collector.
class ServiceFailureCollector : public CrashCollector {
 public:
  ServiceFailureCollector();

  ~ServiceFailureCollector() override;

  // Collects service failure.
  bool Collect();

  void SetServiceName(const std::string& service_name) {
    service_name_ = service_name;
  }

 protected:
  std::string failure_report_path_;
  std::string exec_name_;
  std::string service_name_;

 private:
  friend class ServiceFailureCollectorTest;
  friend class ArcServiceFailureCollectorTest;

  // Service failure dump consists only of the signature.
  bool LoadServiceFailure(std::string* signature);

  DISALLOW_COPY_AND_ASSIGN(ServiceFailureCollector);
};

#endif  // CRASH_REPORTER_SERVICE_FAILURE_COLLECTOR_H_
