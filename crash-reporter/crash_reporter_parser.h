// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CRASH_REPORTER_PARSER_H_
#define CRASH_REPORTER_CRASH_REPORTER_PARSER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/time/clock.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/anomaly_detector.h"

namespace anomaly {

// Anomaly_detector's collector for syslog entries from our own crash_reporter.
// Unlike other anomaly_detector collectors, this doesn't actually ever create
// crash reports -- ParseLogEntry always returns nullopt. Instead, it produces
// UMA metrics that track how well Chrome's crash handlers (breakpad or
// crashpad) are working. If Chrome gets a segfault or such, its internal crash
// handler should invoke crash_reporter directly. Once the internal crash
// handler is done, the kernel should also invoke crash_reporter via the normal
// core pattern file. Both of these produce distinct log entries. By matching
// these up, we can detect how often the internal crash handler is failing to
// invoke crash_reporter. In particular, if we see an invoked-by-kernel message
// without a corresponding invoking-directly message, Chrome's crash handler
// failed. We record the number of unmatched invoked-by-kernel messages, and,
// for a denominator, we record the total number of invoked-by-kernel messages.
//
// (There are some cases -- "dump without crashing" -- in which Chrome will
// invoke crash_reporter but will not actually crash, and so will not produce
// an invoked-by-kernel message. This is why we go to the trouble of actually
// matching up messages from the log, instead of just counting the number of
// invoked-directly and invoked-from-kernel events. The "dump without crashing"
// events will overcount the number of successes and hide the true number of
// failures. Therefore, we ignore "dump without crashing" crashes by not
// counting the number of invoked-by-Chrome messages we see, and not reporting
// the number of unmatched invoked-by-Chrome messages.)
class CrashReporterParser : public Parser {
 public:
  // We hold on to unmatched messages for at least this long before reporting
  // them as unmatched.
  static constexpr base::TimeDelta kTimeout = base::TimeDelta::FromSeconds(30);

  explicit CrashReporterParser(
      std::unique_ptr<base::Clock> clock,
      std::unique_ptr<MetricsLibraryInterface> metrics_lib);
  MaybeCrashReport ParseLogEntry(const std::string& line) override;
  void PeriodicUpdate() override;

 private:
  enum class Collector {
    // Log entry was from ChromeCollector.
    CHROME,

    // Log entry was from UserCollector.
    USER
  };

  struct UnmatchedCrash {
    int pid;
    base::Time timestamp;
    Collector collector;
  };

  std::unique_ptr<base::Clock> clock_;
  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
  std::vector<UnmatchedCrash> unmatched_crashes_;
};

}  // namespace anomaly

#endif  // CRASH_REPORTER_CRASH_REPORTER_PARSER_H_
