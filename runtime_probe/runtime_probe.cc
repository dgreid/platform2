// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/values.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "runtime_probe/daemon.h"
#include "runtime_probe/probe_config.h"
#include "runtime_probe/utils/config_utils.h"

namespace {
enum ExitStatus {
  kSuccess = EXIT_SUCCESS,  // 0
  kUnknownError = 1,
  kConfigFileSyntaxError = 11,
  kFailToParseProbeArgFromConfig = 12,
  kNoPermissionForArbitraryProbeConfig = 13,
};

// VLOG uses negative log level. This function provides safe coversion from the
// input |log_level| into the corresponding negative integer.
int SafeLogLevelConversion(const int log_level) {
  if (log_level < 0)
    return 0;
  if (log_level > 3)
    return -3;
  return -log_level;
}

}  // namespace

int main(int argc, char* argv[]) {
  // Flags are subject to change
  DEFINE_string(config_file_path, "",
                "File path to probe config, empty to use default one");
  DEFINE_bool(dbus, false, "Run in the mode to respond D-Bus call");
  DEFINE_bool(to_stdout, false, "Output probe result to stdout");
  DEFINE_int32(verbosity_level, 0, "Set verbosity level. Allowed value: 0 to 3")
      brillo::FlagHelper::Init(argc, argv, "ChromeOS runtime probe tool");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);

  const int log_level = SafeLogLevelConversion(FLAGS_verbosity_level);
  logging::SetMinLogLevel(log_level);
  LOG(INFO) << "Starting Runtime Probe";

  if (FLAGS_dbus) {
    LOG(INFO) << "Running in daemon mode";
    runtime_probe::Daemon daemon;
    return daemon.Run();
  }

  LOG(INFO) << "Running in CLI mode";

  // Required by dbus in libchrome.
  base::AtExitManager at_exit_manager;

  // Invoke as a command line tool. Device can load arbitrary probe config
  // iff cros_debug == 1
  std::string probe_config_path;
  if (!runtime_probe::GetProbeConfigPath(FLAGS_config_file_path,
                                         &probe_config_path))
    return ExitStatus::kNoPermissionForArbitraryProbeConfig;

  const auto probe_config_data =
      runtime_probe::ParseProbeConfig(probe_config_path);
  if (!probe_config_data)
    return ExitStatus::kConfigFileSyntaxError;

  auto probe_config =
      runtime_probe::ProbeConfig::FromValue(probe_config_data.value().config);

  if (!probe_config) {
    LOG(ERROR) << "Failed to parse from argument from ProbeConfig\n";
    return ExitStatus::kFailToParseProbeArgFromConfig;
  }
  const auto probe_result = probe_config->Eval();
  if (FLAGS_to_stdout) {
    LOG(INFO) << "Dumping probe results to stdout";
    std::cout << probe_result;
  } else {
    LOG(INFO) << probe_result;
  }

  return ExitStatus::kSuccess;
}
