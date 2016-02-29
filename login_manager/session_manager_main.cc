// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/callback.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/syslog_logging.h>
#include <linux/limits.h>
#include <rootdev/rootdev.h>

#include "login_manager/browser_job.h"
#include "login_manager/chrome_setup.h"
#include "login_manager/file_checker.h"
#include "login_manager/login_metrics.h"
#include "login_manager/regen_mitigator.h"
#include "login_manager/session_manager_service.h"
#include "login_manager/system_utils_impl.h"

using std::map;
using std::string;
using std::vector;

// Watches a Chrome binary and restarts it when it crashes. Also watches
// window manager binary as well. Actually supports watching several
// processes specified as command line arguments separated with --.
// Also listens over DBus for the commands specified in
// session_manager_dbus_adaptor.h.

namespace switches {

// Name of the flag that contains the command for running Chrome.
static const char kChromeCommand[] = "chrome-command";
static const char kChromeCommandDefault[] = "/opt/google/chrome/chrome";

// Name of the flag that contains the path to the file which disables restart of
// managed jobs upon exit or crash if the file is present.
static const char kDisableChromeRestartFile[] = "disable-chrome-restart-file";
// The default path to this file.
static const char kDisableChromeRestartFileDefault[] =
    "/var/run/disable_chrome_restart";

// Flag that causes session manager to show the help message and exit.
static const char kHelp[] = "help";
// The help message shown if help flag is passed to the program.
static const char kHelpMessage[] =
    "\nAvailable Switches: \n"
    "  --chrome-command=</path/to/executable>\n"
    "    Path to the Chrome executable. Split along whitespace into arguments\n"
    "    (to which standard Chrome arguments will be appended); a value like\n"
    "    \"/usr/local/bin/strace /path/to/chrome\" may be used to wrap Chrome "
    "in\n"
    "    another program. (default: /opt/google/chrome/chrome)\n"
    "  --disable-chrome-restart-file=</path/to/file>\n"
    "    Magic file that causes this program to stop restarting the\n"
    "    chrome binary and exit. (default: /var/run/disable_chrome_restart)\n";
}  // namespace switches

using login_manager::BrowserJob;
using login_manager::BrowserJobInterface;
using login_manager::FileChecker;
using login_manager::LoginMetrics;
using login_manager::PerformChromeSetup;
using login_manager::SessionManagerService;
using login_manager::SystemUtilsImpl;

// Directory in which per-boot metrics flag files will be stored.
static const char kFlagFileDir[] = "/var/run/session_manager";

// Hang-detection magic file and constants.
static const char kHangDetectionFlagFile[] = "enable_hang_detection";
static const uint32_t kHangDetectionIntervalDefaultSeconds = 60;
static const uint32_t kHangDetectionIntervalShortSeconds = 5;

// Time to wait for children to exit gracefully before killing them
// with a SIGABRT.
static const int kKillTimeoutDefaultSeconds = 3;
static const int kKillTimeoutLongSeconds = 12;

namespace {
bool BootDeviceIsRotationalDisk() {
  char full_rootdev_path[PATH_MAX];
  if (rootdev(full_rootdev_path, PATH_MAX - 1, true, true) != 0) {
    PLOG(WARNING) << "Couldn't find root device. Guessing it's not rotational.";
    return false;
  }
  CHECK(base::StartsWith(full_rootdev_path, "/dev/",
                         base::CompareCase::SENSITIVE));
  string device_only(full_rootdev_path + 5, PATH_MAX - 5);
  base::FilePath sysfs_path(
      base::StringPrintf("/sys/block/%s/queue/rotational",
                         device_only.c_str()));
  string rotational_contents;
  if (!base::ReadFileToString(sysfs_path, &rotational_contents))
    PLOG(WARNING) << "Couldn't read from " << sysfs_path.value();
  return rotational_contents == "1";
}
}  // namespace

int main(int argc, char* argv[]) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogHeader);

  // Allow waiting for all descendants, not just immediate children
  if (::prctl(PR_SET_CHILD_SUBREAPER, 1))
    PLOG(ERROR) << "Couldn't set child subreaper";

  if (cl->HasSwitch(switches::kHelp)) {
    LOG(INFO) << switches::kHelpMessage;
    return 0;
  }

  // Parse the base Chrome command.
  string command_flag(switches::kChromeCommandDefault);
  if (cl->HasSwitch(switches::kChromeCommand))
    command_flag = cl->GetSwitchValueASCII(switches::kChromeCommand);
  vector<string> command =
      base::SplitString(command_flag, base::kWhitespaceASCII,
                        base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // Start the X server and set things up for running Chrome.
  bool is_developer_end_user = false;
  map<string, string> env_vars;
  vector<string> args;
  uid_t uid = 0;
  PerformChromeSetup(&is_developer_end_user, &env_vars, &args, &uid);
  command.insert(command.end(), args.begin(), args.end());

  // Shim that wraps system calls, file system ops, etc.
  SystemUtilsImpl system;

  // Checks magic file that causes the session_manager to stop managing the
  // browser process. Devs and tests can use this to keep the session_manager
  // running while stopping and starting the browser manaually.
  string magic_chrome_file =
      cl->GetSwitchValueASCII(switches::kDisableChromeRestartFile);
  if (magic_chrome_file.empty())
    magic_chrome_file.assign(switches::kDisableChromeRestartFileDefault);
  FileChecker checker((base::FilePath(magic_chrome_file)));  // So vexing!

  // Used to report various metrics around user type (guest vs non), dev-mode,
  // and policy/key file status.
  base::FilePath flag_file_dir(kFlagFileDir);
  if (!base::CreateDirectory(flag_file_dir))
    PLOG(FATAL) << "Cannot create flag file directory at " << kFlagFileDir;
  LoginMetrics metrics(flag_file_dir);

  // The session_manager supports pinging the browser periodically to check that
  // it is still alive.  On developer systems, this would be a problem, as
  // debugging the browser would cause it to be aborted. Override via a
  // flag-file is allowed to enable integration testing.
  bool enable_hang_detection = !is_developer_end_user;
  uint32_t hang_detection_interval = kHangDetectionIntervalDefaultSeconds;
  if (base::PathExists(flag_file_dir.Append(kHangDetectionFlagFile)))
    hang_detection_interval = kHangDetectionIntervalShortSeconds;

  // On platforms with rotational disks, Chrome takes longer to shut down. As
  // such, we need to change our baseline assumption about what "taking too long
  // to shutdown" means and wait for longer before killing Chrome and triggering
  // a report.
  int kill_timeout = kKillTimeoutDefaultSeconds;
  if (BootDeviceIsRotationalDisk())
    kill_timeout = kKillTimeoutLongSeconds;
  LOG(INFO) << "Will wait " << kill_timeout << "s for graceful browser exit.";

  // This job encapsulates the command specified on the command line, and the
  // UID that the caller would like to run it as.
  scoped_ptr<BrowserJobInterface> browser_job(
      new BrowserJob(command, env_vars, uid, &checker, &metrics, &system));
  bool should_run_browser = browser_job->ShouldRunBrowser();

  base::MessageLoopForIO message_loop;
  brillo::BaseMessageLoop brillo_loop(&message_loop);
  brillo_loop.SetAsCurrent();

  scoped_refptr<SessionManagerService> manager = new SessionManagerService(
      std::move(browser_job),
      uid,
      kill_timeout,
      enable_hang_detection,
      base::TimeDelta::FromSeconds(hang_detection_interval),
      &metrics,
      &system);

  if (manager->Initialize()) {
    // Allows devs to start/stop browser manually.
    if (should_run_browser) {
      brillo_loop.PostTask(
          FROM_HERE, base::Bind(&SessionManagerService::RunBrowser, manager));
    }
    // Returns when brillo_loop.BreakLoop() is called.
    brillo_loop.Run();
  }
  manager->Finalize();

  LOG_IF(WARNING, manager->exit_code() != SessionManagerService::SUCCESS)
      << "session_manager exiting with code " << manager->exit_code();
  return manager->exit_code();
}
