// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_reporter_parser.h"

#include <utility>

#include <re2/re2.h>

namespace anomaly {

constexpr LazyRE2 chrome_crash_called_directly = {
    "Received crash notification for chrome\\[(\\d+)\\][[:alnum:] ]+"
    "\\(called directly\\)"};

constexpr LazyRE2 chrome_crash_called_by_kernel = {
    "Received crash notification for chrome\\[(\\d+)\\][[:alnum:], ]+"
    "\\(ignoring call by kernel - chrome crash"};

constexpr char kUMACrashesFromKernel[] = "Crash.Chrome.CrashesFromKernel";
constexpr char kUMAMissedCrashes[] = "Crash.Chrome.MissedCrashes";
constexpr base::TimeDelta CrashReporterParser::kTimeout;

CrashReporterParser::CrashReporterParser(
    std::unique_ptr<base::Clock> clock,
    std::unique_ptr<MetricsLibraryInterface> metrics_lib)
    : clock_(std::move(clock)), metrics_lib_(std::move(metrics_lib)) {
  metrics_lib_->Init();
}

MaybeCrashReport CrashReporterParser::ParseLogEntry(const std::string& line) {
  int pid = 0;
  UnmatchedCrash crash;
  if (RE2::PartialMatch(line, *chrome_crash_called_directly, &pid)) {
    crash.pid = pid;
    crash.collector = Collector::CHROME;
    crash.timestamp = clock_->Now();
  } else if (RE2::PartialMatch(line, *chrome_crash_called_by_kernel, &pid)) {
    crash.pid = pid;
    crash.collector = Collector::USER;
    crash.timestamp = clock_->Now();
  } else {
    return base::nullopt;
  }

  // Find the matching entry in our unmatched_crashes_ vector. We expect each
  // real chrome crash to reported twice, with the same PID -- once with "called
  // directly" and once with "ignoring call by kernel".
  for (auto it = unmatched_crashes_.begin(); it != unmatched_crashes_.end();
       ++it) {
    if (it->pid == crash.pid && it->collector != crash.collector) {
      // Found the corresponding message from the other collector. Throw away
      // both.
      unmatched_crashes_.erase(it);
      // One of the two was a crash from kernel, so record that we got a crash
      // from kernel. (We only send the events when we match or don't match;
      // this avoids having our data polluted by events just before a shutdown.)
      if (!metrics_lib_->SendCrosEventToUMA(kUMACrashesFromKernel)) {
        LOG(WARNING) << "Could not mark Chrome crash as correctly processed";
      }
      return base::nullopt;
    }
  }

  unmatched_crashes_.push_back(crash);
  return base::nullopt;
}

void CrashReporterParser::PeriodicUpdate() {
  base::Time too_old = clock_->Now() - kTimeout;
  auto it = unmatched_crashes_.begin();

  while (it != unmatched_crashes_.end()) {
    if (it->timestamp < too_old) {
      if (it->collector == Collector::USER) {
        if (!metrics_lib_->SendCrosEventToUMA(kUMACrashesFromKernel) ||
            !metrics_lib_->SendCrosEventToUMA(kUMAMissedCrashes)) {
          LOG(WARNING) << "Could not mark Chrome crash as missed";
        }
      }
      it = unmatched_crashes_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace anomaly
