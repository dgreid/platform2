// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_MISSED_CRASH_COLLECTOR_H_
#define CRASH_REPORTER_MISSED_CRASH_COLLECTOR_H_

#include "crash-reporter/crash_collector.h"

#include <string>

#include <stdint.h>
#include <stdio.h>

// Handles reports from anomaly_detector that we failed to capture a Chrome
// crash. The class is a bit of an oddity in that it doesn't collect its logs
// itself; instead, it has the logs passed to it on a file descriptor.
class MissedCrashCollector : public CrashCollector {
 public:
  // Visible for testing only.
  static constexpr int64_t kDefaultChunkSize = 1 << 16;

  MissedCrashCollector();
  ~MissedCrashCollector() override;

  bool Collect(int pid,
               int recent_miss_count,
               int recent_match_count,
               int pending_miss_count);

  // Does not take ownership.
  void set_input_file_for_testing(FILE* input_file) {
    input_file_ = input_file;
  }

 private:
  // FILE we can read from that contains the logs to attach to this crash
  // report. Default is stdin. Class does not own the FILE and will not close
  // it.
  FILE* input_file_;

  // Read all the contents of the given FILE* to |contents| until either
  // EOF or an error. Assumes |file| is at the start of the file.
  static bool ReadFILEToString(FILE* file, std::string* contents);
};

#endif  // CRASH_REPORTER_MISSED_CRASH_COLLECTOR_H_
