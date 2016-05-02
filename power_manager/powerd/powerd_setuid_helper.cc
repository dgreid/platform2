// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a small setuid-root program that runs a few commands on behalf of
// the powerd process.

#include <fcntl.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <string>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/flag_helper.h>

// Maximum number of arguments supported for internally-defined commands.
const size_t kMaxArgs = 64;

// Value for the PATH environment variable. This is both used to search for
// binaries that are executed by this program and inherited by those binaries.
const char kPathEnvironment[] = "/usr/sbin:/usr/bin:/sbin:/bin";

// Path to device on which VT_UNLOCKSWITCH and VT_LOCKSWITCH ioctls can be made
// to enable or disable VT switching.
const char kConsolePath[] = "/dev/tty0";

// Runs a command with the supplied arguments.  The argument list must be
// NULL-terminated.  This method calls exec() without forking, so it will never
// return.
void RunCommand(const char* command, const char* arg, ...) {
  char* argv[kMaxArgs + 1];
  size_t num_args = 1;
  argv[0] = const_cast<char*>(command);

  va_list list;
  va_start(list, arg);
  while (arg) {
    num_args++;
    CHECK(num_args <= kMaxArgs) << "Too many arguments";
    argv[num_args - 1] = const_cast<char*>(arg);
    arg = va_arg(list, char*);
  }
  va_end(list);
  argv[num_args] = NULL;

  // initctl commands appear to fail if the real UID isn't set correctly.
  PCHECK(setuid(0) == 0) << "setuid() failed";
  PCHECK(clearenv() == 0) << "clearenv() failed";
  PCHECK(setenv("POWERD_SETUID_HELPER", "1", 1) == 0) << "setenv() failed";
  PCHECK(setenv("PATH", kPathEnvironment, 1) == 0) << "setenv() failed";
  PCHECK(execvp(command, argv) != -1) << "execv() failed";
}

// Locks or unlocks VT switching.  In a perfect world this would live in powerd,
// but these ioctls require the CAP_SYS_TTY_CONFIG capability and setting that
// breaks the setuid() call in RunCommand().  In a slightly less perfect world
// this would live in the powerd_suspend script, but there doesn't seem to be
// any way for a script to lock or unlock switching directly.
void SetVTSwitchingAllowed(bool allowed) {
  int fd = open(kConsolePath, O_WRONLY);
  PCHECK(fd >= 0) << "open(" << kConsolePath << ") failed";
  PCHECK(ioctl(fd, allowed ? VT_UNLOCKSWITCH : VT_LOCKSWITCH) == 0)
      << "ioctl() failed";
  close(fd);
}

int main(int argc, char* argv[]) {
  DEFINE_string(action, "", "Action to perform.  Must be one of \"lock_vt\", "
                "\"mosys_eventlog\", \"reboot\", \"set_wifi_transmit_power\", "
                "\"shut_down\", \"suspend\", " "and \"unlock_vt\".");
  DEFINE_string(mosys_eventlog_code, "", "Hexadecimal byte, e.g. \"0xa7\", "
                "describing the event being logged.");
  DEFINE_string(shutdown_reason, "", "Optional shutdown reason starting with a "
                "lowercase letter and consisting only of lowercase letters and "
                "dashes.");
  DEFINE_int64(suspend_duration, -1, "Pass --suspend_duration <INT> to "
               "powerd_suspend to resume after <INT> seconds for a dark "
               "resume.");
  DEFINE_uint64(suspend_wakeup_count, 0, "Pass --wakeup_count <INT> to "
                "powerd_suspend for the \"suspend\" action.");
  DEFINE_bool(suspend_wakeup_count_valid, false,
              "Should --suspend_wakeup_count be honored?");
  DEFINE_bool(suspend_to_idle, false,
              "Should the system suspend to idle (freeze)?");
  DEFINE_bool(wifi_transmit_power_tablet, false,
              "Set wifi transmit power mode to tablet mode");
  DEFINE_string(wifi_transmit_power_iwl_power_table, "",
                "Power table for iwlwifi driver");
  brillo::FlagHelper::Init(argc, argv, "powerd setuid helper");

  if (FLAGS_action == "lock_vt") {
    SetVTSwitchingAllowed(false);
  } else if (FLAGS_action == "mosys_eventlog") {
    CHECK(FLAGS_mosys_eventlog_code.size() == 4 &&
          FLAGS_mosys_eventlog_code[0] == '0' &&
          FLAGS_mosys_eventlog_code[1] == 'x' &&
          isxdigit(FLAGS_mosys_eventlog_code[2]) &&
          isxdigit(FLAGS_mosys_eventlog_code[3])) << "Invalid event code";
    RunCommand("mosys", "eventlog", "add", FLAGS_mosys_eventlog_code.c_str(),
               NULL);
  } else if (FLAGS_action == "reboot") {
    RunCommand("shutdown", "-r", "now", NULL);
  } else if (FLAGS_action == "set_wifi_transmit_power") {
    const char* tablet = FLAGS_wifi_transmit_power_tablet ?
        "--tablet" : "--notablet";
    std::string power_table;
    if (!FLAGS_wifi_transmit_power_iwl_power_table.empty()) {
      power_table = "--iwl_power_table=" +
          FLAGS_wifi_transmit_power_iwl_power_table;
    }
    RunCommand("set_wifi_transmit_power", tablet,
               (power_table.empty() ? NULL : power_table.c_str()), NULL);
  } else if (FLAGS_action == "shut_down") {
    std::string reason_arg;
    if (!FLAGS_shutdown_reason.empty()) {
      for (size_t i = 0; i < FLAGS_shutdown_reason.size(); ++i) {
        char ch = FLAGS_shutdown_reason[i];
        CHECK((ch >= 'a' && ch <= 'z') || (i > 0 && ch == '-'))
            << "Invalid shutdown reason";
      }
      reason_arg = "SHUTDOWN_REASON=" + FLAGS_shutdown_reason;
    }
    RunCommand("initctl", "emit", "--no-wait", "runlevel", "RUNLEVEL=0",
               (reason_arg.empty() ? NULL : reason_arg.c_str()), NULL);
  } else if (FLAGS_action == "suspend") {
    std::string duration_flag = "--suspend_duration=" +
        base::IntToString(FLAGS_suspend_duration);
    std::string idle_flag = FLAGS_suspend_to_idle ?
        std::string("--suspend_to_idle") : std::string("--nosuspend_to_idle");
    std::string wakeup_flag;
    if (FLAGS_suspend_wakeup_count_valid) {
      wakeup_flag = "--wakeup_count=" +
          base::Uint64ToString(FLAGS_suspend_wakeup_count);
    }
    RunCommand("powerd_suspend", duration_flag.c_str(), idle_flag.c_str(),
               wakeup_flag.empty() ? NULL : wakeup_flag.c_str(), NULL);
  } else if (FLAGS_action == "unlock_vt") {
    SetVTSwitchingAllowed(true);
  } else {
    LOG(ERROR) << "Unknown action \"" << FLAGS_action << "\"";
    return 1;
  }

  return 0;
}
