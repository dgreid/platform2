// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/probe_tool.h"

#include <fcntl.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/values.h>
#include <brillo/errors/error_codes.h>
#include <build/build_config.h>
#include <build/buildflag.h>
#include <chromeos/dbus/service_constants.h>
#include <vboot/crossystem.h>

#include "debugd/src/error_utils.h"
#include "debugd/src/sandboxed_process.h"

using base::ListValue;

namespace debugd {

namespace {
constexpr char kErrorPath[] = "org.chromium.debugd.RunProbeFunctionError";
constexpr char kSandboxInfoDir[] = "/etc/runtime_probe/sandbox";
constexpr char kBinary[] = "/usr/bin/runtime_probe_helper";
constexpr char kRunAs[] = "runtime_probe";

bool CreateNonblockingPipe(base::ScopedFD* read_fd, base::ScopedFD* write_fd) {
  int pipe_fd[2];
  int ret = pipe2(pipe_fd, O_CLOEXEC | O_NONBLOCK);
  if (ret != 0) {
    PLOG(ERROR) << "Cannot create a pipe.";
    return false;
  }
  read_fd->reset(pipe_fd[0]);
  write_fd->reset(pipe_fd[1]);
  return true;
}

}  // namespace

bool ProbeTool::EvaluateProbeFunction(
    brillo::ErrorPtr* error,
    const std::string& sandbox_info,
    const std::string& probe_statement,
    brillo::dbus_utils::FileDescriptor* outfd) {
  std::unique_ptr<brillo::Process> process;

  // Details of sandboxing for probing should be centralized in a single
  // directory. Sandboxing is mandatory when we don't allow debug features.
  if (VbGetSystemPropertyInt("cros_debug") != 1) {
    std::unique_ptr<SandboxedProcess> sandboxed_process{new SandboxedProcess()};

    const auto seccomp_path = base::FilePath{kSandboxInfoDir}.Append(
        base::StringPrintf("%s-seccomp.policy", sandbox_info.c_str()));
    const auto minijail_args_path = base::FilePath{kSandboxInfoDir}.Append(
        base::StringPrintf("%s.args", sandbox_info.c_str()));

    if (!base::PathExists(seccomp_path) ||
        !base::PathExists(minijail_args_path)) {
      DEBUGD_ADD_ERROR(error, kErrorPath,
                       "Sandbox info is missing for this architecture");
      return false;
    }

    // Read and parse the arguments, use JSON to avoid quote escaping.
    std::string minijail_args_str;
    if (!base::ReadFileToString(base::FilePath(minijail_args_path),
                                &minijail_args_str)) {
      DEBUGD_ADD_ERROR(error, kErrorPath, "Failed to load minijail arguments");
      return false;
    }

    DLOG(INFO) << "minijail arguments : " << minijail_args_str;

    // Parse the JSON formatted minijail arguments.
    auto minijail_args = base::JSONReader::Read(minijail_args_str);

    if (!minijail_args) {
      DEBUGD_ADD_ERROR(error, kErrorPath,
                       "minijail args are not stored in list");
      return false;
    }
    std::vector<std::string> parsed_args;

    // The following is the general minijail set up for runtime_probe in debugd
    // /dev/log needs to be bind mounted before any possible tmpfs mount on run
    parsed_args.push_back("-G");  // Inherit all the supplementary groups

    // Run the process inside the new VFS namespace. The root of VFS is mounted
    // on /var/empty/
    parsed_args.push_back("-P");
    parsed_args.push_back("/mnt/empty");

    parsed_args.push_back("-b");        // Bind mount rootfs
    parsed_args.push_back("/");         // Bind mount rootfs
    parsed_args.push_back("-b");        // Bind mount /proc
    parsed_args.push_back("/proc");     // Bind mount /proc
    parsed_args.push_back("-b");        // Enable logging in minijail
    parsed_args.push_back("/dev/log");  // Enable logging in minijail
    parsed_args.push_back("-t");        // Bind mount /tmp
    parsed_args.push_back("-r");        // Remount /proc read-only
    parsed_args.push_back("-d");        // Mount a new /dev with minimum nodes

    for (const auto& arg : minijail_args->GetList()) {
      if (!arg.is_string()) {
        DEBUGD_ADD_ERROR(error, kErrorPath,
                         "Failed to parse minijail arguments");
        return false;
      }
      parsed_args.push_back(arg.GetString());
    }

    sandboxed_process->SandboxAs(kRunAs, kRunAs);
    sandboxed_process->SetSeccompFilterPolicyFile(seccomp_path.MaybeAsASCII());
    DLOG(INFO) << "Sandbox for " << sandbox_info << " is ready";
    if (!sandboxed_process->Init(parsed_args)) {
      DEBUGD_ADD_ERROR(error, kErrorPath, "Process initialization failure.");
      return false;
    }
    process = std::move(sandboxed_process);
  } else {
    process = std::make_unique<brillo::ProcessImpl>();
    // Explicitly running it without sandboxing.
    LOG(ERROR) << "Running " << sandbox_info << " without sandbox";
  }
  base::ScopedFD read_fd, write_fd;
  if (!CreateNonblockingPipe(&read_fd, &write_fd)) {
    DEBUGD_ADD_ERROR(error, kErrorPath, "Cannot create a pipe.");
    return false;
  }

  process->AddArg(kBinary);
  process->AddArg(probe_statement);
  process->BindFd(write_fd.get(), STDOUT_FILENO);
  process->Start();
  process->Release();
  *outfd = std::move(read_fd);
  return true;
}

}  // namespace debugd
