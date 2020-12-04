// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/user_collector_base.h"

#include "crash-reporter/vm_support.h"

#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>
#include <re2/re2.h>

#include "crash-reporter/constants.h"
#include "crash-reporter/util.h"

using base::FilePath;
using base::ReadFileToString;
using base::StringPrintf;

namespace {

const char kStatePrefix[] = "State:\t";
const char kUptimeField[] = "ptime";
const char kUserCrashSignal[] = "org.chromium.CrashReporter.UserCrash";

}  // namespace

const char* UserCollectorBase::kUserId = "Uid:\t";
const char* UserCollectorBase::kGroupId = "Gid:\t";

UserCollectorBase::UserCollectorBase(
    const std::string& collector_name,
    CrashDirectorySelectionMethod crash_directory_selection_method)
    : CrashCollector(collector_name,
                     crash_directory_selection_method,
                     kNormalCrashSendMode,
                     collector_name) {}

void UserCollectorBase::Initialize(bool directory_failure, bool early) {
  CrashCollector::Initialize(early);
  initialized_ = true;
  directory_failure_ = directory_failure;
}

void UserCollectorBase::AccounceUserCrash() {
  brillo::ProcessImpl dbus;
  dbus.AddArg("/usr/bin/dbus-send");
  dbus.AddArg("--type=signal");
  dbus.AddArg("--system");
  dbus.AddArg("/");
  dbus.AddArg(kUserCrashSignal);
  // Announce through D-Bus whenever a user crash happens. This is
  // used by the metrics daemon to log active use time between
  // crashes.
  //
  // This could be done more efficiently by explicit fork/exec or
  // using a dbus library directly. However, this should run
  // relatively rarely and longer term we may need to implement a
  // better way to do this that doesn't rely on D-Bus.
  LOG_IF(WARNING, !dbus.Start()) << "dbus-send running failed";

  // We run in the background in case dbus daemon itself is crashed
  // and not responding.  This allows us to not block and potentially
  // deadlock on a dbus-daemon crash.  If dbus-daemon crashes without
  // restarting, each crash will fork off a lot of dbus-send
  // processes.  Such a system is in a unusable state and will need
  // to be restarted anyway.
  dbus.Release();
}

bool UserCollectorBase::HandleCrash(
    const UserCollectorBase::CrashAttributes& attrs, const char* force_exec) {
  CHECK(initialized_);

  base::TimeDelta crash_time;
  GetUptime(&crash_time);

  std::string exec;
  if (force_exec) {
    exec.assign(force_exec);
  } else if (!GetExecutableBaseNameFromPid(attrs.pid, &exec)) {
    // If we cannot find the exec name, use the kernel supplied name.
    // We don't always use the kernel's since it truncates the name to
    // 16 characters.
    exec = StringPrintf("supplied_%s", attrs.exec_name.c_str());
  }

  std::string reason;
  bool dump = ShouldDump(attrs.pid, attrs.uid, exec, &reason);

  // anomaly_detector's CrashReporterParser looks for this message; don't change
  // it without updating the regex.
  const auto message = StringPrintf(
      "Received crash notification for %s[%d] sig %d, user %u group %u",
      exec.c_str(), attrs.pid, attrs.signal, attrs.uid, attrs.gid);

  // TODO(crbug.com/1053847) The executable name is sensitive user data inside
  // the VM, so don't log this message. Eventually we will move the VM logs
  // inside the cryptohome and this will be unnecessary.
  if (!VmSupport::Get()) {
    LogCrash(message, reason);
  }

  if (dump) {
    AccounceUserCrash();

    AddExtraMetadata(exec, attrs.pid);

    bool out_of_capacity = false;
    ErrorType error_type = ConvertAndEnqueueCrash(
        attrs.pid, exec, attrs.uid, attrs.gid, crash_time, &out_of_capacity);
    if (error_type != kErrorNone) {
      if (!out_of_capacity) {
        EnqueueCollectionErrorLog(error_type, exec);
      }
      return false;
    }
  }

  return true;
}

base::Optional<UserCollectorBase::CrashAttributes>
UserCollectorBase::ParseCrashAttributes(const std::string& crash_attributes) {
  RE2 re("(\\d+):(\\d+):(\\d+):(\\d+):(.*)");
  UserCollectorBase::CrashAttributes attrs;
  if (!RE2::FullMatch(crash_attributes, re, &attrs.pid, &attrs.signal,
                      &attrs.uid, &attrs.gid, &attrs.exec_name)) {
    return base::nullopt;
  }
  return attrs;
}

bool UserCollectorBase::ShouldDump(base::Optional<pid_t> pid,
                                   std::string* reason) const {
  VmSupport* vm_support = VmSupport::Get();
  if (vm_support) {
    if (!pid.has_value()) {
      *reason = "ignoring - unknown PID inside VM";
      return false;
    }

    if (!vm_support->ShouldDump(*pid, reason)) {
      return false;
    }
  }

  *reason = "handling";
  return true;
}

bool UserCollectorBase::ShouldDump(std::string* reason) const {
  return ShouldDump(base::nullopt, reason);
}

bool UserCollectorBase::GetFirstLineWithPrefix(
    const std::vector<std::string>& lines,
    const char* prefix,
    std::string* line) {
  for (const auto& current_line : lines) {
    if (current_line.find(prefix) == 0) {
      *line = current_line;
      return true;
    }
  }
  return false;
}

bool UserCollectorBase::GetIdFromStatus(
    const char* prefix,
    IdKind kind,
    const std::vector<std::string>& status_lines,
    int* id) {
  // From fs/proc/array.c:task_state(), this file contains:
  // \nUid:\t<uid>\t<euid>\t<suid>\t<fsuid>\n
  std::string id_line;
  if (!GetFirstLineWithPrefix(status_lines, prefix, &id_line)) {
    return false;
  }
  std::string id_substring = id_line.substr(strlen(prefix), std::string::npos);
  std::vector<std::string> ids = base::SplitString(
      id_substring, "\t", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (ids.size() != kIdMax || kind < 0 || kind >= kIdMax) {
    return false;
  }
  const char* number = ids[kind].c_str();
  char* end_number = nullptr;
  *id = strtol(number, &end_number, 10);
  if (*end_number != '\0') {
    return false;
  }
  return true;
}

bool UserCollectorBase::GetStateFromStatus(
    const std::vector<std::string>& status_lines, std::string* state) {
  std::string state_line;
  if (!GetFirstLineWithPrefix(status_lines, kStatePrefix, &state_line)) {
    return false;
  }
  *state = state_line.substr(strlen(kStatePrefix), std::string::npos);
  return true;
}

bool UserCollectorBase::ClobberContainerDirectory(
    const base::FilePath& container_dir) {
  // Delete a pre-existing directory from crash reporter that may have
  // been left around for diagnostics from a failed conversion attempt.
  // If we don't, existing files can cause forking to fail.
  if (!base::DeletePathRecursively(container_dir)) {
    PLOG(ERROR) << "Could not delete " << container_dir.value();
    return false;
  }

  if (!base::CreateDirectory(container_dir)) {
    PLOG(ERROR) << "Could not create " << container_dir.value();
    return false;
  }

  return true;
}

const FilePath UserCollectorBase::GetCrashProcessingDir() {
  return FilePath("/tmp/crash_reporter");
}

UserCollectorBase::ErrorType UserCollectorBase::ConvertAndEnqueueCrash(
    pid_t pid,
    const std::string& exec,
    uid_t supplied_ruid,
    gid_t supplied_rgid,
    const base::TimeDelta& crash_time,
    bool* out_of_capacity) {
  FilePath crash_path;
  if (!GetCreatedCrashDirectory(pid, supplied_ruid, &crash_path,
                                out_of_capacity)) {
    LOG(ERROR) << "Unable to find/create process-specific crash path";
    return kErrorSystemIssue;
  }

  // Directory like /tmp/crash_reporter/1234 which contains the
  // procfs entries and other temporary files used during conversion.
  const FilePath container_dir =
      GetCrashProcessingDir().Append(StringPrintf("%d", pid));
  if (!ClobberContainerDirectory(container_dir))
    return kErrorSystemIssue;

  std::string dump_basename = FormatDumpBasename(exec, time(nullptr), pid);
  FilePath core_path = GetCrashPath(crash_path, dump_basename, "core");
  FilePath meta_path = GetCrashPath(crash_path, dump_basename, "meta");
  FilePath minidump_path =
      GetCrashPath(crash_path, dump_basename, constants::kMinidumpExtension);
  FilePath log_path = GetCrashPath(crash_path, dump_basename, "log");
  FilePath proc_log_path = GetCrashPath(crash_path, dump_basename, "proclog");

  if (GetLogContents(FilePath(log_config_path_), exec, log_path)) {
    AddCrashMetaUploadFile("log", log_path.BaseName().value());
  }

  if (GetProcessTree(pid, proc_log_path)) {
    AddCrashMetaUploadFile("process_tree", proc_log_path.BaseName().value());
  }

#if USE_DIRENCRYPTION
  // Join the session keyring, if one exists.
  util::JoinSessionKeyring();
#endif  // USE_DIRENCRYPTION

  ErrorType error_type =
      ConvertCoreToMinidump(pid, container_dir, core_path, minidump_path);
  if (error_type != kErrorNone) {
    if (error_type != kErrorReadCoreData)
      LOG(INFO) << "Leaving core file at " << core_path.value()
                << " due to conversion error";
    return error_type;
  } else {
    base::FilePath target;
    if (!NormalizeFilePath(minidump_path, &target))
      target = minidump_path;

    // TODO(crbug.com/1053847) The executable name is sensitive user data inside
    // the VM, so don't log this message. Eventually we will move the VM logs
    // inside the cryptohome and this will be unnecessary.
    if (!VmSupport::Get()) {
      LOG(INFO) << "Stored minidump to " << target.value();
    }
  }

  base::TimeDelta start_time;
  if (GetUptimeAtProcessStart(pid, &start_time) && crash_time > start_time) {
    const base::TimeDelta uptime = crash_time - start_time;
    AddCrashMetaUploadData(kUptimeField,
                           std::to_string(uptime.InMilliseconds()));
  } else {
    LOG(WARNING) << "Failed to get process uptime.";
  }

  // Here we commit to sending this file.  We must not return false
  // after this point or we will generate a log report as well as a
  // crash report.
  FinishCrash(meta_path, exec, minidump_path.BaseName().value());

  if (!util::IsDeveloperImage()) {
    base::DeleteFile(core_path);
  } else {
    LOG(INFO) << "Leaving core file at " << core_path.value()
              << " due to developer image";
  }

  base::DeletePathRecursively(container_dir);
  return kErrorNone;
}

bool UserCollectorBase::GetCreatedCrashDirectory(pid_t pid,
                                                 uid_t supplied_ruid,
                                                 FilePath* crash_file_path,
                                                 bool* out_of_capacity) {
  FilePath process_path = GetProcessPath(pid);
  std::string status;
  if (directory_failure_) {
    LOG(ERROR) << "Purposefully failing to create spool directory";
    return false;
  }

  uid_t uid;
  if (base::ReadFileToString(process_path.Append("status"), &status)) {
    std::vector<std::string> status_lines = base::SplitString(
        status, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

    std::string process_state;
    if (!GetStateFromStatus(status_lines, &process_state)) {
      LOG(ERROR) << "Could not find process state in status file";
      return false;
    }
    LOG(INFO) << "State of crashed process [" << pid << "]: " << process_state;

    // Get effective UID of crashing process.
    int id;
    if (!GetIdFromStatus(kUserId, kIdEffective, status_lines, &id)) {
      LOG(ERROR) << "Could not find euid in status file";
      return false;
    }
    uid = id;
  } else {
    LOG(INFO) << "Using supplied UID " << supplied_ruid
              << " for crashed process [" << pid
              << "] due to error reading status file";
    uid = supplied_ruid;
  }

  if (!GetCreatedCrashDirectoryByEuid(uid, crash_file_path, out_of_capacity)) {
    LOG(ERROR) << "Could not create crash directory";
    return false;
  }
  return true;
}

std::vector<std::string> UserCollectorBase::GetCommandLine(pid_t pid) const {
  const FilePath path = GetProcessPath(pid).Append("cmdline");
  // The /proc/[pid]/cmdline file contains the command line separated and
  // terminated by a null byte, e.g. "command\0arg\0arg\0". The file is
  // empty if the process is a zombie.
  std::string cmdline;
  if (!ReadFileToString(path, &cmdline)) {
    PLOG(ERROR) << "Could not read " << path.value();
    return std::vector<std::string>();
  }

  if (cmdline.empty()) {
    LOG(ERROR) << "Empty cmdline for " << path.value();
    return std::vector<std::string>();
  }

  // Split the string by null bytes.
  return base::SplitString(cmdline, std::string(1, '\0'), base::KEEP_WHITESPACE,
                           base::SPLIT_WANT_ALL);
}
