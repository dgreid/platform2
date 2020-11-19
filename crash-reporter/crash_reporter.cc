// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>      // for open
#include <sys/mount.h>  // for MS_SLAVE

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/strings/stringprintf.h>
#include <base/task/single_thread_task_executor.h>
#include <base/optional.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <libminijail.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/arc_collector.h"
#include "crash-reporter/arc_util.h"
#include "crash-reporter/arcvm_native_collector.h"
#include "crash-reporter/bert_collector.h"
#include "crash-reporter/chrome_collector.h"
#include "crash-reporter/constants.h"
#include "crash-reporter/crash_reporter_failure_collector.h"
#include "crash-reporter/ec_collector.h"
#include "crash-reporter/ephemeral_crash_collector.h"
#include "crash-reporter/generic_failure_collector.h"
#include "crash-reporter/kernel_collector.h"
#include "crash-reporter/kernel_warning_collector.h"
#include "crash-reporter/missed_crash_collector.h"
#include "crash-reporter/mount_failure_collector.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/selinux_violation_collector.h"
#include "crash-reporter/udev_collector.h"
#include "crash-reporter/unclean_shutdown_collector.h"
#include "crash-reporter/user_collector.h"
#include "crash-reporter/util.h"
#include "crash-reporter/vm_collector.h"

using base::FilePath;
using base::StringPrintf;

namespace {

const char kKernelCrashDetected[] =
    "/run/metrics/external/crash-reporter/kernel-crash-detected";
const char kUncleanShutdownDetected[] =
    "/run/metrics/external/crash-reporter/unclean-shutdown-detected";
const char kBootCollectorDone[] = "/run/crash_reporter/boot-collector-done";

// Used for consent verification.
// TODO(mutexlox): Declare this in main and plumb it through via parameters.
MetricsLibrary s_metrics_lib;

bool TouchFile(const FilePath& file_path) {
  return base::WriteFile(file_path, "", 0) == 0;
}

bool SetUpLockFile() {
  base::FilePath lock_file = paths::Get(paths::kCrashSenderLockFile);
  if (!TouchFile(lock_file)) {
    LOG(ERROR) << "Could not touch lock file: " << lock_file.value();
    return false;
  }

  // Allow crash-access group to read and write crash lock file.
  return util::SetGroupAndPermissions(lock_file, constants::kCrashGroupName,
                                      /*execute=*/false);
}

// Set up necessary crash reporter state.
// This function will change ownership and permissions on many files (to allow
// `crash` to read/write them) so it MUST run as root.
// Return true on success.
bool InitializeSystem(UserCollector* user_collector, bool early) {
  // Try to create the lock file for crash_sender. Creating this early ensures
  // that no one else can make a directory or such with this name. If the lock
  // file isn't a normal file, crash_sender will never work correctly.
  if (!SetUpLockFile()) {
    LOG(ERROR) << "Couldn't set up lock file";
    return false;
  }

  // Set up all the common crash state directories first.  If we can't guarantee
  // these basic paths, just give up & don't turn on anything else.
  if (!CrashCollector::InitializeSystemCrashDirectories(early)) {
    return false;
  }

  // Set up metrics flag directory. Returns with non-zero if we cannot create
  // it.
  if (!CrashCollector::InitializeSystemMetricsDirectories()) {
    return false;
  }

  return user_collector->Enable(early);
}

int BootCollect(bool always_allow_feedback,
                KernelCollector* kernel_collector,
                ECCollector* ec_collector,
                BERTCollector* bert_collector,
                UncleanShutdownCollector* unclean_shutdown_collector,
                EphemeralCrashCollector* ephemeral_crash_collector) {
  bool was_kernel_crash = false;
  bool was_unclean_shutdown = false;
  LOG(INFO) << "Running boot collector";

  if (always_allow_feedback || util::IsFeedbackAllowed(&s_metrics_lib)) {
    /* TODO(drinkcat): Distinguish between EC crash and unclean shutdown. */
    ec_collector->Collect();

    // Invoke to collect firmware bert dump.
    bert_collector->Collect();

    kernel_collector->Enable();
    if (kernel_collector->is_enabled()) {
      was_kernel_crash = kernel_collector->Collect();
    }
    was_unclean_shutdown = unclean_shutdown_collector->Collect();

    // Touch a file to notify the metrics daemon that a kernel
    // crash has been detected so that it can log the time since
    // the last kernel crash.
    if (was_kernel_crash) {
      TouchFile(FilePath(kKernelCrashDetected));
    } else if (was_unclean_shutdown) {
      // We only count an unclean shutdown if it did not come with
      // an associated kernel crash.
      TouchFile(FilePath(kUncleanShutdownDetected));
    }
    ephemeral_crash_collector->Collect();
  } else if (ephemeral_crash_collector->SkipConsent()) {
    ephemeral_crash_collector->Collect();
  }

  // The below calls happen independently of metrics consent, as they do not
  // generate any crash reports.

  // Must enable the unclean shutdown collector *after* collecting.
  unclean_shutdown_collector->Enable();

  // Copy lsb-release and os-release into system crash spool.  Done after
  // collecting so that boot-time collected crashes will be associated with the
  // previous boot.
  unclean_shutdown_collector->SaveVersionData();

  // Presence of this files unblocks powerd from performing lid-closed action
  // (crbug.com/988831).
  TouchFile(FilePath(kBootCollectorDone));

  return 0;
}

// Ensure stdout, stdin, and stderr are open file descriptors.  If
// they are not, any code which writes to stderr/stdout may write out
// to files opened during execution.  In particular, when
// crash_reporter is run by the kernel coredump pipe handler (via
// kthread_create/kernel_execve), it will not have file table entries
// 1 and 2 (stdout and stderr) populated.  We populate them here.
void OpenStandardFileDescriptors() {
  int new_fd = -1;
  // We open /dev/null to fill in any of the standard [0, 2] file
  // descriptors.  We leave these open for the duration of the
  // process.  This works because open returns the lowest numbered
  // invalid fd.
  do {
    new_fd = open("/dev/null", 0);
    CHECK_GE(new_fd, 0) << "Unable to open /dev/null";
  } while (new_fd >= 0 && new_fd <= 2);
  close(new_fd);
}

// Reduce privs that we don't need.  But we still need:
// - The top most /proc to pull details out of it.
// - Read access to the crashing process's memory (regardless of user).
// - Write access to the crash spool dir.
void EnterSandbox(bool write_proc, bool log_to_stderr) {
  // If we're not root, we won't be able to jail ourselves (well, we could if
  // we used user namespaces, but maybe later).  Need to double check handling
  // when called by chrome to process its crashes.
  if (getuid() != 0)
    return;

  struct minijail* j = minijail_new();
  minijail_namespace_ipc(j);
  minijail_namespace_uts(j);
  minijail_namespace_net(j);
  minijail_namespace_vfs(j);
  // Remount mounts as MS_SLAVE to prevent crash_reporter from holding on to
  // mounts that might be unmounted in the root mount namespace.
  minijail_remount_mode(j, MS_SLAVE);
  minijail_mount_tmp(j);
  minijail_mount_dev(j);
  if (!log_to_stderr)
    minijail_bind(j, "/dev/log", "/dev/log", 0);
  minijail_no_new_privs(j);
  minijail_new_session_keyring(j);

  // If we're initializing the system, we need to write to /proc/sys/.
  if (!write_proc) {
    minijail_remount_proc_readonly(j);
  }

  minijail_enter(j);
  minijail_destroy(j);
}

}  // namespace

// Information to invoke a specific call on a collector.
struct InvocationInfo {
  // True iff this callback should be invoked.
  // Once this is true and we invoke the associated callback, main() returns,
  // so only one handler can run for each execution of crash_reporter.
  bool should_handle;
  // Callback to invoke if |should_handle| is true. (can be null).
  base::RepeatingCallback<bool()> cb;
};

// Information required to initialize and invoke a collector
struct CollectorInfo {
  // An un-owned pointer to the collector
  CrashCollector* collector;
  // Initialization function. If none is specified, invoke the default
  // crash_collector Initialize().
  base::RepeatingClosure init;
  // List of handlers with associated conditions.
  // If a particular condition is true, run init and the associated handler (if
  // any). If there is no associated handler, keep going.
  std::vector<InvocationInfo> handlers;
};

int main(int argc, char* argv[]) {
  DEFINE_bool(init, false, "Initialize crash logging");
  DEFINE_bool(boot_collect, false, "Run per-boot crash collection tasks");
  DEFINE_bool(clean_shutdown, false, "Signal clean shutdown");
  DEFINE_bool(mount_failure, false, "Report mount failure");
  DEFINE_bool(umount_failure, false, "Report umount failure");
  DEFINE_string(mount_device, "",
                "Device that failed to mount. Used with --mount_failure and "
                "--umount_failure");
  DEFINE_bool(ephemeral_collect, false,
              "Move crash reports to more persistent storage if available "
              "(tmpfs -> reboot vault) or (tmpfs/reboot vault -> encstateful)");
  DEFINE_bool(crash_test, false, "Crash test");
  DEFINE_bool(early, false,
              "Modifies crash-reporter to work during early boot");
  DEFINE_bool(preserve_across_clobber, false,
              "Persist early user crash reports across clobbers.");
  DEFINE_string(user, "", "User crash info (pid:signal:exec_name)");
  DEFINE_string(udev, "", "Udev event description (type:device:subsystem)");
  DEFINE_bool(kernel_warning, false, "Report collected kernel warning");
  DEFINE_bool(kernel_iwlwifi_error, false,
              "Report collected kernel iwlwifi error");
  DEFINE_bool(kernel_wifi_warning, false,
              "Report collected kernel wifi warning");
  DEFINE_bool(kernel_smmu_fault, false, "Report collected kernel smmu faults");
  DEFINE_bool(kernel_suspend_warning, false,
              "Report collected kernel suspend warning");
  DEFINE_bool(missed_chrome_crash, false,
              "Report that we missed a Chrome crash");
  DEFINE_int32(recent_miss_count, -1,
               "For missed_chrome_crash, how many Chrome crashes have we "
               "missed over the last 1 minute");
  DEFINE_int32(recent_match_count, -1,
               "For missed_chrome_crash, how many Chrome crashes have we "
               "matched over the last 1 minute");
  DEFINE_int32(pending_miss_count, -1,
               "For missed_chrome_crash, how many Chrome crashes are we "
               "tracking that might be counted as a miss soon");
  DEFINE_bool(log_to_stderr, false, "Log to stderr instead of syslog.");
  DEFINE_string(arc_service_failure, "",
                "The specific ARC service name that failed");
  DEFINE_bool(suspend_failure, false, "Report collected suspend failure logs.");
  DEFINE_bool(vm_crash, false, "Report collected from VM crash");
  DEFINE_int32(vm_pid, -1, "PID of the main VM process");
  DEFINE_bool(crash_reporter_crashed, false,
              "Report crash_reporter itself crashing");
  DEFINE_string(service_failure, "", "The specific service name that failed");
  DEFINE_bool(selinux_violation, false, "Report collected SELinux violation");
  // TODO(crbug.com/1000398): Remove --chrome flag after Chrome switches from
  // breakpad to crashpad.
  // Note: --chrome is being replaced by --chrome_memfd;
  //       --chrome_dump_dir is only used for tests and only used when
  // --chrome_memfd is used and not when --chrome is used.
  DEFINE_string(chrome, "", "Chrome crash dump file");
  DEFINE_int32(chrome_memfd, -1, "Chrome crash dump memfd");
  DEFINE_string(chrome_dump_dir, "",
                "Directory to write Chrome minidumps, used for tests only");
  DEFINE_int32(pid, -1, "PID of crashing process");
  DEFINE_int32(uid, -1, "UID of crashing process");
  DEFINE_string(exe, "", "Executable name of crashing process");
  DEFINE_string(error_key, "",
                "Key for error reports. Replaces exe for some errors that "
                "aren't tied to an executable. Unlike exe, this is not "
                "uploaded as part of the crash report.");
  DEFINE_int64(crash_loop_before, -1,
               "UNIX timestamp. If invoked before this time, use the special "
               "login-crash-loop handling system. (Keep crash report in memory "
               "and then pass to debugd for immediate upload.)");
  DEFINE_bool(core2md_failure, false, "Core2md failure test");
  DEFINE_bool(directory_failure, false, "Spool directory failure test");
  DEFINE_bool(
      always_allow_feedback, false,
      "Force if feedback is allowed check to return true, used for tests only");
#if USE_CHEETS
  DEFINE_string(arc_java_crash, "",
                "Read Java crash log of the given type from standard input");
  DEFINE_string(arc_device, "", "Metadata for ARC crashes");
  DEFINE_string(arc_board, "", "Metadata for ARC crashes");
  DEFINE_string(arc_cpu_abi, "", "Metadata for ARC crashes");
  DEFINE_string(arc_fingerprint, "", "Metadata for ARC crashes");
  DEFINE_bool(arc_is_arcvm, false, "Is ARCVM");
  DEFINE_bool(arc_native, false, "ARC Native Crash");
  DEFINE_int64(arc_native_time, -1,
               "UNIX timestamp of the time when the native crash happened. "
               "Metadata for ARCVM native crashes");
#endif

  OpenStandardFileDescriptors();
  FilePath my_path = base::MakeAbsoluteFilePath(FilePath(argv[0]));
  brillo::FlagHelper::Init(argc, argv, "Chromium OS Crash Reporter");

  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(task_executor.task_runner());

  // In certain cases, /dev/log may not be available: log to stderr instead.
  if (FLAGS_log_to_stderr) {
    brillo::InitLog(brillo::kLogToStderr);
  } else {
    brillo::OpenLog(my_path.BaseName().value().c_str(), true);
    brillo::InitLog(brillo::kLogToSyslog);
  }

  if (util::SkipCrashCollection(argc, argv)) {
    return 0;
  }

  bool always_allow_feedback = false;
  if (FLAGS_always_allow_feedback) {
    CHECK(util::IsTestImage()) << "--always_allow_feedback is only for tests";
    LOG(INFO) << "--always_allow_feedback set; skipping consent check";
    always_allow_feedback = true;
  }

  // Make it possible to test what happens when we crash while handling a crash.
  if (FLAGS_crash_test) {
    LOG(ERROR) << "crash_test requested";
    *(volatile char*)0 = 0;
    return 0;
  }

  // Now that we've processed the command line, sandbox ourselves.
  EnterSandbox(FLAGS_init || FLAGS_clean_shutdown, FLAGS_log_to_stderr);

  // Decide if we should use Crash-Loop sending mode. If session_manager sees
  // several Chrome crashes in a brief period, it will log the user out. On the
  // last Chrome startup before it logs the user out, it will set the
  // --crash_loop_before flag. The value of the flag will be a time_t timestamp
  // giving the last second at which a crash would be considered a crash loop
  // and thus log the user out. If we have another crash before that second,
  // we have detected a crash-loop and we want to invoke special handling
  // (specifically, we don't want to save the crash in the user's home directory
  // because that will be inaccessible to crash_sender once the user is logged
  // out).
  CrashCollector::CrashSendingMode crash_sending_mode =
      CrashCollector::kNormalCrashSendMode;
  if (FLAGS_crash_loop_before >= 0) {
    base::Time crash_loop_before =
        base::Time::FromTimeT(static_cast<time_t>(FLAGS_crash_loop_before));
    if (base::Time::Now() <= crash_loop_before) {
      crash_sending_mode = CrashCollector::kCrashLoopSendingMode;
      LOG(INFO) << "Using crash loop sending mode";
    }
  }

  UserCollectorBase::CrashAttributes user_crash_attrs;
  if (!FLAGS_user.empty()) {
    base::Optional<UserCollectorBase::CrashAttributes> attrs =
        UserCollectorBase::ParseCrashAttributes(FLAGS_user);
    if (!attrs.has_value()) {
      LOG(ERROR) << "Invalid parameter: --user=" << FLAGS_user;
      return 1;
    }
    user_crash_attrs = *attrs;
  }

  std::vector<CollectorInfo> collectors;
#if USE_CHEETS
  ArcvmNativeCollector arcvm_native_collector;
  collectors.push_back({
      .collector = &arcvm_native_collector,
      .handlers = {{
          // This handles native crashes of ARCVM.
          .should_handle = FLAGS_arc_is_arcvm && FLAGS_arc_native,
          .cb = base::BindRepeating(
              &ArcvmNativeCollector::HandleCrash,
              base::Unretained(&arcvm_native_collector),
              arc_util::BuildProperty{.device = FLAGS_arc_device,
                                      .board = FLAGS_arc_board,
                                      .cpu_abi = FLAGS_arc_cpu_abi,
                                      .fingerprint = FLAGS_arc_fingerprint},
              ArcvmNativeCollector::CrashInfo{
                  .time = static_cast<time_t>(FLAGS_arc_native_time),
                  .pid = FLAGS_pid,
                  .exec_name = FLAGS_exe}),
      }},
  });

  ArcCollector arc_collector;

  // Always initialize arc_collector so that we can use it to determine if the
  // process is an arc process.
  arc_collector.Initialize(FLAGS_directory_failure, false /* early */);
  bool is_arc_process = !FLAGS_user.empty() && ArcCollector::IsArcRunning() &&
                        arc_collector.IsArcProcess(user_crash_attrs.pid);

  collectors.push_back({
      .collector = &arc_collector,
      .init = base::DoNothing(),
      .handlers =
          {{
               // This handles native crashes of ARC++.
               .should_handle = !FLAGS_arc_is_arcvm && is_arc_process,
               .cb = base::BindRepeating(&ArcCollector::HandleCrash,
                                         base::Unretained(&arc_collector),
                                         user_crash_attrs, nullptr),
           },
           {
               // This handles Java app crashes of ARC++ and ARCVM.
               .should_handle = !FLAGS_arc_java_crash.empty(),
               .cb = base::BindRepeating(
                   &ArcCollector::HandleJavaCrash,
                   base::Unretained(&arc_collector), FLAGS_arc_java_crash,
                   arc_util::BuildProperty{
                       .device = FLAGS_arc_device,
                       .board = FLAGS_arc_board,
                       .cpu_abi = FLAGS_arc_cpu_abi,
                       .fingerprint = FLAGS_arc_fingerprint,
                   }),
           }},
  });
#else   // USE_CHEETS
  bool is_arc_process = false;
#endif  // USE_CHEETS

  UserCollector user_collector;
  collectors.push_back({
      .collector = &user_collector,
      .init = base::BindRepeating(&UserCollector::Initialize,
                                  base::Unretained(&user_collector),
                                  my_path.value(), FLAGS_core2md_failure,
                                  FLAGS_directory_failure, FLAGS_early),
      .handlers = {{
                       // NOTE: This is not handling a crash; it's instead
                       // initializing the entire crash reporting system.
                       // So, leave |cb| unset and call InitializeSystem
                       // manually below.
                       .should_handle = FLAGS_init,
                   },
                   {
                       .should_handle = FLAGS_clean_shutdown,
                       // Leave cb unset: clean_shutdown requires other
                       // collectors, so it's handled later.
                   },
                   {
                       .should_handle = !FLAGS_user.empty() && !is_arc_process,
                       .cb = base::BindRepeating(
                           &UserCollector::HandleCrash,
                           base::Unretained(&user_collector), user_crash_attrs,
                           nullptr),
                   }},
  });

  EphemeralCrashCollector ephemeral_crash_collector;
  collectors.push_back({
      .collector = &ephemeral_crash_collector,
      .init = base::BindRepeating(&EphemeralCrashCollector::Initialize,
                                  base::Unretained(&ephemeral_crash_collector),
                                  FLAGS_preserve_across_clobber),
      .handlers =
          {{
               .should_handle = FLAGS_ephemeral_collect,
               .cb = base::BindRepeating(
                   &EphemeralCrashCollector::Collect,
                   base::Unretained(&ephemeral_crash_collector)),
           },
           {
               .should_handle = FLAGS_boot_collect,
               // leave cb empty because boot_collect needs multiple collectors.
               // It's handled separately at the end of main.
           }},
  });

  MountFailureCollector mount_failure_collector(
      MountFailureCollector::ValidateStorageDeviceType(FLAGS_mount_device));
  collectors.push_back({
      .collector = &mount_failure_collector,
      .handlers = {{
          .should_handle = FLAGS_mount_failure || FLAGS_umount_failure,
          .cb = base::BindRepeating(&MountFailureCollector::Collect,
                                    base::Unretained(&mount_failure_collector),
                                    FLAGS_mount_failure),
      }},
  });

  MissedCrashCollector missed_crash_collector;
  collectors.push_back({
      .collector = &missed_crash_collector,
      .handlers = {{
          .should_handle = FLAGS_missed_chrome_crash,
          .cb = base::BindRepeating(&MissedCrashCollector::Collect,
                                    base::Unretained(&missed_crash_collector),
                                    FLAGS_pid, FLAGS_recent_miss_count,
                                    FLAGS_recent_match_count,
                                    FLAGS_pending_miss_count),
      }},
  });

  UncleanShutdownCollector unclean_shutdown_collector;
  collectors.push_back({
      .collector = &unclean_shutdown_collector,
      .handlers = {{
          .should_handle = FLAGS_boot_collect || FLAGS_clean_shutdown,
          // leave cb empty because both of these need multiple collectors and
          // are handled separately at the end of main.
      }},
  });

  std::vector<InvocationInfo> boot_handlers = {{
      .should_handle = FLAGS_boot_collect,
      // leave cb empty because boot_collect needs multiple collectors and is
      // handled separately at the end of main. should_handle is only true so
      // the collector gets initialized.
  }};

  KernelCollector kernel_collector;
  collectors.push_back({
      .collector = &kernel_collector,
      .handlers = boot_handlers,
  });

  ECCollector ec_collector;
  collectors.push_back({
      .collector = &ec_collector,
      .handlers = boot_handlers,
  });

  BERTCollector bert_collector;
  collectors.push_back({
      .collector = &bert_collector,
      .handlers = boot_handlers,
  });

  UdevCollector udev_collector;
  collectors.push_back({.collector = &udev_collector,
                        .handlers = {{
                            .should_handle = !FLAGS_udev.empty(),
                            .cb = base::BindRepeating(
                                &UdevCollector::HandleCrash,
                                base::Unretained(&udev_collector), FLAGS_udev),
                        }}});

  CHECK(FLAGS_chrome.empty() || FLAGS_chrome_memfd == -1)
      << "--chrome= and --chrome_memfd= cannot be both set";
  if (FLAGS_chrome_memfd == -1) {
    CHECK(FLAGS_error_key.empty())
        << "--error_key is only for --chrome_memfd crashes";
  }

  ChromeCollector chrome_collector(crash_sending_mode);
  collectors.push_back({
      .collector = &chrome_collector,
      .handlers = {{
                       .should_handle = !FLAGS_chrome.empty(),
                       .cb = base::BindRepeating(
                           &ChromeCollector::HandleCrash,
                           base::Unretained(&chrome_collector),
                           FilePath(FLAGS_chrome), FLAGS_pid, FLAGS_uid,
                           FLAGS_exe),
                   },
                   {
                       .should_handle = FLAGS_chrome_memfd >= 0,
                       .cb = base::BindRepeating(
                           &ChromeCollector::HandleCrashThroughMemfd,
                           base::Unretained(&chrome_collector),
                           FLAGS_chrome_memfd, FLAGS_pid, FLAGS_uid, FLAGS_exe,
                           FLAGS_error_key, FLAGS_chrome_dump_dir),
                   }},
  });

  KernelWarningCollector kernel_warning_collector;
  base::RepeatingCallback<bool(KernelWarningCollector::WarningType)>
      kernel_warn_cb =
          base::BindRepeating(&KernelWarningCollector::Collect,
                              base::Unretained(&kernel_warning_collector));
  collectors.push_back({
      .collector = &kernel_warning_collector,
      .handlers = {{
                       .should_handle = FLAGS_kernel_warning,
                       .cb = base::BindRepeating(
                           kernel_warn_cb,
                           KernelWarningCollector::WarningType::kGeneric),
                   },
                   {
                       .should_handle = FLAGS_kernel_wifi_warning,
                       .cb = base::BindRepeating(
                           kernel_warn_cb,
                           KernelWarningCollector::WarningType::kWifi),
                   },
                   {
                       .should_handle = FLAGS_kernel_smmu_fault,
                       .cb = base::BindRepeating(
                           kernel_warn_cb,
                           KernelWarningCollector::WarningType::kSMMUFault),
                   },
                   {
                       .should_handle = FLAGS_kernel_suspend_warning,
                       .cb = base::BindRepeating(
                           kernel_warn_cb,
                           KernelWarningCollector::WarningType::kSuspend),
                   },
                   {
                       .should_handle = FLAGS_kernel_iwlwifi_error,
                       .cb = base::BindRepeating(
                           kernel_warn_cb,
                           KernelWarningCollector::WarningType::kIwlwifi),
                   }},
  });

  GenericFailureCollector generic_failure_collector;
  collectors.push_back(
      {.collector = &generic_failure_collector,
       .handlers = {
           {
               .should_handle = FLAGS_suspend_failure,
               .cb = base::BindRepeating(
                   &GenericFailureCollector::Collect,
                   base::Unretained(&generic_failure_collector),
                   GenericFailureCollector::kSuspendFailure),
           },
           {
               .should_handle = !FLAGS_arc_service_failure.empty(),
               .cb = base::BindRepeating(
                   &GenericFailureCollector::CollectFull,
                   base::Unretained(&generic_failure_collector),
                   StringPrintf("%s-%s",
                                GenericFailureCollector::kArcServiceFailure,
                                FLAGS_arc_service_failure.c_str()),
                   GenericFailureCollector::kArcServiceFailure,
                   util::GetServiceFailureWeight()),
           },
           {
               .should_handle = !FLAGS_service_failure.empty(),
               .cb = base::BindRepeating(
                   &GenericFailureCollector::CollectFull,
                   base::Unretained(&generic_failure_collector),
                   StringPrintf("%s-%s",
                                GenericFailureCollector::kServiceFailure,
                                FLAGS_service_failure.c_str()),
                   GenericFailureCollector::kServiceFailure,
                   util::GetServiceFailureWeight()),
           }}});

  SELinuxViolationCollector selinux_violation_collector;
  collectors.push_back({.collector = &selinux_violation_collector,
                        .handlers = {{
                            .should_handle = FLAGS_selinux_violation,
                            .cb = base::BindRepeating(
                                &SELinuxViolationCollector::Collect,
                                base::Unretained(&selinux_violation_collector)),
                        }}});

  CrashReporterFailureCollector crash_reporter_failure_collector;
  collectors.push_back(
      {.collector = &crash_reporter_failure_collector,
       .handlers = {{
           .should_handle = FLAGS_crash_reporter_crashed,
           .cb = base::BindRepeating(
               &CrashReporterFailureCollector::Collect,
               base::Unretained(&crash_reporter_failure_collector)),
       }}});

  VmCollector vm_collector;
  collectors.push_back({.collector = &vm_collector,
                        .handlers = {{
                            .should_handle = FLAGS_vm_crash,
                            .cb = base::BindRepeating(
                                &VmCollector::Collect,
                                base::Unretained(&vm_collector), FLAGS_vm_pid),
                        }}});

  for (const CollectorInfo& collector : collectors) {
    bool ran_init = false;
    for (const InvocationInfo& info : collector.handlers) {
      if (info.should_handle) {
        if (!ran_init) {
          if (collector.init) {
            collector.init.Run();
          } else {
            collector.collector->Initialize(FLAGS_early);
          }
          ran_init = true;
        }
        if (info.cb) {
          // Accumulate logs to a string to help in diagnosing failures during
          // collection.
          brillo::LogToString(true);

          // Default to successful exit status if there's no consent.
          bool handled = true;
          // For early boot crash collectors, the consent file will not be
          // accessible.  Instead, check consent during boot collection.
          if (FLAGS_early || always_allow_feedback ||
              util::IsFeedbackAllowed(&s_metrics_lib)) {
            handled = info.cb.Run();
          } else if (collector.collector == &ephemeral_crash_collector &&
                     ephemeral_crash_collector.SkipConsent()) {
            // Due to the specific circumstances in which the ephemeral
            // collector runs, it might need to skip consent checks (e.g. if
            // it's running just after a disk clobber, the clobber may have
            // wiped out a user's preferences). Other collectors should not skip
            // consent checks.
            handled = info.cb.Run();
          }
          brillo::LogToString(false);
          return handled ? 0 : 1;
        }
      }
    }
  }

  if (FLAGS_init) {
    // Called manually to skip the normal consent checks; we always initialize
    // the system regardless of consent.
    return InitializeSystem(&user_collector, FLAGS_early) ? 0 : 1;
  }

  // These special cases (which use multiple collectors) are at the end so that
  // it's clear that all relevant collectors have been initialized.
  if (FLAGS_boot_collect) {
    return BootCollect(always_allow_feedback, &kernel_collector, &ec_collector,
                       &bert_collector, &unclean_shutdown_collector,
                       &ephemeral_crash_collector);
  }

  if (FLAGS_clean_shutdown) {
    int ret = 0;
    if (!unclean_shutdown_collector.Disable())
      ret = 1;
    if (!user_collector.Disable())
      ret = 1;
    return ret;
  }
}
