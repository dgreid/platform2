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
#include <vector>

#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringize_macros.h>
#include <base/time/time.h>
#include <brillo/key_value_store.h>
#include <brillo/process/process.h>

#include "crash-reporter/arc_util.h"
#include "crash-reporter/util.h"

using base::File;
using base::FilePath;
using base::ReadFileToString;
using base::TimeDelta;
using base::TimeTicks;

using brillo::ProcessImpl;

namespace {

const FilePath kContainersDir("/run/containers");
const char kArcDirPattern[] = "android*";
const FilePath kContainerPid("container.pid");

const FilePath kArcBuildProp("system/build.prop");  // Relative to ARC root.

const char kCoreCollectorPath[] = "/usr/bin/core_collector";
const char kCoreCollector32Path[] = "/usr/bin/core_collector32";

const char kChromePath[] = "/opt/google/chrome/chrome";

// Keys for build properties.
const char kBoardProperty[] = "ro.product.board";
const char kCpuAbiProperty[] = "ro.product.cpu.abi";
const char kDeviceProperty[] = "ro.product.device";
const char kFingerprintProperty[] = "ro.build.fingerprint";

const size_t kBufferSize = 4096;

inline bool IsAppProcess(const std::string& name) {
  return name == "app_process32" || name == "app_process64";
}

bool ReadCrashLogFromStdin(std::stringstream* stream);

bool GetChromeVersion(std::string* version);

bool GetArcRoot(FilePath* root);
bool GetArcProperties(ArcCollector::BuildProperty* build_property);

std::string FormatDuration(uint64_t seconds);

}  // namespace

ArcCollector::ArcCollector() : ArcCollector(ContextPtr(new ArcContext(this))) {}

ArcCollector::ArcCollector(ContextPtr context)
    : UserCollectorBase("ARC", kAlwaysUseUserCrashDirectory),
      context_(std::move(context)) {}

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

bool ArcCollector::HandleJavaCrash(const std::string& crash_type,
                                   const BuildProperty& build_property) {
  std::string reason;
  const bool should_dump = UserCollectorBase::ShouldDump(
      is_feedback_allowed_function_(), util::IsDeveloperImage(), &reason);

  std::ostringstream message;
  message << "Received " << crash_type << " notification";

  if (!should_dump) {
    LogCrash(message.str(), reason);
    close(STDIN_FILENO);
    return true;
  }

  std::stringstream stream;
  if (!ReadCrashLogFromStdin(&stream)) {
    PLOG(ERROR) << "Failed to read crash log";
    return false;
  }

  CrashLogHeaderMap map;
  std::string exception_info, log;
  if (!arc_util::ParseCrashLog(crash_type, &stream, &map, &exception_info,
                               &log)) {
    LOG(ERROR) << "Failed to parse crash log";
    return false;
  }

  const auto exec = arc_util::GetCrashLogHeader(map, arc_util::kProcessKey);
  message << " for " << exec;
  LogCrash(message.str(), reason);

  bool out_of_capacity = false;
  if (!CreateReportForJavaCrash(crash_type, build_property, map, exception_info,
                                log, &out_of_capacity)) {
    if (!out_of_capacity) {
      EnqueueCollectionErrorLog(kErrorSystemIssue);
    }
    return false;
  }

  return true;
}

// static
bool ArcCollector::IsArcRunning() {
  return GetArcPid(nullptr);
}

// static
bool ArcCollector::GetArcPid(pid_t* arc_pid) {
  base::FileEnumerator containers(
      kContainersDir, false, base::FileEnumerator::DIRECTORIES, kArcDirPattern);

  for (FilePath container = containers.Next(); !container.empty();
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

bool ArcCollector::ArcContext::GetArcPid(pid_t* pid) const {
  return ArcCollector::GetArcPid(pid);
}

bool ArcCollector::ArcContext::GetPidNamespace(pid_t pid,
                                               std::string* ns) const {
  const FilePath path = GetProcessPath(pid).Append("ns").Append("pid");

  // The /proc/[pid]/ns/pid file is a special symlink that resolves to a string
  // containing the inode number of the PID namespace, e.g. "pid:[4026531838]".
  FilePath target;
  if (!base::ReadSymbolicLink(path, &target)) {
    PLOG(ERROR) << "Failed reading symbolic link: " << path.value();
    return false;
  }

  *ns = target.value();
  return true;
}

bool ArcCollector::ArcContext::GetExeBaseName(pid_t pid,
                                              std::string* exe) const {
  return collector_->CrashCollector::GetExecutableBaseNameFromPid(pid, exe);
}

bool ArcCollector::ArcContext::GetCommand(pid_t pid,
                                          std::string* command) const {
  std::vector<std::string> args = collector_->GetCommandLine(pid);
  if (args.size() == 0)
    return false;
  // Return the command and discard the arguments.
  *command = args[0];
  return true;
}

bool ArcCollector::ArcContext::ReadAuxvForProcess(pid_t pid,
                                                  std::string* contents) const {
  // The architecture with the largest auxv size is powerpc with 400 bytes.
  // Round it up to the next power of two.
  constexpr size_t kMaxAuxvSize = 512;
  const FilePath auxv_path = GetProcessPath(pid).Append("auxv");
  return base::ReadFileToStringWithMaxSize(auxv_path, contents, kMaxAuxvSize);
}

std::string ArcCollector::GetOsVersion() const {
  std::string version;
  return GetChromeVersion(&version) ? version : kUnknownValue;
}

bool ArcCollector::GetExecutableBaseNameFromPid(pid_t pid,
                                                std::string* base_name) {
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
                              const std::string& exec,
                              std::string* reason) {
  if (!IsArcProcess(pid)) {
    *reason = "ignoring - crash origin is not ARC";
    return false;
  }

  if (uid >= kSystemUserEnd) {
    *reason = "ignoring - not a system process";
    return false;
  }

  return UserCollectorBase::ShouldDump(is_feedback_allowed_function_(),
                                       util::IsDeveloperImage(), reason);
}

UserCollectorBase::ErrorType ArcCollector::ConvertCoreToMinidump(
    pid_t pid,
    const base::FilePath& container_dir,
    const base::FilePath& core_path,
    const base::FilePath& minidump_path) {
  FilePath root;
  if (!GetArcRoot(&root)) {
    LOG(ERROR) << "Failed to get ARC root";
    return kErrorSystemIssue;
  }

  const char* collector_path = kCoreCollectorPath;
  bool is_64_bit;
  ErrorType elf_class_error = Is64BitProcess(pid, &is_64_bit);
  // Still try to run core_collector32 if 64-bit detection failed.
  if (__WORDSIZE == 64 && (elf_class_error != kErrorNone || !is_64_bit))
    collector_path = kCoreCollector32Path;

  ProcessImpl core_collector;
  core_collector.AddArg(collector_path);
  core_collector.AddArg("--minidump");
  core_collector.AddArg(minidump_path.value());
  core_collector.AddArg("--coredump");
  core_collector.AddArg(core_path.value());
  core_collector.AddArg("--proc");
  core_collector.AddArg(container_dir.value());
  core_collector.AddArg("--prefix");
  core_collector.AddArg(root.value());

  std::string error;
  int exit_code =
      util::RunAndCaptureOutput(&core_collector, STDERR_FILENO, &error);

  if (exit_code < 0) {
    PLOG(ERROR) << "Failed to start " << collector_path;
    return kErrorSystemIssue;
  }

  if (exit_code == EX_OK) {
    std::string process;
    ArcCollector::GetExecutableBaseNameFromPid(pid, &process);
    AddArcMetaData(process, "native_crash", true);
    return kErrorNone;
  }

  util::LogMultilineError(error);

  LOG(ERROR) << collector_path << " failed with exit code " << exit_code;
  switch (exit_code) {
    case EX_OSFILE:
      return kErrorInvalidCoreFile;
    case EX_SOFTWARE:
      return kErrorCore2MinidumpConversion;
    default:
      return base::PathExists(core_path) ? kErrorSystemIssue
                                         : kErrorReadCoreData;
  }
}

void ArcCollector::AddArcMetaData(const std::string& process,
                                  const std::string& crash_type,
                                  bool add_arc_properties) {
  AddCrashMetaUploadData(arc_util::kProductField, arc_util::kArcProduct);
  AddCrashMetaUploadData(arc_util::kProcessField, process);
  AddCrashMetaUploadData(arc_util::kCrashTypeField, crash_type);
  AddCrashMetaUploadData(arc_util::kChromeOsVersionField,
                         CrashCollector::GetOsVersion());

  BuildProperty build_property;

  if (add_arc_properties && GetArcProperties(&build_property)) {
    AddCrashMetaUploadData(arc_util::kArcVersionField,
                           build_property.fingerprint);
    AddCrashMetaUploadData(arc_util::kDeviceField, build_property.device);
    AddCrashMetaUploadData(arc_util::kBoardField, build_property.board);
    AddCrashMetaUploadData(arc_util::kCpuAbiField, build_property.cpu_abi);
    AddCrashMetaUploadData(
        arc_util::kAndroidVersionField,
        arc_util::GetVersionFromFingerprint(build_property.fingerprint)
            .value_or(kUnknownValue));
  }

  int64_t start_time;
  brillo::ErrorPtr error;
  SetUpDBus();
  if (session_manager_proxy_->GetArcStartTimeTicks(&start_time, &error)) {
    const uint64_t delta = static_cast<uint64_t>(
        (TimeTicks::Now() - TimeTicks::FromInternalValue(start_time))
            .InSeconds());
    AddCrashMetaUploadData(arc_util::kUptimeField, FormatDuration(delta));
  } else {
    LOG(ERROR) << "Failed to get ARC uptime: " << error->GetMessage();
  }

  if (arc_util::IsSilentReport(crash_type))
    AddCrashMetaData(arc_util::kSilentKey, "true");
}

bool ArcCollector::CreateReportForJavaCrash(const std::string& crash_type,
                                            const BuildProperty& build_property,
                                            const CrashLogHeaderMap& map,
                                            const std::string& exception_info,
                                            const std::string& log,
                                            bool* out_of_capacity) {
  FilePath crash_dir;
  if (!GetCreatedCrashDirectoryByEuid(geteuid(), &crash_dir, out_of_capacity)) {
    LOG(ERROR) << "Failed to create or find crash directory";
    return false;
  }

  const auto process = arc_util::GetCrashLogHeader(map, arc_util::kProcessKey);
  pid_t dt = arc_util::CreateRandomPID();
  const auto basename = FormatDumpBasename(process, std::time(nullptr), dt);
  const FilePath log_path = GetCrashPath(crash_dir, basename, "log");

  const int size = static_cast<int>(log.size());
  if (WriteNewFile(log_path, log.c_str(), size) != size) {
    PLOG(ERROR) << "Failed to write log";
    return false;
  }

  AddArcMetaData(process, crash_type, false);
  AddCrashMetaUploadData(arc_util::kArcVersionField,
                         build_property.fingerprint);
  AddCrashMetaUploadData(
      arc_util::kAndroidVersionField,
      arc_util::GetVersionFromFingerprint(build_property.fingerprint)
          .value_or(kUnknownValue));
  AddCrashMetaUploadData(arc_util::kDeviceField, build_property.device);
  AddCrashMetaUploadData(arc_util::kBoardField, build_property.board);
  AddCrashMetaUploadData(arc_util::kCpuAbiField, build_property.cpu_abi);

  for (const auto& mapping : arc_util::kHeaderToFieldMapping) {
    if (map.count(mapping.first)) {
      AddCrashMetaUploadData(mapping.second,
                             arc_util::GetCrashLogHeader(map, mapping.first));
    }
  }

  if (exception_info.empty()) {
    if (const char* const tag = arc_util::GetSubjectTag(crash_type)) {
      std::ostringstream out;
      out << '[' << tag << ']';
      const auto it = map.find(arc_util::kSubjectKey);
      if (it != map.end())
        out << ' ' << it->second;

      AddCrashMetaData(arc_util::kSignatureField, out.str());
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

    AddCrashMetaUploadText(arc_util::kExceptionInfoField,
                           info_path.BaseName().value());
  }

  const FilePath meta_path = GetCrashPath(crash_dir, basename, "meta");
  FinishCrash(meta_path, process, log_path.BaseName().value());
  return true;
}

UserCollectorBase::ErrorType ArcCollector::Is64BitProcess(
    int pid, bool* is_64_bit) const {
  std::string auxv_contents;
  if (!context_->ReadAuxvForProcess(pid, &auxv_contents)) {
    PLOG(ERROR) << "Could not read /proc/" << pid << "/auxv";
    return kErrorSystemIssue;
  }
  // auxv is an array of unsigned long[2], and the first element in each entry
  // is an AT_* key. We assume we are running a 32-bit process (hence the
  // |*is_64_bit| below), and then try to see if any of the keys seem off.
  // All AT_* keys are less than ~48, so if we find any key that exceeds 256, we
  // definitely know it is not a 32-bit process. This will almost always trigger
  // correctly because some of the values in the auxv are pointers and their
  // high bits are almost always non-zero. For illustration purposes, consider
  // the following auxv taken from a x86_64 machine:
  //
  // |-------64-bit key------|-----64-bit value------|
  // |32-bit key-|32-bit val-|32-bit key-|32-bit val-|
  //  21 00 00 00 00 00 00 00 00 30 db e6 fe 7f 00 00
  //  10 00 00 00 00 00 00 00 ff fb eb bf 00 00 00 00
  //  06 00 00 00 00 00 00 00 00 10 00 00 00 00 00 00
  //  ...
  //
  //  When interpreted as 64-bit unsigned longs, all the keys are less than 256,
  //  but when interpreted as 32-bit unsigned longs, some of the "keys" will
  //  contain the upper parts of addresses.
  struct Auxv32BitEntry {
    uint32_t key;
    uint32_t value;
  };
  if (auxv_contents.size() % sizeof(Auxv32BitEntry) != 0) {
    LOG(ERROR) << "Could not parse the contents of the auxv file. "
               << "Size not a multiple of 8: " << auxv_contents.size();
    return kErrorSystemIssue;
  }
  *is_64_bit = false;

  const Auxv32BitEntry* auxv_32_bit_entries =
      reinterpret_cast<const Auxv32BitEntry*>(auxv_contents.data());
  const size_t auxv_32_bit_entries_length =
      auxv_contents.size() / sizeof(Auxv32BitEntry);

  for (size_t i = 0; i < auxv_32_bit_entries_length; ++i) {
    if (auxv_32_bit_entries[i].key > 256) {
      *is_64_bit = true;
      break;
    }
  }

  return kErrorNone;
}

namespace {

bool ReadCrashLogFromStdin(std::stringstream* stream) {
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

bool GetChromeVersion(std::string* version) {
  ProcessImpl chrome;
  chrome.AddArg(kChromePath);
  chrome.AddArg("--product-version");

  int exit_code = util::RunAndCaptureOutput(&chrome, STDOUT_FILENO, version);
  if (exit_code != EX_OK || version->empty()) {
    LOG(ERROR) << "Failed to get Chrome version";
    return false;
  }

  version->pop_back();  // Discard EOL.
  return true;
}

bool GetArcRoot(FilePath* root) {
  base::FileEnumerator containers(
      kContainersDir, false, base::FileEnumerator::DIRECTORIES, kArcDirPattern);

  for (FilePath container = containers.Next(); !container.empty();
       container = containers.Next()) {
    const FilePath path = container.Append("root");
    if (base::PathExists(path)) {
      *root = path;
      return true;
    }
  }

  return false;
}

bool GetArcProperties(ArcCollector::BuildProperty* build_property) {
  FilePath root;
  brillo::KeyValueStore store;
  if (GetArcRoot(&root) && store.Load(root.Append(kArcBuildProp)) &&
      store.GetString(kFingerprintProperty, &(build_property->fingerprint)) &&
      store.GetString(kDeviceProperty, &(build_property->device)) &&
      store.GetString(kBoardProperty, &(build_property->board)) &&
      store.GetString(kCpuAbiProperty, &(build_property->cpu_abi)))
    return true;

  LOG(ERROR) << "Failed to get ARC properties";
  return false;
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
