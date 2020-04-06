// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/kernel_warning_collector.h"

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>
#include <re2/re2.h>

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

// Extract the crashing function name from the signature.
// Signature example: 6a839c19-lkdtm_do_action+0x225/0x5bc
// Signature example2: 6a839c19-unknown-function+0x161/0x344 [iwlmvm]
constexpr LazyRE2 sig_re = {R"(^[0-9a-fA-F]+-([0-9a-zA-Z_-]+)\+.*$)"};

bool KernelWarningCollector::LoadKernelWarning(std::string* content,
                                               std::string* signature,
                                               std::string* func_name) {
  FilePath kernel_warning_path(warning_report_path_.c_str());
  if (!base::ReadFileToString(kernel_warning_path, content)) {
    PLOG(ERROR) << "Could not open " << kernel_warning_path.value();
    return false;
  }
  // The signature is in the first or second line.
  // First, try the first, and if it's not there, try the second.
  std::string::size_type end_position = content->find('\n');
  if (end_position == std::string::npos) {
    LOG(ERROR) << "unexpected kernel warning format";
    return false;
  }
  size_t start = 0;
  for (int i = 0; i < 2; i++) {
    *signature = content->substr(start, end_position - start);

    if (RE2::FullMatch(*signature, *sig_re, func_name)) {
      return true;
    } else {
      LOG(INFO) << *signature << " does not match regex";
      signature->clear();
      func_name->clear();
    }

    // Else, try the next line.
    start = end_position + 1;
    end_position = content->find('\n', start);
  }

  LOG(WARNING) << "Couldn't find match for signature line. "
               << "Falling back to first line of warning.";
  *signature = content->substr(0, content->find('\n'));
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
  std::string func_name;
  if (!LoadKernelWarning(&kernel_warning, &warning_signature, &func_name)) {
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

  // Attempt to make the exec_name more unique to avoid collisions.
  if (!func_name.empty()) {
    func_name.insert(func_name.begin(), '_');
  } else {
    LOG(WARNING) << "Couldn't extract function name from signature. "
                    "Going on without it.";
  }

  std::string dump_basename = FormatDumpBasename(
      base::StrCat({exec_name, func_name}), time(nullptr), kKernelPid);
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

  return true;
}
