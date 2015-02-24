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
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <chromeos/syslog_logging.h>

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
using std::wstring;

// Watches a Chrome binary and restarts it when it crashes. Also watches
// window manager binary as well. Actually supports watching several
// processes specified as command line arguments separated with --.
// Also listens over DBus for the commands specified in dbus_glib_shim.h.

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

// Name of flag specifying the time (in s) to wait for children to exit
// gracefully before killing them with a SIGABRT.
static const char kKillTimeout[] = "kill-timeout";
static const int kKillTimeoutDefault = 3;

// Name of the flag specifying whether we should kill and restart chrome
// if we detect that it has hung.
static const char kEnableHangDetection[] = "enable-hang-detection";
static const uint32_t kHangDetectionIntervalDefaultSeconds = 60;

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
    "    chrome binary and exit. (default: /var/run/disable_chrome_restart)\n"
    "  --kill-timeout=[number in seconds]\n"
    "    Number of seconds to wait for children to exit gracefully before\n"
    "    killing them with a SIGABRT.\n"
    "  --enable-hang-detection[=number in seconds]\n"
    "    Ping the browser over DBus periodically to determine if it's alive.\n"
    "    Optionally accepts a period value in seconds.  Default is 60.\n"
    "    If it fails to respond, SIGABRT and restart it."
    "  -- /path/to/program [arg1 [arg2 [ . . . ] ] ]\n"
    "    Supplies the required program to execute and its arguments.\n";

}  // namespace switches

using login_manager::BrowserJob;
using login_manager::BrowserJobInterface;
using login_manager::FileChecker;
using login_manager::LoginMetrics;
using login_manager::PerformChromeSetup;
using login_manager::SessionManagerService;
using login_manager::SystemUtilsImpl;

// Directory in which per-boot metrics flag files will be stored.
const char kFlagFileDir[] = "/var/run/session_manager";

int main(int argc, char* argv[]) {
  base::AtExitManager exit_manager;
  CommandLine::Init(argc, argv);
  CommandLine* cl = CommandLine::ForCurrentProcess();
  chromeos::InitLog(chromeos::kLogToSyslog | chromeos::kLogHeader);

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
  vector<string> command;
  base::SplitStringAlongWhitespace(command_flag, &command);

  // Parse kill timeout if it's present.
  int kill_timeout = switches::kKillTimeoutDefault;
  if (cl->HasSwitch(switches::kKillTimeout)) {
    string timeout_flag = cl->GetSwitchValueASCII(switches::kKillTimeout);
    int from_flag = 0;
    if (base::StringToInt(timeout_flag, &from_flag)) {
      kill_timeout = from_flag;
    } else {
      DLOG(WARNING) << "Failed to parse kill timeout, defaulting to "
                    << kill_timeout;
    }
  }

  // Parse hang detection interval if it's present.
  uint32_t hang_detection_interval =
      switches::kHangDetectionIntervalDefaultSeconds;
  if (cl->HasSwitch(switches::kEnableHangDetection)) {
    string flag = cl->GetSwitchValueASCII(switches::kEnableHangDetection);
    uint32_t from_flag = 0;
    if (base::StringToUint(flag, &from_flag)) {
      hang_detection_interval = from_flag;
    } else {
      DLOG(WARNING) << "Failed to parse hang detection interval, defaulting to "
                    << hang_detection_interval;
    }
  }

  // Start the X server and set things up for running Chrome.
  map<string, string> env_vars;
  vector<string> args;
  uid_t uid = 0;
  PerformChromeSetup(&env_vars, &args, &uid);
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

  // This job encapsulates the command specified on the command line, and the
  // UID that the caller would like to run it as.
  scoped_ptr<BrowserJobInterface> browser_job(
      new BrowserJob(command, env_vars, uid, &checker, &metrics, &system));
  bool should_run_browser = browser_job->ShouldRunBrowser();

  base::MessageLoopForIO message_loop;
  base::RunLoop run_loop;

  scoped_refptr<SessionManagerService> manager = new SessionManagerService(
      browser_job.Pass(),
      run_loop.QuitClosure(),
      uid,
      kill_timeout,
      cl->HasSwitch(switches::kEnableHangDetection),
      base::TimeDelta::FromSeconds(hang_detection_interval),
      &metrics,
      &system);

  if (manager->Initialize()) {
    // Allows devs to start/stop browser manually.
    if (should_run_browser) {
      message_loop.PostTask(
          FROM_HERE, base::Bind(&SessionManagerService::RunBrowser, manager));
    }
    run_loop.Run();  // Returns when run_loop's QuitClosure is posted and run.
  }
  manager->Finalize();

  LOG_IF(WARNING, manager->exit_code() != SessionManagerService::SUCCESS)
      << "session_manager exiting with code " << manager->exit_code();
  return manager->exit_code();
}
