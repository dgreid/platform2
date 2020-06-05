// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/service_failure_collector.h"

#include <base/files/file_util.h>
#include <base/logging.h>

#include "crash-reporter/util.h"

namespace {
const char kSignatureKey[] = "sig";
}  // namespace

using base::FilePath;
using base::StringPrintf;

ServiceFailureCollector::ServiceFailureCollector()
    : CrashCollector("service_failure"),
      failure_report_path_("/dev/stdin"),
      exec_name_("service-failure") {}

ServiceFailureCollector::~ServiceFailureCollector() {}

bool ServiceFailureCollector::LoadServiceFailure(std::string* signature) {
  FilePath failure_report_path(failure_report_path_);
  if (!base::ReadFileToString(failure_report_path, signature)) {
    PLOG(ERROR) << "Could not open " << failure_report_path_;
    return false;
  }
  // The report is a single line with the signature. Chop off the first newline
  // character and anything that might follow it.
  std::string::size_type end_position = signature->find('\n');
  if (end_position != std::string::npos) {
    signature->resize(end_position);
  }

  return !signature->empty();
}

bool ServiceFailureCollector::Collect(const std::string& service_name) {
  std::string reason = "normal collection";
  bool feedback = true;
  if (util::IsDeveloperImage()) {
    reason = "always collect from developer builds";
    feedback = true;
  } else if (!is_feedback_allowed_function_()) {
    reason = "no user consent";
    feedback = false;
  }

  LOG(INFO) << "Processing service failure: " << reason;

  if (!feedback) {
    return true;
  }

  std::string failure_signature;
  if (!LoadServiceFailure(&failure_signature)) {
    return true;
  }

  FilePath crash_directory;
  if (!GetCreatedCrashDirectoryByEuid(kRootUid, &crash_directory, nullptr)) {
    return true;
  }

  std::string dump_basename =
      FormatDumpBasename(exec_name_ + "-" + service_name, time(nullptr), 0);
  FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");
  FilePath meta_path = GetCrashPath(crash_directory, dump_basename, "meta");

  AddCrashMetaUploadData("weight",
                         StringPrintf("%d", util::GetServiceFailureWeight()));
  AddCrashMetaData(kSignatureKey, failure_signature);

  bool result = GetLogContents(log_config_path_, exec_name_, log_path);
  if (result) {
    FinishCrash(meta_path, exec_name_ + "-" + service_name,
                log_path.BaseName().value());
  }

  return true;
}
