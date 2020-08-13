// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/user_collector.h"

#include <bits/wordsize.h>
#include <elf.h>
#include <fcntl.h>
#include <stdint.h>

#include <unordered_set>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>

#include "crash-reporter/user_collector_base.h"
#include "crash-reporter/util.h"
#include "crash-reporter/vm_support.h"

using base::FilePath;
using base::StringPrintf;

namespace {

// This procfs file is used to cause kernel core file writing to
// instead pipe the core file into a user space process.  See
// core(5) man page.
const char kCorePatternFile[] = "/proc/sys/kernel/core_pattern";
const char kCorePipeLimitFile[] = "/proc/sys/kernel/core_pipe_limit";
// Set core_pipe_limit to 4 so that we can catch a few unrelated concurrent
// crashes, but finite to avoid infinitely recursing on crash handling.
const char kCorePipeLimit[] = "4";
const char kCoreToMinidumpConverterPath[] = "/usr/bin/core2md";

const char kFilterPath[] = "/opt/google/crash-reporter/filter";

// Core pattern lock file: only exists on linux-3.18 and earlier.
const char kCorePatternLockFile[] = "/proc/sys/kernel/lock_core_pattern";

// Filename we touch in our state directory when we get enabled.
constexpr char kCrashHandlingEnabledFlagFile[] = "crash-handling-enabled";

// Returns true if the given executable name matches that of Chrome.  This
// includes checks for threads that Chrome has renamed.
bool IsChromeExecName(const std::string& exec);

// This is needed for kernels older than linux-4.4. Once we drop support for
// older kernels (upgrading or going EOL), we can drop this logic.
bool LockCorePattern() {
  base::FilePath core_pattern_lock_file(kCorePatternLockFile);

  // Core pattern lock was only added for kernel versions before 4.4.
  if (!base::PathExists(core_pattern_lock_file)) {
    VLOG(1) << "No core pattern lock available";
    return true;
  }

  if (util::IsDeveloperImage()) {
    LOG(INFO) << "Developer image -- leaving core pattern unlocked";
    return true;
  }

  if (base::WriteFile(core_pattern_lock_file, "1", 1) != 1) {
    PLOG(ERROR) << "Failed to lock core pattern";
    return false;
  }

  return true;
}

}  // namespace

UserCollector::UserCollector()
    : UserCollectorBase("user", kUseNormalCrashDirectorySelectionMethod),
      core_pattern_file_(kCorePatternFile),
      core_pipe_limit_file_(kCorePipeLimitFile),
      filter_path_(kFilterPath),
      core2md_failure_(false) {}

void UserCollector::Initialize(
    const std::string& our_path,
    UserCollector::IsFeedbackAllowedFunction is_feedback_allowed_function,
    bool core2md_failure,
    bool directory_failure,
    bool early) {
  UserCollectorBase::Initialize(is_feedback_allowed_function, directory_failure,
                                early);
  our_path_ = our_path;
  core2md_failure_ = core2md_failure;
}

UserCollector::~UserCollector() {}

void UserCollector::FinishCrash(const base::FilePath& meta_path,
                                const std::string& exec_name,
                                const std::string& payload_name) {
  VmSupport* vm_support = VmSupport::Get();
  if (vm_support)
    vm_support->AddMetadata(this);

  UserCollectorBase::FinishCrash(meta_path, exec_name, payload_name);

  if (vm_support)
    vm_support->FinishCrash(meta_path);
}

// Return the string that should be used for the kernel's core_pattern file.
// Note that if you change the format of the enabled pattern, you'll probably
// also need to change the UserCollectorBase::ParseCrashAttributes function, the
// user_collector_test.cc unittest, the logging_UserCrash.py autotest,
// and the platform.UserCrash tast test.
std::string UserCollector::GetPattern(bool enabled, bool early) const {
  if (enabled) {
    // Combine the crash attributes into one parameter to try to reduce
    // the size of the invocation line for crash_reporter, since the kernel
    // has a fixed-sized (128B) buffer for it (before parameter expansion).
    // Note that the kernel does not support quoted arguments in core_pattern.
    return StringPrintf("|%s %s--user=%%P:%%s:%%u:%%g:%%f", our_path_.c_str(),
                        early ? "--early --log_to_stderr " : "");
  } else {
    return "core";
  }
}

bool UserCollector::SetUpInternal(bool enabled, bool early) {
  CHECK(initialized_);
  LOG(INFO) << (enabled ? "Enabling" : "Disabling") << " user crash handling";

  if (base::WriteFile(FilePath(core_pipe_limit_file_), kCorePipeLimit,
                      strlen(kCorePipeLimit)) !=
      static_cast<int>(strlen(kCorePipeLimit))) {
    PLOG(ERROR) << "Unable to write " << core_pipe_limit_file_;
    return false;
  }
  std::string pattern = GetPattern(enabled, early);
  if (base::WriteFile(FilePath(core_pattern_file_), pattern.c_str(),
                      pattern.length()) != static_cast<int>(pattern.length())) {
    int saved_errno = errno;
    // If the core pattern is locked and we try to reset the |core_pattern|
    // while disabling |user_collector| or resetting it to what it already was,
    // expect failure here with an EPERM.
    bool ignore_error = false;
    if (errno == EPERM && base::PathExists(FilePath(kCorePatternLockFile))) {
      std::string actual_contents;
      if (!base::ReadFileToString(FilePath(core_pattern_file_),
                                  &actual_contents)) {
        PLOG(ERROR) << "Failed to read " << core_pattern_file_;
        actual_contents.clear();
      }
      if (!enabled || base::TrimWhitespaceASCII(
                          actual_contents, base::TRIM_TRAILING) == pattern) {
        ignore_error = true;
        LOG(WARNING) << "Failed to write to locked core pattern; ignoring";
      }
    }
    if (!ignore_error) {
      LOG(ERROR) << "Unable to write " << core_pattern_file_ << ": "
                 << strerror(saved_errno);
      return false;
    }
  }

  // Attempt to lock down |core_pattern|: this only works for kernels older than
  // linux-3.18.
  if (enabled && !early && !LockCorePattern()) {
    LOG(ERROR) << "Failed to lock core pattern on a supported device";
    return false;
  }

  // Set up the base crash processing dir for future users.
  const FilePath dir = GetCrashProcessingDir();

  // First nuke all existing content.  This will take care of deleting any
  // existing paths (files, symlinks, dirs, etc...) for us.
  if (!base::DeleteFile(dir, true))
    PLOG(WARNING) << "Cleanup of directory failed: " << dir.value();

  // This will create the directory with 0700 mode.  Since init is run as root,
  // root will own these too.
  if (!base::CreateDirectory(dir)) {
    PLOG(ERROR) << "Creating directory failed: " << dir.value();
    return false;
  }

  // Write out a flag file for testing to indicate we have started correctly.
  char data[] = "enabled";
  size_t write_len = sizeof(data) - 1;
  if (base::WriteFile(base::FilePath(crash_reporter_state_path_)
                          .Append(kCrashHandlingEnabledFlagFile),
                      data, write_len) != write_len) {
    PLOG(WARNING) << "Unable to create flag file for crash reporter enabled";
  }

  return true;
}

bool UserCollector::CopyOffProcFiles(pid_t pid, const FilePath& container_dir) {
  FilePath process_path = GetProcessPath(pid);
  if (!base::PathExists(process_path)) {
    LOG(ERROR) << "Path " << process_path.value() << " does not exist";
    return false;
  }
  static const char* const kProcFiles[] = {"auxv", "cmdline", "environ", "maps",
                                           "status"};
  for (std::string proc_file : kProcFiles) {
    if (!base::CopyFile(process_path.Append(proc_file),
                        container_dir.Append(proc_file))) {
      PLOG(ERROR) << "Could not copy " << proc_file << " file";
      return false;
    }
  }
  return true;
}

bool UserCollector::ValidateProcFiles(const FilePath& container_dir) const {
  // Check if the maps file is empty, which could be due to the crashed
  // process being reaped by the kernel before finishing a core dump.
  int64_t file_size = 0;
  if (!base::GetFileSize(container_dir.Append("maps"), &file_size)) {
    PLOG(ERROR) << "Could not get the size of maps file";
    return false;
  }
  if (file_size == 0) {
    LOG(ERROR) << "maps file is empty";
    return false;
  }
  return true;
}

UserCollector::ErrorType UserCollector::ValidateCoreFile(
    const FilePath& core_path) const {
  int fd = HANDLE_EINTR(open(core_path.value().c_str(), O_RDONLY));
  if (fd < 0) {
    PLOG(ERROR) << "Could not open core file " << core_path.value();
    return kErrorReadCoreData;
  }

  char e_ident[EI_NIDENT];
  bool read_ok = base::ReadFromFD(fd, e_ident, sizeof(e_ident));
  IGNORE_EINTR(close(fd));
  if (!read_ok) {
    LOG(ERROR) << "Could not read header of core file";
    return kErrorInvalidCoreFile;
  }

  if (e_ident[EI_MAG0] != ELFMAG0 || e_ident[EI_MAG1] != ELFMAG1 ||
      e_ident[EI_MAG2] != ELFMAG2 || e_ident[EI_MAG3] != ELFMAG3) {
    LOG(ERROR) << "Invalid core file";
    return kErrorInvalidCoreFile;
  }

#if __WORDSIZE == 64
  // TODO(benchan, mkrebs): Remove this check once core2md can
  // handles both 32-bit and 64-bit ELF on a 64-bit platform.
  if (e_ident[EI_CLASS] == ELFCLASS32) {
    LOG(ERROR) << "Conversion of 32-bit core file on 64-bit platform is "
               << "currently not supported";
    return kErrorUnsupported32BitCoreFile;
  }
#endif

  return kErrorNone;
}

bool UserCollector::CopyStdinToCoreFile(const FilePath& core_path) {
  // Copy off all stdin to a core file.
  FilePath stdin_path("/dev/fd/0");
  if (base::CopyFile(stdin_path, core_path)) {
    return true;
  }

  PLOG(ERROR) << "Could not write core file";
  // If the file system was full, make sure we remove any remnants.
  base::DeleteFile(core_path, false);
  return false;
}

bool UserCollector::RunCoreToMinidump(const FilePath& core_path,
                                      const FilePath& procfs_directory,
                                      const FilePath& minidump_path,
                                      const FilePath& temp_directory) {
  FilePath output_path = temp_directory.Append("output");
  brillo::ProcessImpl core2md;
  core2md.RedirectOutput(output_path.value());
  core2md.AddArg(kCoreToMinidumpConverterPath);
  core2md.AddArg(core_path.value());
  core2md.AddArg(procfs_directory.value());

  if (!core2md_failure_) {
    core2md.AddArg(minidump_path.value());
  } else {
    // To test how core2md errors are propagaged, cause an error
    // by forgetting a required argument.
  }

  int errorlevel = core2md.Run();

  std::string output;
  base::ReadFileToString(output_path, &output);
  if (errorlevel != 0) {
    LOG(ERROR) << "Problem during " << kCoreToMinidumpConverterPath
               << " [result=" << errorlevel << "]: " << output;
    return false;
  }

  if (!base::PathExists(minidump_path)) {
    LOG(ERROR) << "Minidump file " << minidump_path.value()
               << " was not created";
    return false;
  }

  if (chmod(minidump_path.value().c_str(), kSystemCrashFilesMode) < 0) {
    PLOG(ERROR) << "Couldn't chmod minidump file: " << minidump_path.value();
    return false;
  }
  return true;
}

bool UserCollector::RunFilter(pid_t pid) {
  int mode;
  int exec_mode = base::FILE_PERMISSION_EXECUTE_BY_USER |
                  base::FILE_PERMISSION_EXECUTE_BY_GROUP |
                  base::FILE_PERMISSION_EXECUTE_BY_OTHERS;
  if (!base::GetPosixFilePermissions(base::FilePath(filter_path_), &mode) ||
      (mode & exec_mode) != exec_mode) {
    // Filter does not exist or is not executable.
    return true;
  }

  brillo::ProcessImpl filter;
  filter.AddArg(filter_path_);
  filter.AddArg(StringPrintf("%d", pid));

  return filter.Run() == 0;
}

bool UserCollector::ShouldDump(pid_t pid,
                               bool has_owner_consent,
                               bool is_developer,
                               bool handle_chrome_crashes,
                               const std::string& exec,
                               std::string* reason) {
  reason->clear();

  // Treat Chrome crashes as if the user opted-out.  We stop counting Chrome
  // crashes towards user crashes, so user crashes really mean non-Chrome
  // user-space crashes.
  if (!handle_chrome_crashes && IsChromeExecName(exec)) {
    // anomaly_detector's CrashReporterParser looks for this message; don't
    // change it without updating the regex.
    *reason =
        "ignoring call by kernel - chrome crash; "
        "waiting for chrome to call us directly";
    return false;
  }

  if (!RunFilter(pid)) {
    *reason = "filtered out";
    return false;
  }

  return UserCollectorBase::ShouldDump(pid, has_owner_consent, is_developer,
                                       reason);
}

bool UserCollector::ShouldDump(pid_t pid,
                               uid_t,
                               const std::string& exec,
                               std::string* reason) {
  return ShouldDump(pid, is_feedback_allowed_function_(),
                    util::IsDeveloperImage(), ShouldHandleChromeCrashes(), exec,
                    reason);
}

UserCollector::ErrorType UserCollector::ConvertCoreToMinidump(
    pid_t pid,
    const FilePath& container_dir,
    const FilePath& core_path,
    const FilePath& minidump_path) {
  // If proc files are unusable, we continue to read the core file from stdin,
  // but only skip the core-to-minidump conversion, so that we may still use
  // the core file for debugging.
  bool proc_files_usable =
      CopyOffProcFiles(pid, container_dir) && ValidateProcFiles(container_dir);

  if (!CopyStdinToCoreFile(core_path)) {
    return kErrorReadCoreData;
  }

  if (!proc_files_usable) {
    LOG(INFO) << "Skipped converting core file to minidump due to "
              << "unusable proc files";
    return kErrorUnusableProcFiles;
  }

  ErrorType error = ValidateCoreFile(core_path);
  if (error != kErrorNone) {
    return error;
  }

  if (!RunCoreToMinidump(core_path,
                         container_dir,  // procfs directory
                         minidump_path,
                         container_dir)) {  // temporary directory
    return kErrorCore2MinidumpConversion;
  }

  return kErrorNone;
}

namespace {

bool IsChromeExecName(const std::string& exec) {
  static const char* const kChromeNames[] = {
      "chrome",
      // These are additional thread names seen in http://crash/
      "MediaPipeline",
      // These come from the use of base::PlatformThread::SetName() directly
      "CrBrowserMain", "CrRendererMain", "CrUtilityMain", "CrPPAPIMain",
      "CrPPAPIBrokerMain", "CrPluginMain", "CrWorkerMain", "CrGpuMain",
      "BrokerEvent", "CrVideoRenderer", "CrShutdownDetector", "UsbEventHandler",
      "CrNaClMain", "CrServiceMain",
      // These thread names come from the use of base::Thread
      "Gamepad polling thread", "Chrome_InProcGpuThread",
      "Chrome_DragDropThread", "Renderer::FILE", "VC manager",
      "VideoCaptureModuleImpl", "JavaBridge", "VideoCaptureManagerThread",
      "Geolocation", "Geolocation_wifi_provider",
      "Device orientation polling thread", "Chrome_InProcRendererThread",
      "NetworkChangeNotifier", "Watchdog", "inotify_reader",
      "cf_iexplore_background_thread", "BrowserWatchdog",
      "Chrome_HistoryThread", "Chrome_SyncThread", "Chrome_ShellDialogThread",
      "Printing_Worker", "Chrome_SafeBrowsingThread", "SimpleDBThread",
      "D-Bus thread", "AudioThread", "NullAudioThread", "V4L2Thread",
      "ChromotingClientDecodeThread", "Profiling_Flush", "worker_thread_ticker",
      "AudioMixerAlsa", "AudioMixerCras", "FakeAudioRecordingThread",
      "CaptureThread", "Chrome_WebSocketproxyThread", "ProcessWatcherThread",
      "Chrome_CameraThread", "import_thread", "NaCl_IOThread",
      "Chrome_CloudPrintJobPrintThread", "Chrome_CloudPrintProxyCoreThread",
      "DaemonControllerFileIO", "ChromotingMainThread",
      "ChromotingEncodeThread", "ChromotingDesktopThread", "ChromotingIOThread",
      "ChromotingFileIOThread", "Chrome_libJingle_WorkerThread",
      "Chrome_ChildIOThread", "GLHelperThread", "RemotingHostPlugin",
      // "PAC thread #%d",  // not easy to check because of "%d"
      "Chrome_DBThread", "Chrome_WebKitThread", "Chrome_FileThread",
      "Chrome_FileUserBlockingThread", "Chrome_ProcessLauncherThread",
      "Chrome_CacheThread", "Chrome_IOThread", "Cache Thread", "File Thread",
      "ServiceProcess_IO", "ServiceProcess_File", "extension_crash_uploader",
      "gpu-process_crash_uploader", "plugin_crash_uploader",
      "renderer_crash_uploader",
      // These come from the use of webkit_glue::WebThreadImpl
      "Compositor", "Browser Compositor",
      // "WorkerPool/%d",  // not easy to check because of "%d"
      // These come from the use of base::Watchdog
      "Startup watchdog thread Watchdog", "Shutdown watchdog thread Watchdog",
      // These come from the use of AudioDeviceThread::Start
      "AudioDevice", "AudioInputDevice", "AudioOutputDevice",
      // These come from the use of MessageLoopFactory::GetMessageLoop
      "GpuVideoDecoder", "RtcVideoDecoderThread", "PipelineThread",
      "AudioDecoderThread", "VideoDecoderThread",
      // These come from the use of MessageLoopFactory::GetMessageLoopProxy
      "CaptureVideoDecoderThread", "CaptureVideoDecoder",
      // These come from the use of base::SimpleThread
      "LocalInputMonitor/%d",  // "%d" gets lopped off for kernel-supplied
      // These come from the use of base::DelegateSimpleThread
      "ipc_channel_nacl reader thread/%d", "plugin_audio_input_thread/%d",
      "plugin_audio_thread/%d",
      // These come from the use of base::SequencedWorkerPool
      "BrowserBlockingWorker%d/%d",  // "%d" gets lopped off for kernel-supplied
  };
  static std::unordered_set<std::string> chrome_names;

  // Initialize a set of chrome names, for efficient lookup
  if (chrome_names.empty()) {
    for (std::string check_name : kChromeNames) {
      chrome_names.insert(check_name);
      // When checking a kernel-supplied name, it should be truncated to 15
      // chars.  See PR_SET_NAME in
      // http://www.kernel.org/doc/man-pages/online/pages/man2/prctl.2.html,
      // although that page misleads by saying "16 bytes".
      chrome_names.insert("supplied_" + std::string(check_name, 0, 15));
    }
  }

  return base::Contains(chrome_names, exec);
}

}  // namespace
