// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_PROCESS_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_PROCESS_FETCHER_H_

#include <sys/types.h>

#include <cstdint>

#include <base/files/file_path.h>
#include <base/optional.h>

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The ProcessFetcher class is responsible for gathering information about a
// particular process on the device.
class ProcessFetcher final {
 public:
  // |process_id| is the PID for the process whose information will be fetched.
  // Only override |root_dir| for testing.
  ProcessFetcher(pid_t process_id,
                 const base::FilePath& root_dir = base::FilePath("/"));
  ProcessFetcher(const ProcessFetcher&) = delete;
  ProcessFetcher& operator=(const ProcessFetcher&) = delete;
  ~ProcessFetcher();

  // Returns information about a particular process on the device, or the error
  // that occurred retrieving the information.
  chromeos::cros_healthd::mojom::ProcessResultPtr FetchProcessInfo();

 private:
  // Parses relevant fields from /proc/|process_id_|/stat. Returns the first
  // error encountered or base::nullopt if no errors occurred. |priority|,
  // |nice| and |start_time_ticks| are only valid if base::nullopt was returned.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr> ParseProcPidStat(
      chromeos::cros_healthd::mojom::ProcessState* state,
      int8_t* priority,
      int8_t* nice,
      uint64_t* start_time_ticks);

  // Calculates the uptime of the process in clock ticks using
  // |start_time_ticks|. Returns the first error encountered or base::nullopt if
  // no errors occurred. |process_uptime_ticks| is only valid if base::nullopt
  // was returned.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>
  CalculateProcessUptime(uint64_t start_time_ticks,
                         uint64_t* process_uptime_ticks);

  // Fetches the real user ID of the process. Returns the first error
  // encountered or base::nullopt if no errors occurred. |user_id| is only
  // valid if base::nullopt was returned.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr> GetProcessUid(
      uid_t* user_id);

  // File paths read will be relative to |root_dir_|. In production, this should
  // be "/", but it can be overridden for testing.
  const base::FilePath root_dir_;
  // Procfs subdirectory with files specific to the process.
  const base::FilePath proc_pid_dir_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_PROCESS_FETCHER_H_
