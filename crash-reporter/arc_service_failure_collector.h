// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The ARC service failure collector is a minor variant of the service failure
// collector that grabs some additional logs for services relating to android on
// ChromeOS (ARC).
// Like with regular service failures, the anomaly detector looks in the syslog
// for lines from init indicating that a process it manages exited with a
// non-zero status code.

#ifndef CRASH_REPORTER_ARC_SERVICE_FAILURE_COLLECTOR_H_
#define CRASH_REPORTER_ARC_SERVICE_FAILURE_COLLECTOR_H_

#include "crash-reporter/service_failure_collector.h"

// ARC ServiceFailureCollector.
class ArcServiceFailureCollector : public ServiceFailureCollector {
 public:
  ArcServiceFailureCollector();

  ~ArcServiceFailureCollector() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(ArcServiceFailureCollector);
};

#endif  // CRASH_REPORTER_ARC_SERVICE_FAILURE_COLLECTOR_H_
