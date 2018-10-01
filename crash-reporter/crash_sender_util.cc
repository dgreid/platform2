// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_sender_util.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <map>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <brillo/flag_helper.h>

#include "crash-reporter/crash_sender_paths.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/util.h"

namespace util {

namespace {

// getenv() wrapper that returns an empty string, if the environment variable is
// not defined.
std::string GetEnv(const std::string& name) {
  const char* value = getenv(name.c_str());
  return value ? value : "";
}

// Shows the usage of crash_sender and exits the process as a success.
void ShowUsageAndExit() {
  printf(
      "Usage: crash_sender [options]\n"
      "Options:\n"
      " -e <var>=<val>     Set env |var| to |val| (only some vars)\n");
  exit(EXIT_SUCCESS);
}

}  // namespace

void ParseCommandLine(int argc, const char* const* argv) {
  std::map<std::string, std::string> env_vars;
  for (const EnvPair& pair : kEnvironmentVariables) {
    // Honor the existing value if it's already set.
    const char* value = getenv(pair.name);
    env_vars[pair.name] = value ? value : pair.value;
  }

  // Process -e options, and collect other options.
  std::vector<const char*> new_argv;
  new_argv.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "-e") {
      if (i + 1 < argc) {
        ++i;
        std::string name_value = argv[i];
        std::vector<std::string> pair = base::SplitString(
            name_value, "=", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
        if (pair.size() == 2) {
          if (env_vars.count(pair[0]) == 0) {
            LOG(ERROR) << "Unknown variable name: " << pair[0];
            exit(EXIT_FAILURE);
          }
          env_vars[pair[0]] = pair[1];
        } else {
          LOG(ERROR) << "Malformed value for -e: " << name_value;
          exit(EXIT_FAILURE);
        }
      } else {
        LOG(ERROR) << "Value for -e is missing";
        exit(EXIT_FAILURE);
      }
    } else {
      new_argv.push_back(argv[i]);
    }
  }
  // argv[argc] should be a null pointer per the C standard.
  new_argv.push_back(nullptr);

  // Process the remaining flags.
  DEFINE_bool(h, false, "Show this help and exit");
  brillo::FlagHelper::Init(new_argv.size() - 1, new_argv.data(),
                           "Chromium OS Crash Sender");
  // TODO(satorux): Remove this once -e option is gone.
  if (FLAGS_h)
    ShowUsageAndExit();

  // Set the predefined environment variables.
  for (const auto& it : env_vars)
    setenv(it.first.c_str(), it.second.c_str(), 1 /* overwrite */);
}

bool IsMock() {
  return base::PathExists(
      paths::GetAt(paths::kSystemRunStateDirectory, paths::kMockCrashSending));
}

bool ShouldPauseSending() {
  return (base::PathExists(paths::Get(paths::kPauseCrashSending)) &&
          GetEnv("OVERRIDE_PAUSE_SENDING") == "0");
}

bool CheckDependencies(base::FilePath* missing_path) {
  const char* const kDependencies[] = {
      paths::kFind, paths::kMetricsClient,
      paths::kRestrictedCertificatesDirectory,
  };

  for (const char* dependency : kDependencies) {
    const base::FilePath path = paths::Get(dependency);
    int permissions = 0;
    // Check if |path| is an executable or a directory.
    if (!(base::GetPosixFilePermissions(path, &permissions) &&
          (permissions & base::FILE_PERMISSION_EXECUTE_BY_USER))) {
      *missing_path = path;
      return false;
    }
  }
  return true;
}

Sender::Sender(const Sender::Options& options)
    : shell_script_(options.shell_script), proxy_(options.proxy) {}

bool Sender::Init() {
  if (!scoped_temp_dir_.CreateUniqueTempDir()) {
    PLOG(ERROR) << "Failed to create a temporary directory.";
    return false;
  }
  return true;
}

bool Sender::SendCrashes(const base::FilePath& crash_dir) {
  if (!base::DirectoryExists(crash_dir)) {
    // Directory not existing is not an error.
    return true;
  }

  const int child_pid = fork();
  if (child_pid == 0) {
    char* shell_script_path = const_cast<char*>(shell_script_.value().c_str());
    char* temp_dir_path =
        const_cast<char*>(scoped_temp_dir_.GetPath().value().c_str());
    char* crash_dir_path = const_cast<char*>(crash_dir.value().c_str());
    char* shell_argv[] = {shell_script_path, temp_dir_path, crash_dir_path,
                          nullptr};
    execve(shell_script_path, shell_argv, environ);
    exit(EXIT_FAILURE);  // execve() failed.
  } else {
    int status = 0;
    if (waitpid(child_pid, &status, 0) < 0) {
      PLOG(ERROR) << "Failed to wait for the child process: " << child_pid;
      return false;
    }
    if (!WIFEXITED(status)) {
      LOG(ERROR) << "Terminated abnormally: " << status;
      return false;
    }
    int exit_code = WEXITSTATUS(status);
    if (exit_code != 0) {
      LOG(ERROR) << "Terminated with non-zero exit code: " << exit_code;
      return false;
    }
  }

  return true;
}

bool Sender::SendUserCrashes() {
  scoped_refptr<dbus::Bus> bus;
  bool fully_successful = true;

  // Set up the session manager proxy if it's not given from the options.
  if (!proxy_) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    scoped_refptr<dbus::Bus> bus = new dbus::Bus(options);
    CHECK(bus->Connect());
    proxy_.reset(new org::chromium::SessionManagerInterfaceProxy(bus));
  }

  std::vector<base::FilePath> directories;
  if (util::GetUserCrashDirectories(proxy_.get(), &directories)) {
    for (auto directory : directories) {
      if (!SendCrashes(directory)) {
        LOG(ERROR) << "Skipped " << directory.value();
        fully_successful = false;
      }
    }
  }

  if (bus)
    bus->ShutdownAndBlock();

  return fully_successful;
}

}  // namespace util
