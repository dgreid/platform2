// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/missed_crash_collector.h"

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

constexpr int64_t MissedCrashCollector::kDefaultChunkSize;

MissedCrashCollector::MissedCrashCollector()
    : CrashCollector("missed_crash"), input_file_(stdin) {}

MissedCrashCollector::~MissedCrashCollector() = default;

// static
bool MissedCrashCollector::ReadFILEToString(FILE* file, std::string* contents) {
  // This is very much a rewording of base::ReadFileToString(), except that:
  // a) We pass in a FILE* instead of opening one. We don't use
  //    base::ReadFileToString() because we often in fd-exhaustion when a missed
  //    crash occurs and we don't want to risk opening more file descriptors.
  // b) We don't try to find a file size since stdin isn't going to give us a
  //    file size.
  size_t bytes_read_this_pass;
  size_t bytes_read_so_far = 0;
  clearerr(file);
  contents->clear();
  contents->resize(kDefaultChunkSize);

  while ((bytes_read_this_pass = fread(&(*contents)[bytes_read_so_far], 1,
                                       kDefaultChunkSize, file)) > 0) {
    bytes_read_so_far += bytes_read_this_pass;
    // Last fread syscall (after EOF) can be avoided via feof, which is just a
    // flag check.
    if (feof(file))
      break;
    contents->resize(bytes_read_so_far + kDefaultChunkSize);
  }
  bool read_status = !ferror(file);
  contents->resize(bytes_read_so_far);

  return read_status;
}

bool MissedCrashCollector::Collect(int pid) {
  std::string reason = "normal collection";
  bool feedback = true;
  if (!is_feedback_allowed_function_()) {
    reason = "no user consent";
    feedback = false;
  }

  LOG(INFO) << "Processing missed crash for process " << pid << ": " << reason;

  if (!feedback) {
    return true;
  }

  std::string logs;
  if (!ReadFILEToString(input_file_, &logs)) {
    LOG(ERROR) << "Could not read input logs";
    logs += "<failed read>";
    // Keep going in hopes of getting some information.
  }

  base::FilePath crash_directory;
  // We always use kRootUid here (and thus write to /var/spool/crash), even
  // though the missed crash was probably under user ID 1000. Since we only
  // read system logs and system information, there should be no user-specific
  // information in the logs (that is, the logs don't contain anything from
  // the user's cryptohome). Furthermore, since we are launched by
  // anomaly_detector, we are inside anomaly_detector's minijail. Using the
  // "correct" userid here would mean allowing writes to many more locations in
  // that minijail config. I'd rather keep the write restrictions as tight as
  // possible unless we actually have sensitive information here.
  if (!GetCreatedCrashDirectoryByEuid(kRootUid, &crash_directory, nullptr)) {
    LOG(WARNING) << "Could not get crash directory (full?)";
    return true;
  }

  StripSensitiveData(&logs);

  constexpr char kExecName[] = "missed_crash";
  std::string dump_basename = FormatDumpBasename(kExecName, time(nullptr), pid);
  const base::FilePath log_path =
      GetCrashPath(crash_directory, dump_basename, "log.gz");
  const base::FilePath meta_path =
      GetCrashPath(crash_directory, dump_basename, "meta");
  if (!WriteNewCompressedFile(log_path, logs.data(), logs.size())) {
    PLOG(WARNING) << "Error writing sanitized log to " << log_path.value();
  }

  AddCrashMetaData("sig", "missed-crash");
  AddCrashMetaUploadData("pid", base::NumberToString(pid));

  FinishCrash(meta_path, kExecName, log_path.BaseName().value());

  return true;
}
