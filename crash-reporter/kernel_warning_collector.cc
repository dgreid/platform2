// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/kernel_warning_collector.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "crash-reporter/util.h"

namespace {
const char kGenericWarningExecName[] = "kernel-warning";
const char kWifiWarningExecName[] = "kernel-wifi-warning";
const char kSuspendWarningExecName[] = "kernel-suspend-warning";
const char kKernelWarningSignatureKey[] = "sig";
const pid_t kKernelPid = 0;
}  // namespace

using base::FilePath;
using base::StringPrintf;

KernelWarningCollector::KernelWarningCollector()
    : CrashCollector("kernel_warning"), warning_report_path_("/dev/stdin") {}

KernelWarningCollector::~KernelWarningCollector() {}

bool KernelWarningCollector::LoadKernelWarning(std::string* content,
                                               std::string* signature) {
  FilePath kernel_warning_path(warning_report_path_.c_str());
  if (!base::ReadFileToString(kernel_warning_path, content)) {
    PLOG(ERROR) << "Could not open " << kernel_warning_path.value();
    return false;
  }
  // The signature is in the first line.
  std::string::size_type end_position = content->find('\n');
  if (end_position == std::string::npos) {
    LOG(ERROR) << "unexpected kernel warning format";
    return false;
  }
  *signature = content->substr(0, end_position);
  return true;
}

bool KernelWarningCollector::Collect(WarningType type) {
  std::string reason = "normal collection";
  bool feedback = true;
  if (util::IsDeveloperImage()) {
    reason = "always collect from developer builds";
    feedback = true;
  } else if (!is_feedback_allowed_function_()) {
    reason = "no user consent";
    feedback = false;
  }

  LOG(INFO) << "Processing kernel warning: " << reason;

  if (!feedback) {
    return true;
  }

  std::string kernel_warning;
  std::string warning_signature;
  if (!LoadKernelWarning(&kernel_warning, &warning_signature)) {
    return true;
  }

  FilePath root_crash_directory;
  if (!GetCreatedCrashDirectoryByEuid(kRootUid, &root_crash_directory,
                                      nullptr)) {
    return true;
  }

  const char* exec_name;
  if (type == kWifi)
    exec_name = kWifiWarningExecName;
  else if (type == kSuspend)
    exec_name = kSuspendWarningExecName;
  else
    exec_name = kGenericWarningExecName;

  std::string dump_basename =
      FormatDumpBasename(exec_name, time(nullptr), kKernelPid);
  FilePath log_path =
      GetCrashPath(root_crash_directory, dump_basename, "log.gz");
  FilePath meta_path =
      GetCrashPath(root_crash_directory, dump_basename, "meta");
  FilePath kernel_crash_path = root_crash_directory.Append(
      StringPrintf("%s.kcrash", dump_basename.c_str()));

  // We must use WriteNewFile instead of base::WriteFile as we
  // do not want to write with root access to a symlink that an attacker
  // might have created.
  if (WriteNewFile(kernel_crash_path, kernel_warning.data(),
                   kernel_warning.length()) !=
      static_cast<int>(kernel_warning.length())) {
    LOG(INFO) << "Failed to write kernel warning to "
              << kernel_crash_path.value().c_str();
    return true;
  }

  AddCrashMetaData(kKernelWarningSignatureKey, warning_signature);

  // Get the log contents, compress, and attach to crash report.
  bool result = GetLogContents(log_config_path_, exec_name, log_path);
  if (result) {
    AddCrashMetaUploadFile("log", log_path.BaseName().value());
  }

  FinishCrash(meta_path, exec_name, kernel_crash_path.BaseName().value());

  LOG(INFO) << "Stored kernel warning into " << kernel_crash_path.value();
  return true;
}
