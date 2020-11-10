// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The early crash meta collector doesn't collect crashes in the sense that many
// others do. Instead, it moves crashes that happened when the full filesystem
// was not available from ephemeral storage (like /run) to the encrypted
// stateful partition, so that they persist across reboot.

#ifndef CRASH_REPORTER_EPHEMERAL_CRASH_COLLECTOR_H_
#define CRASH_REPORTER_EPHEMERAL_CRASH_COLLECTOR_H_

#include <vector>

#include <base/files/file_path.h>

#include "crash-reporter/crash_collector.h"

// The ephemeral crash collector persists already collected crashes into the
// either the encrypted stateful partition or (in its absence) the encrypted
// reboot vault.
class EphemeralCrashCollector : public CrashCollector {
 public:
  EphemeralCrashCollector();
  EphemeralCrashCollector(const EphemeralCrashCollector&) = delete;
  EphemeralCrashCollector& operator=(const EphemeralCrashCollector&) = delete;

  ~EphemeralCrashCollector() override = default;

  void Initialize(IsFeedbackAllowedFunction is_feedback_allowed_function,
                  bool preserve_across_clobber);

  // Collect early crashes collected into /run/crash_reporter/crash
  bool Collect();

 private:
  bool early_;
  std::vector<base::FilePath> source_directories_;
  friend class EphemeralCrashCollectorTest;
};

#endif  // CRASH_REPORTER_EPHEMERAL_CRASH_COLLECTOR_H_
