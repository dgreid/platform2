// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/arc_collector.h"

#include <sysexits.h>
#include <unistd.h>

#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringize_macros.h>
#include <base/time/time.h>
#include <brillo/key_value_store.h>
#include <brillo/process.h>

using base::File;
using base::FilePath;
using base::ReadFileToString;
using base::TimeDelta;
using base::TimeTicks;

using brillo::ProcessImpl;

namespace {

const FilePath kContainersDir("/run/containers");
const FilePath::StringType kArcDirPattern("android_*");
const FilePath kContainerPid("container.pid");

const FilePath kArcRootPrefix("/opt/google/containers/android/rootfs/root");
const FilePath kArcBuildProp("system/build.prop");  // Relative to ARC root.

// TODO(domlaskowski): Dispatch to core_collector{,32} at run time. Note that
// ARC processes are always 32-bit on 64-bit platforms.
const char kCoreCollectorPath[] = "/usr/bin/core_collector"
#if __WORDSIZE == 64
    "32"
#endif
    "";

const char kChromePath[] = "/opt/google/chrome/chrome";

const char kArcProduct[] = "ChromeOS_ARC";

// Metadata fields included in reports.
const char kArcVersionField[] = "arc_version";
const char kBoardField[] = "board";
const char kChromeOsVersionField[] = "chrome_os_version";
const char kCpuAbiField[] = "cpu_abi";
const char kCrashTypeField[] = "crash_type";
const char kDeviceField[] = "device";
const char kExceptionInfoField[] = "exception_info";
const char kProcessField[] = "process";
const char kProductField[] = "prod";
const char kSignatureField[] = "sig";
const char kUptimeField[] = "uptime";

// Keys for crash log headers.
const char kBuildKey[] = "Build";
const char kProcessKey[] = "Process";
const char kSubjectKey[] = "Subject";

// Keys for build properties.
const char kBoardProperty[] = "ro.product.board";
const char kCpuAbiProperty[] = "ro.product.cpu.abi";
const char kDeviceProperty[] = "ro.product.device";
const char kFingerprintProperty[] = "ro.build.fingerprint";

const size_t kBufferSize = 4096;

inline bool IsAppProcess(const std::string &name) {
  return name == "app_process32" || name == "app_process64";
}

inline TimeTicks ToSeconds(const TimeTicks &time) {
  return TimeTicks::FromInternalValue(
      TimeDelta::FromSeconds(TimeDelta::FromInternalValue(
          time.ToInternalValue()).InSeconds()).ToInternalValue());
}

bool ReadCrashLogFromStdin(std::stringstream *stream);

bool HasExceptionInfo(const std::string &type);
const char *GetSubjectTag(const std::string &type);

bool GetChromeVersion(std::string *version);
bool GetArcProperties(std::string *version,
                      std::string *device,
                      std::string *board,
                      std::string *cpu_abi);

// Runs |process| and redirects |fd| to |output|. Returns the exit code, or -1
// if the process failed to start.
int RunAndCaptureOutput(ProcessImpl *process, int fd, std::string *output);

std::string FormatDuration(uint64_t seconds);

}  // namespace

ArcCollector::ArcCollector()
    : ArcCollector(ContextPtr(new ArcContext(this))) {
}

ArcCollector::ArcCollector(ContextPtr context)
    : UserCollectorBase("ARC", true),
      context_(std::move(context)) {
}

bool ArcCollector::IsArcProcess(pid_t pid) const {
  pid_t arc_pid;
  if (!context_->GetArcPid(&arc_pid)) {
    LOG(ERROR) << "Failed to get PID of ARC container";
    return false;
  }
  std::string arc_ns;
  if (!context_->GetPidNamespace(arc_pid, &arc_ns)) {
    LOG(ERROR) << "Failed to get PID namespace of ARC container";
    return false;
  }
  std::string ns;
  if (!context_->GetPidNamespace(pid, &ns)) {
    LOG(ERROR) << "Failed to get PID namespace of process";
    return false;
  }
  return ns == arc_ns;
}

bool ArcCollector::HandleJavaCrash(const std::string &crash_type,
                                   const std::string &device,
                                   const std::string &board,
                                   const std::string &cpu_abi) {
  std::string reason;
  const bool should_dump = UserCollectorBase::ShouldDump(
      is_feedback_allowed_function_(), IsDeveloperImage(), &reason);

  std::ostringstream message;
  message << "Received " << crash_type << " notification";

  if (!should_dump) {
    LogCrash(message.str(), reason);
    close(STDIN_FILENO);
    return true;
  }

  std::stringstream stream;
  if (!ReadCrashLogFromStdin(&stream)) {
    LOG(ERROR) << "Failed to read crash log";
    return false;
  }

  CrashLogHeaderMap map;
  std::string exception_info;
  if (!ParseCrashLog(crash_type, &stream, &map, &exception_info)) {
    LOG(ERROR) << "Failed to parse crash log";
    return false;
  }

  const auto exec = GetCrashLogHeader(map, kProcessKey);
  message << " for " << exec;
  LogCrash(message.str(), reason);

  count_crash_function_();

  bool out_of_capacity = false;
  if (!CreateReportForJavaCrash(crash_type, device, board, cpu_abi,
                                map, exception_info, stream.str(),
                                &out_of_capacity)) {
    if (!out_of_capacity)
      EnqueueCollectionErrorLog(0, kErrorSystemIssue, exec);

    return false;
  }

  return true;
}

// static
bool ArcCollector::IsArcRunning() {
  return GetArcPid(nullptr);
}

// static
bool ArcCollector::GetArcPid(pid_t *arc_pid) {
  base::FileEnumerator containers(
      kContainersDir, false, base::FileEnumerator::DIRECTORIES, kArcDirPattern);

  for (FilePath container = containers.Next();
       !container.empty();
       container = containers.Next()) {
    std::string contents;
    if (!ReadFileToString(container.Append(kContainerPid), &contents) ||
        contents.empty())
      continue;

    contents.pop_back();  // Trim EOL.

    pid_t pid;
    if (!base::StringToInt(contents, &pid) ||
        !base::PathExists(GetProcessPath(pid)))
      continue;

    if (arc_pid)
      *arc_pid = pid;

    return true;
  }

  return false;
}

bool ArcCollector::ArcContext::GetArcPid(pid_t *pid) const {
  return ArcCollector::GetArcPid(pid);
}

bool ArcCollector::ArcContext::GetPidNamespace(pid_t pid,
                                               std::string *ns) const {
  const FilePath path = GetProcessPath(pid).Append("ns").Append("pid");

  // The /proc/[pid]/ns/pid file is a special symlink that resolves to a string
  // containing the inode number of the PID namespace, e.g. "pid:[4026531838]".
  FilePath target;
  if (!collector_->GetSymlinkTarget(path, &target))
    return false;

  *ns = target.value();
  return true;
}

bool ArcCollector::ArcContext::GetExeBaseName(pid_t pid,
                                              std::string *exe) const {
  return collector_->CrashCollector::GetExecutableBaseNameFromPid(pid, exe);
}

bool ArcCollector::ArcContext::GetCommand(pid_t pid,
                                          std::string *command) const {
  const FilePath path = GetProcessPath(pid).Append("cmdline");
  // The /proc/[pid]/cmdline file contains the command line separated and
  // terminated by a null byte, e.g. "command\0arg\0arg\0". The file is
  // empty if the process is a zombie.
  if (!ReadFileToString(path, command))
    return false;
  const auto pos = command->find('\0');
  if (pos == std::string::npos)
    return false;
  command->erase(pos);  // Discard command-line arguments.
  return true;
}

std::string ArcCollector::GetVersion() const {
  std::string version;
  return GetChromeVersion(&version) ? version : kUnknownVersion;
}

bool ArcCollector::GetExecutableBaseNameFromPid(pid_t pid,
                                                std::string *base_name) {
  if (!context_->GetExeBaseName(pid, base_name))
    return false;

  // The runtime for non-native ARC apps overwrites its command line with the
  // package name of the app, so use that instead.
  if (IsArcProcess(pid) && IsAppProcess(*base_name)) {
    if (!context_->GetCommand(pid, base_name))
      LOG(ERROR) << "Failed to get package name";
  }
  return true;
}

bool ArcCollector::ShouldDump(pid_t pid,
                              uid_t uid,
                              const std::string &exec,
                              std::string *reason) {
  if (!IsArcProcess(pid)) {
    *reason = "ignoring - crash origin is not ARC";
    return false;
  }

  if (uid >= kSystemUserEnd) {
    *reason = "ignoring - not a system process";
    return false;
  }

  return UserCollectorBase::ShouldDump(
      is_feedback_allowed_function_(), IsDeveloperImage(), reason);
}

UserCollectorBase::ErrorType ArcCollector::ConvertCoreToMinidump(
    pid_t pid,
    const base::FilePath &container_dir,
    const base::FilePath &core_path,
    const base::FilePath &minidump_path) {
  ProcessImpl core_collector;
  core_collector.AddArg(kCoreCollectorPath);
  core_collector.AddArg("--minidump");
  core_collector.AddArg(minidump_path.value());
  core_collector.AddArg("--coredump");
  core_collector.AddArg(core_path.value());
  core_collector.AddArg("--proc");
  core_collector.AddArg(container_dir.value());
  core_collector.AddArg("--prefix");
  core_collector.AddArg(kArcRootPrefix.value());

  std::string error;
  int exit_code = RunAndCaptureOutput(&core_collector, STDERR_FILENO, &error);

  if (exit_code < 0) {
    LOG(ERROR) << "Failed to start " << kCoreCollectorPath;
    return kErrorSystemIssue;
  }

  if (exit_code == EX_OK) {
    std::string process;
    ArcCollector::GetExecutableBaseNameFromPid(pid, &process);
    AddArcMetaData(process, "native_crash", true);
    return kErrorNone;
  }

  std::istringstream in(error);
  std::string line;

  while (std::getline(in, line))
    LOG(ERROR) << line;

  LOG(ERROR) << kCoreCollectorPath << " failed with exit code " << exit_code;
  switch (exit_code) {
    case EX_OSFILE:
      return kErrorInvalidCoreFile;
    case EX_SOFTWARE:
      return kErrorCore2MinidumpConversion;
    default:
      return base::PathExists(core_path) ? kErrorSystemIssue :
                                           kErrorReadCoreData;
  }
}

void ArcCollector::AddArcMetaData(const std::string &process,
                                  const std::string &crash_type,
                                  bool add_arc_properties) {
  AddCrashMetaUploadData(kProductField, kArcProduct);
  AddCrashMetaUploadData(kProcessField, process);
  AddCrashMetaUploadData(kCrashTypeField, crash_type);
  AddCrashMetaUploadData(kChromeOsVersionField, CrashCollector::GetVersion());

  std::string version, device, board, cpu_abi;

  if (add_arc_properties &&
      GetArcProperties(&version, &device, &board, &cpu_abi)) {
    AddCrashMetaUploadData(kArcVersionField, version);
    AddCrashMetaUploadData(kDeviceField, device);
    AddCrashMetaUploadData(kBoardField, board);
    AddCrashMetaUploadData(kCpuAbiField, cpu_abi);
  }

  int64_t start_time;
  brillo::ErrorPtr error;
  if (!session_manager_proxy_->GetArcStartTimeTicks(&start_time, &error)) {
    LOG(ERROR) << "Failed to get ARC uptime: " << error->GetMessage();
    return;
  }

  const uint64_t delta = static_cast<uint64_t>((TimeTicks::Now() -
      TimeTicks::FromInternalValue(start_time)).InSeconds());
  AddCrashMetaUploadData(kUptimeField, FormatDuration(delta));
}

// static
std::string ArcCollector::GetCrashLogHeader(const CrashLogHeaderMap &map,
                                            const char *key) {
  const auto it = map.find(key);
  return it == map.end() ? "unknown" : it->second;
}

// static
bool ArcCollector::ParseCrashLog(const std::string &type,
                                 std::stringstream *stream,
                                 CrashLogHeaderMap *map,
                                 std::string *exception_info) {
  std::string line;

  // The last header is followed by an empty line.
  while (std::getline(*stream, line) && !line.empty()) {
    const auto end = line.find(':');

    if (end != std::string::npos) {
      const auto begin = line.find_first_not_of(' ', end + 1);

      if (begin != std::string::npos) {
        map->emplace(line.substr(0, end), line.substr(begin));
        continue;
      }
    }

    // Ignore malformed headers. The report is still created, but the associated
    // metadata fields are set to "unknown".
    LOG(WARNING) << "Header has unexpected format: " << line;
  }

  if (stream->fail())
    return false;

  if (HasExceptionInfo(type)) {
    std::ostringstream out;
    out << stream->rdbuf();
    *exception_info = out.str();
  }

  return true;
}

bool ArcCollector::CreateReportForJavaCrash(const std::string &crash_type,
                                            const std::string &device,
                                            const std::string &board,
                                            const std::string &cpu_abi,
                                            const CrashLogHeaderMap &map,
                                            const std::string &exception_info,
                                            const std::string &log,
                                            bool *out_of_capacity) {
  FilePath crash_dir;
  if (!GetCreatedCrashDirectoryByEuid(geteuid(), &crash_dir, out_of_capacity)) {
    LOG(ERROR) << "Failed to create or find crash directory";
    return false;
  }

  const auto process = GetCrashLogHeader(map, kProcessKey);

  // FormatDumpBasename relies on the assumption that the combination of process
  // name, timestamp, and PID is unique. This does not hold if a process crashes
  // more than once in the span of a second. While this is improbable for native
  // crashes, Java crashes are not always fatal and may happen in bursts. Hence,
  // ensure uniqueness by replacing the PID with the number of microseconds
  // since the current second.
  const auto now = TimeTicks::Now();
  const pid_t dt = static_cast<pid_t>((now - ToSeconds(now)).InMicroseconds());

  const auto basename = FormatDumpBasename(process, std::time(nullptr), dt);
  const FilePath log_path = GetCrashPath(crash_dir, basename, "log");

  const int size = static_cast<int>(log.size());
  if (WriteNewFile(log_path, log.c_str(), size) != size) {
    PLOG(ERROR) << "Failed to write log";
    return false;
  }

  AddArcMetaData(process, crash_type, false);
  AddCrashMetaUploadData(kArcVersionField, GetCrashLogHeader(map, kBuildKey));
  AddCrashMetaUploadData(kDeviceField, device);
  AddCrashMetaUploadData(kBoardField, board);
  AddCrashMetaUploadData(kCpuAbiField, cpu_abi);

  if (exception_info.empty()) {
    if (const char * const tag = GetSubjectTag(crash_type)) {
      std::ostringstream out;
      out << '[' << tag << "] " << GetCrashLogHeader(map, kSubjectKey);

      AddCrashMetaData(kSignatureField, out.str());
    } else {
      LOG(ERROR) << "Invalid crash type: " << crash_type;
      return false;
    }
  } else {
    const FilePath info_path = GetCrashPath(crash_dir, basename, "info");
    const int size = static_cast<int>(exception_info.size());

    if (WriteNewFile(info_path, exception_info.c_str(), size) != size) {
      PLOG(ERROR) << "Failed to write exception info";
      return false;
    }

    AddCrashMetaUploadText(kExceptionInfoField, info_path.value());
  }

  const FilePath meta_path = GetCrashPath(crash_dir, basename, "meta");
  WriteCrashMetaData(meta_path, process, log_path.value());
  return true;
}

namespace {

bool ReadCrashLogFromStdin(std::stringstream *stream) {
  File src(STDIN_FILENO);
  char buffer[kBufferSize];

  while (true) {
    const int count = src.ReadAtCurrentPosNoBestEffort(buffer, kBufferSize);
    if (count < 0)
      return false;

    if (count == 0)
      return stream->tellp() > 0;  // Crash log should not be empty.

    stream->write(buffer, count);
  }
}

bool HasExceptionInfo(const std::string &type) {
  static const std::unordered_set<std::string> kTypes = {
    "system_app_crash",
    "system_app_wtf",
    "system_server_crash",
    "system_server_wtf"
  };
  return kTypes.count(type);
}

const char *GetSubjectTag(const std::string &type) {
  static const std::unordered_map<std::string, const char *> kTags = {
    { "system_app_anr", "ANR" },
    { "system_server_watchdog", "system server watchdog" }
  };

  const auto it = kTags.find(type);
  return it == kTags.cend() ? nullptr : it->second;
}

bool GetChromeVersion(std::string *version) {
  ProcessImpl chrome;
  chrome.AddArg(kChromePath);
  chrome.AddArg("--product-version");

  int exit_code = RunAndCaptureOutput(&chrome, STDOUT_FILENO, version);
  if (exit_code != EX_OK || version->empty()) {
    LOG(ERROR) << "Failed to get Chrome version";
    return false;
  }

  version->pop_back();  // Discard EOL.
  return true;
}

bool GetArcProperties(std::string *version,
                      std::string *device,
                      std::string *board,
                      std::string *cpu_abi) {
  brillo::KeyValueStore store;
  if (store.Load(kArcRootPrefix.Append(kArcBuildProp)) &&
      store.GetString(kFingerprintProperty, version) &&
      store.GetString(kDeviceProperty, device) &&
      store.GetString(kBoardProperty, board) &&
      store.GetString(kCpuAbiProperty, cpu_abi))
    return true;

  LOG(ERROR) << "Failed to get ARC properties";
  return false;
}

int RunAndCaptureOutput(ProcessImpl *process, int fd, std::string *output) {
  process->RedirectUsingPipe(fd, false);
  if (process->Start()) {
    const int out = process->GetPipe(fd);
    char buffer[kBufferSize];
    output->clear();

    while (true) {
      const ssize_t count = HANDLE_EINTR(read(out, buffer, kBufferSize));
      if (count < 0) {
        process->Wait();
        break;
      }

      if (count == 0)
        return process->Wait();

      output->append(buffer, count);
    }
  }

  return -1;
}

std::string FormatDuration(uint64_t seconds) {
  constexpr uint64_t kSecondsPerMinute = 60;
  constexpr uint64_t kSecondsPerHour = 60 * kSecondsPerMinute;
  constexpr uint64_t kSecondsPerDay = 24 * kSecondsPerHour;

  const auto days = seconds / kSecondsPerDay;
  seconds %= kSecondsPerDay;
  const auto hours = seconds / kSecondsPerHour;
  seconds %= kSecondsPerHour;
  const auto minutes = seconds / kSecondsPerMinute;
  seconds %= kSecondsPerMinute;

  std::ostringstream out;

  if (days > 0)
    out << days << "d ";
  if (days > 0 || hours > 0)
    out << hours << "h ";
  if (days > 0 || hours > 0 || minutes > 0)
    out << minutes << "min ";

  out << seconds << 's';
  return out.str();
}

}  // namespace
