// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/generic_failure_collector.h"

#include <base/files/file_util.h>
#include <base/logging.h>

#include "crash-reporter/util.h"

namespace {
const char kSignatureKey[] = "sig";
}  // namespace

using base::FilePath;
using base::StringPrintf;

const char* const GenericFailureCollector::kSuspendFailure = "suspend-failure";
const char* const GenericFailureCollector::kServiceFailure = "service-failure";
const char* const GenericFailureCollector::kArcServiceFailure =
    "arc-service-failure";

GenericFailureCollector::GenericFailureCollector()
    : CrashCollector("generic_failure"), failure_report_path_("/dev/stdin") {}

GenericFailureCollector::~GenericFailureCollector() {}

bool GenericFailureCollector::LoadGenericFailure(std::string* content,
                                                 std::string* signature) {
  FilePath failure_report_path(failure_report_path_.c_str());
  if (!base::ReadFileToString(failure_report_path, content)) {
    LOG(ERROR) << "Could not open " << failure_report_path.value();
    return false;
  }

  std::string::size_type end_position = content->find('\n');
  if (end_position == std::string::npos) {
    LOG(ERROR) << "unexpected generic failure format";
    return false;
  }
  *signature = content->substr(0, end_position);
  return true;
}

bool GenericFailureCollector::CollectFull(const std::string& exec_name,
                                          const std::string& log_key_name,
                                          base::Optional<int> weight) {
  LOG(INFO) << "Processing generic failure";

  std::string generic_failure;
  std::string failure_signature;
  if (!LoadGenericFailure(&generic_failure, &failure_signature)) {
    return true;
  }

  FilePath crash_directory;
  if (!GetCreatedCrashDirectoryByEuid(kRootUid, &crash_directory, nullptr)) {
    return true;
  }

  std::string dump_basename = FormatDumpBasename(exec_name, time(nullptr), 0);
  FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");
  FilePath meta_path = GetCrashPath(crash_directory, dump_basename, "meta");
  if (weight) {
    AddCrashMetaUploadData("weight", StringPrintf("%d", *weight));
  }

  AddCrashMetaData(kSignatureKey, failure_signature);

  bool result = GetLogContents(log_config_path_, log_key_name, log_path);
  if (result) {
    FinishCrash(meta_path, exec_name, log_path.BaseName().value());
  }

  return true;
}
