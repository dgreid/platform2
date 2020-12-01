// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/process_fetcher.h"

#include <unistd.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/numerics/safe_conversions.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"
#include "diagnostics/cros_healthd/utils/procfs_utils.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = chromeos::cros_healthd::mojom;

// Regex used to parse a process's statm file.
constexpr char kProcessStatmFileRegex[] =
    R"((\d+)\s+(\d+)\s+\d+\s+\d+\s+\d+\s+\d+\s+\d+)";
// Regex used to parse procfs's uptime file.
constexpr char kUptimeFileRegex[] = R"(([.\d]+)\s+[.\d]+)";
// Regex used to parse the process's Uid field in the status file.
constexpr char kUidStatusRegex[] = R"(\s*(\d+)\s+\d+\s+\d+\s+\d+)";

// Converts the raw process state read from procfs to a mojo_ipc::ProcessState.
// If the conversion is successful, returns base::nullopt and sets
// |mojo_state_out| to the converted value. If the conversion fails,
// |mojo_state_out| is invalid and an appropriate error is returned.
base::Optional<mojo_ipc::ProbeErrorPtr> GetProcessState(
    const std::string& raw_state, mojo_ipc::ProcessState* mojo_state_out) {
  DCHECK(mojo_state_out);
  // See https://man7.org/linux/man-pages/man5/proc.5.html for allowable raw
  // state values.
  if (raw_state == "R") {
    *mojo_state_out = mojo_ipc::ProcessState::kRunning;
  } else if (raw_state == "S") {
    *mojo_state_out = mojo_ipc::ProcessState::kSleeping;
  } else if (raw_state == "D") {
    *mojo_state_out = mojo_ipc::ProcessState::kWaiting;
  } else if (raw_state == "Z") {
    *mojo_state_out = mojo_ipc::ProcessState::kZombie;
  } else if (raw_state == "T") {
    *mojo_state_out = mojo_ipc::ProcessState::kStopped;
  } else if (raw_state == "t") {
    *mojo_state_out = mojo_ipc::ProcessState::kTracingStop;
  } else if (raw_state == "X") {
    *mojo_state_out = mojo_ipc::ProcessState::kDead;
  } else {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  "Undefined process state: " + raw_state);
  }

  return base::nullopt;
}

// Converts |str| to a signed, 8-bit integer. If the conversion is successful,
// returns base::nullopt and sets |int_out| to the converted value. If the
// conversion fails, |int_out| is invalid and an appropriate error is returned.
base::Optional<mojo_ipc::ProbeErrorPtr> GetInt8FromString(
    const std::string& str, int8_t* int_out) {
  DCHECK(int_out);

  int full_size_int;
  if (!base::StringToInt(str, &full_size_int)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  "Failed to convert " + str + " to int.");
  }

  if (full_size_int > std::numeric_limits<int8_t>::max()) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Integer too large for int8_t: " + std::to_string(full_size_int));
  }

  *int_out = static_cast<int8_t>(full_size_int);

  return base::nullopt;
}

}  // namespace

ProcessFetcher::ProcessFetcher(pid_t process_id, const base::FilePath& root_dir)
    : root_dir_(root_dir),
      proc_pid_dir_(GetProcProcessDirectoryPath(root_dir, process_id)) {}

ProcessFetcher::~ProcessFetcher() = default;

void ProcessFetcher::FetchProcessInfo(
    base::OnceCallback<void(mojo_ipc::ProcessResultPtr)> callback) {
  mojo_ipc::ProcessInfo process_info;

  // Number of ticks after system boot that the process started.
  uint64_t start_time_ticks;
  auto error = ParseProcPidStat(&process_info.state, &process_info.priority,
                                &process_info.nice, &start_time_ticks);
  if (error.has_value()) {
    std::move(callback).Run(
        mojo_ipc::ProcessResult::NewError(std::move(error.value())));
    return;
  }

  error = CalculateProcessUptime(start_time_ticks, &process_info.uptime_ticks);
  if (error.has_value()) {
    std::move(callback).Run(
        mojo_ipc::ProcessResult::NewError(std::move(error.value())));
    return;
  }

  error = ParseProcPidStatm(&process_info.total_memory_kib,
                            &process_info.resident_memory_kib,
                            &process_info.free_memory_kib);
  if (error.has_value()) {
    std::move(callback).Run(
        mojo_ipc::ProcessResult::NewError(std::move(error.value())));
    return;
  }

  uid_t user_id;
  error = GetProcessUid(&user_id);
  if (error.has_value()) {
    std::move(callback).Run(
        mojo_ipc::ProcessResult::NewError(std::move(error.value())));
    return;
  }

  process_info.user_id = static_cast<uint32_t>(user_id);

  if (!ReadAndTrimString(proc_pid_dir_, kProcessCmdlineFile,
                         &process_info.command)) {
    std::move(callback).Run(
        mojo_ipc::ProcessResult::NewError(CreateAndLogProbeError(
            mojo_ipc::ErrorType::kFileReadError,
            "Failed to read " +
                proc_pid_dir_.Append(kProcessCmdlineFile).value())));
    return;
  }

  std::move(callback).Run(
      mojo_ipc::ProcessResult::NewProcessInfo(process_info.Clone()));
  return;
}

base::Optional<mojo_ipc::ProbeErrorPtr> ProcessFetcher::ParseProcPidStat(
    mojo_ipc::ProcessState* state,
    int8_t* priority,
    int8_t* nice,
    uint64_t* start_time_ticks) {
  // Note that start_time_ticks is the only pointer actually dereferenced in
  // this function. The helper functions which set |state|, |priority| and
  // |nice| are responsible for checking the validity of those three pointers.
  DCHECK(start_time_ticks);

  std::string stat_contents;
  const base::FilePath kProcPidStatFile =
      proc_pid_dir_.Append(kProcessStatFile);
  if (!ReadAndTrimString(proc_pid_dir_, kProcessStatFile, &stat_contents)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kFileReadError,
                                  "Failed to read " + kProcPidStatFile.value());
  }

  std::vector<base::StringPiece> stat_tokens =
      base::SplitStringPiece(stat_contents, base::kWhitespaceASCII,
                             base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (stat_tokens.size() <= ProcPidStatIndices::kMaxValue) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to tokenize " + kProcPidStatFile.value());
  }

  auto error = GetProcessState(
      stat_tokens[ProcPidStatIndices::kState].as_string(), state);
  if (error.has_value())
    return error;

  error = GetInt8FromString(
      stat_tokens[ProcPidStatIndices::kPriority].as_string(), priority);
  if (error.has_value())
    return error;

  error = GetInt8FromString(stat_tokens[ProcPidStatIndices::kNice].as_string(),
                            nice);
  if (error.has_value())
    return error;

  base::StringPiece start_time_str =
      stat_tokens[ProcPidStatIndices::kStartTime];
  if (!base::StringToUint64(start_time_str, start_time_ticks)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert starttime to uint64: " + start_time_str.as_string());
  }

  return base::nullopt;
}

base::Optional<mojo_ipc::ProbeErrorPtr> ProcessFetcher::ParseProcPidStatm(
    uint32_t* total_memory_kib,
    uint32_t* resident_memory_kib,
    uint32_t* free_memory_kib) {
  DCHECK(total_memory_kib);
  DCHECK(resident_memory_kib);
  DCHECK(free_memory_kib);

  std::string statm_contents;
  if (!ReadAndTrimString(proc_pid_dir_, kProcessStatmFile, &statm_contents)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Failed to read " + proc_pid_dir_.Append(kProcessStatmFile).value());
  }

  std::string total_memory_pages_str;
  std::string resident_memory_pages_str;
  if (!RE2::FullMatch(statm_contents, kProcessStatmFileRegex,
                      &total_memory_pages_str, &resident_memory_pages_str)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to parse process's statm file: " + statm_contents);
  }

  uint32_t total_memory_pages;
  if (!base::StringToUint(total_memory_pages_str, &total_memory_pages)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert total memory to uint32_t: " +
            total_memory_pages_str);
  }

  uint32_t resident_memory_pages;
  if (!base::StringToUint(resident_memory_pages_str, &resident_memory_pages)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert resident memory to uint32_t: " +
            resident_memory_pages_str);
  }

  if (resident_memory_pages > total_memory_pages) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        base::StringPrintf("Process's resident memory (%u pages) higher than "
                           "total memory (%u pages).",
                           resident_memory_pages, total_memory_pages));
  }

  const auto kPageSizeInBytes = sysconf(_SC_PAGESIZE);
  if (kPageSizeInBytes == -1) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kSystemUtilityError,
                                  "Failed to run sysconf(_SC_PAGESIZE).");
  }

  const auto kPageSizeInKiB = kPageSizeInBytes / 1024;

  *total_memory_kib =
      static_cast<uint32_t>(total_memory_pages * kPageSizeInKiB);
  *resident_memory_kib =
      static_cast<uint32_t>(resident_memory_pages * kPageSizeInKiB);
  *free_memory_kib = static_cast<uint32_t>(
      (total_memory_pages - resident_memory_pages) * kPageSizeInKiB);

  return base::nullopt;
}

base::Optional<mojo_ipc::ProbeErrorPtr> ProcessFetcher::CalculateProcessUptime(
    uint64_t start_time_ticks, uint64_t* process_uptime_ticks) {
  DCHECK(process_uptime_ticks);

  std::string uptime_contents;
  base::FilePath uptime_path = GetProcUptimePath(root_dir_);
  if (!ReadAndTrimString(uptime_path, &uptime_contents)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kFileReadError,
                                  "Failed to read " + uptime_path.value());
  }

  std::string system_uptime_str;
  if (!RE2::FullMatch(uptime_contents, kUptimeFileRegex, &system_uptime_str)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to parse uptime file: " + uptime_contents);
  }

  double system_uptime_seconds;
  if (!base::StringToDouble(system_uptime_str, &system_uptime_seconds)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert system uptime to double: " + system_uptime_str);
  }

  const auto kClockTicksPerSecond = sysconf(_SC_CLK_TCK);
  if (kClockTicksPerSecond == -1) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kSystemUtilityError,
                                  "Failed to run sysconf(_SC_CLK_TCK).");
  }

  *process_uptime_ticks =
      static_cast<uint64_t>(system_uptime_seconds *
                            static_cast<double>(kClockTicksPerSecond)) -
      start_time_ticks;
  return base::nullopt;
}

base::Optional<mojo_ipc::ProbeErrorPtr> ProcessFetcher::GetProcessUid(
    uid_t* user_id) {
  DCHECK(user_id);

  std::string status_contents;
  if (!ReadAndTrimString(proc_pid_dir_, kProcessStatusFile, &status_contents)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Failed to read " + proc_pid_dir_.Append(kProcessStatusFile).value());
  }

  base::StringPairs status_key_value_pairs;
  if (!base::SplitStringIntoKeyValuePairs(status_contents, ':', '\n',
                                          &status_key_value_pairs)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to tokenize status file: " + status_contents);
  }

  bool uid_key_found = false;
  std::string uid_str;
  for (const auto& kv_pair : status_key_value_pairs) {
    if (kv_pair.first != "Uid")
      continue;

    std::string uid_value = kv_pair.second;
    if (!RE2::FullMatch(uid_value, kUidStatusRegex, &uid_str)) {
      return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                    "Failed to parse Uid value: " + uid_value);
    }

    unsigned int user_id_uint;
    if (!base::StringToUint(uid_str, &user_id_uint)) {
      return CreateAndLogProbeError(
          mojo_ipc::ErrorType::kParseError,
          "Failed to convert Uid to uint: " + uid_str);
    }

    *user_id = static_cast<uid_t>(user_id_uint);

    uid_key_found = true;
    break;
  }

  if (!uid_key_found) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kParseError,
                                  "Failed to find Uid key.");
  }

  return base::nullopt;
}

}  // namespace diagnostics
