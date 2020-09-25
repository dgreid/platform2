// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a small setuid-root program that runs a few commands on behalf of
// the powerd process.

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
  argv[num_args] = nullptr;

  // initctl commands appear to fail if the real UID isn't set correctly.
  PCHECK(setuid(0) == 0) << "setuid() failed";
  PCHECK(clearenv() == 0) << "clearenv() failed";
  PCHECK(setenv("POWERD_SETUID_HELPER", "1", 1) == 0) << "setenv() failed";
  PCHECK(setenv("PATH", kPathEnvironment, 1) == 0) << "setenv() failed";
  PCHECK(execvp(command, argv) != -1) << "execv() failed";
}

int main(int argc, char* argv[]) {
  DEFINE_string(action, "",
                "Action to perform.  Must be one of \"mosys_eventlog\", "
                "\"reboot\", \"set_force_lid_open\", "
                "\"set_cellular_transmit_power\" \"set_wifi_transmit_power\", "
                "\"shut_down\", and \"suspend\".");
  DEFINE_bool(force_lid_open, false,
              "Whether lid should be forced open or not for "
              "\"set_force_lid_open\" action.");
  DEFINE_string(mosys_eventlog_code, "",
                "Hexadecimal byte, e.g. \"0xa7\", "
                "describing the event being logged.");
  DEFINE_string(shutdown_reason, "",
                "Optional shutdown or reboot reason starting with a "
                "lowercase letter and consisting only of lowercase letters and "
                "dashes.");
  DEFINE_uint64(suspend_wakeup_count, 0,
                "Pass --wakeup_count <INT> to "
                "powerd_suspend for the \"suspend\" action.");
  DEFINE_bool(suspend_wakeup_count_valid, false,
              "Should --suspend_wakeup_count be honored?");
  DEFINE_bool(suspend_to_idle, false,
              "Should the system suspend to idle (freeze)?");
  DEFINE_bool(wifi_transmit_power_tablet, false,
              "Set wifi transmit power mode to tablet mode");
  DEFINE_string(wifi_transmit_power_domain, "",
                "Regulatory domain for wifi transmit power");
  DEFINE_bool(cellular_transmit_power_low, false,
              "Set cellular transmit power mode to low");
  DEFINE_int64(cellular_transmit_power_gpio, -1,
               "GPIO pin to write to for changing cellular transmit power");
#if USE_TROGDOR_SAR_HACK
  DEFINE_int32(cellular_transmit_power_trogdor_level, 0, "QMI SAR Power level");
#endif  // USE_TROGDOR_SAR_HACK

  brillo::FlagHelper::Init(argc, argv, "powerd setuid helper");

  if (FLAGS_action == "mosys_eventlog") {
    CHECK(FLAGS_mosys_eventlog_code.size() == 4 &&
          FLAGS_mosys_eventlog_code[0] == '0' &&
          FLAGS_mosys_eventlog_code[1] == 'x' &&
          isxdigit(FLAGS_mosys_eventlog_code[2]) &&
          isxdigit(FLAGS_mosys_eventlog_code[3]))
        << "Invalid event code";
    RunCommand("mosys", "eventlog", "add", FLAGS_mosys_eventlog_code.c_str(),
               nullptr);
  } else if (FLAGS_action == "reboot" || FLAGS_action == "shut_down") {
    std::string reason_arg;
    if (!FLAGS_shutdown_reason.empty()) {
      for (size_t i = 0; i < FLAGS_shutdown_reason.size(); ++i) {
        char ch = FLAGS_shutdown_reason[i];
        CHECK((ch >= 'a' && ch <= 'z') || (i > 0 && ch == '-'))
            << "Invalid shutdown reason";
      }
      reason_arg = "SHUTDOWN_REASON=" + FLAGS_shutdown_reason;
    }
    std::string runlevel_arg =
        std::string("RUNLEVEL=") + (FLAGS_action == "reboot" ? "6" : "0");
    RunCommand("initctl", "emit", "--no-wait", "runlevel", runlevel_arg.c_str(),
               (reason_arg.empty() ? nullptr : reason_arg.c_str()), nullptr);
  } else if (FLAGS_action == "set_force_lid_open") {
    const char* state = FLAGS_force_lid_open ? "1" : "0";
    RunCommand("ectool", "forcelidopen", state, nullptr);
  } else if (FLAGS_action == "set_cellular_transmit_power") {
    std::string mode = FLAGS_cellular_transmit_power_low ? "--low" : "";
    std::string target =
        "--gpio=" + base::NumberToString(FLAGS_cellular_transmit_power_gpio);
    RunCommand("set_cellular_transmit_power", mode.c_str(), target.c_str(),
               nullptr);
#if USE_TROGDOR_SAR_HACK
  } else if (FLAGS_action == "set_cellular_transmit_power_trogdor") {
    std::string level =
        "--level=" +
        base::NumberToString(FLAGS_cellular_transmit_power_trogdor_level);
    RunCommand("set_cellular_transmit_power_trogdor", level.c_str(), nullptr);
#endif  // USE_TROGDOR_SAR_HACK
  } else if (FLAGS_action == "set_wifi_transmit_power") {
    const std::string tablet =
        FLAGS_wifi_transmit_power_tablet ? "--tablet" : "--notablet";
    const std::string domain = "--domain=" + FLAGS_wifi_transmit_power_domain;
    RunCommand("set_wifi_transmit_power", tablet.c_str(), domain.c_str(),
               nullptr);
  } else if (FLAGS_action == "suspend") {
    std::string idle_flag = FLAGS_suspend_to_idle
                                ? std::string("--suspend_to_idle")
                                : std::string("--nosuspend_to_idle");
    std::string wakeup_flag;
    if (FLAGS_suspend_wakeup_count_valid) {
      wakeup_flag =
          "--wakeup_count=" + base::NumberToString(FLAGS_suspend_wakeup_count);
    }
    RunCommand("powerd_suspend", idle_flag.c_str(),
               wakeup_flag.empty() ? nullptr : wakeup_flag.c_str(), nullptr);
  } else {
    LOG(ERROR) << "Unknown action \"" << FLAGS_action << "\"";
    return 1;
  }

  return 0;
}
