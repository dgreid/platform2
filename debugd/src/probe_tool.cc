// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/probe_tool.h"

#include <fcntl.h>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/optional.h>
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

namespace debugd {

namespace {
constexpr char kErrorPath[] = "org.chromium.debugd.RunProbeFunctionError";
constexpr char kSandboxInfoDir[] = "/etc/runtime_probe/sandbox";
constexpr char kSandboxArgs[] = "/etc/runtime_probe/sandbox/args.json";
constexpr std::array<const char*, 3> kBinaryAndArgs{"/usr/bin/runtime_probe",
                                                    "--helper", "--"};
constexpr char kRunAs[] = "runtime_probe";

bool CreateNonblockingPipe(base::ScopedFD* read_fd, base::ScopedFD* write_fd) {
  int pipe_fd[2];
  int ret = pipe2(pipe_fd, O_CLOEXEC | O_NONBLOCK);
  if (ret != 0) {
    PLOG(ERROR) << "Cannot create a pipe";
    return false;
  }
  read_fd->reset(pipe_fd[0]);
  write_fd->reset(pipe_fd[1]);
  return true;
}

std::string GetStringFromValue(const base::Value& value) {
  std::string json;
  base::JSONWriter::WriteWithOptions(
      value, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  return json;
}

bool AppendSandboxArgs(brillo::ErrorPtr* error,
                       const std::string& function_name,
                       std::vector<std::string>* parsed_args) {
  std::string minijail_args_str;
  if (!base::ReadFileToString(base::FilePath(kSandboxArgs),
                              &minijail_args_str)) {
    DEBUGD_ADD_ERROR_FMT(error, kErrorPath,
                         "Failed to read minijail arguments from: %s",
                         kSandboxArgs);
    return false;
  }
  auto minijail_args_dict = base::JSONReader::Read(minijail_args_str);
  if (!minijail_args_dict || !minijail_args_dict->is_dict()) {
    DEBUGD_ADD_ERROR_FMT(error, kErrorPath,
                         "Minijail arguments are not stored in dict. Expected "
                         "dict but got: %s",
                         minijail_args_str.c_str());
    return false;
  }
  const auto* minijail_args = minijail_args_dict->FindListKey(function_name);
  if (!minijail_args) {
    DEBUGD_ADD_ERROR_FMT(error, kErrorPath,
                         "Arguments of \"%s\" is not found in minijail "
                         "arguments file: %s",
                         function_name.c_str(), kSandboxArgs);
    return false;
  }
  DVLOG(1) << "Minijail arguments: " << (*minijail_args);
  for (const auto& arg : minijail_args->GetList()) {
    if (!arg.is_string()) {
      auto arg_str = GetStringFromValue(arg);
      DEBUGD_ADD_ERROR_FMT(
          error, kErrorPath,
          "Failed to parse minijail arguments. Expected string but got: %s",
          arg_str.c_str());
      return false;
    }
    parsed_args->push_back(arg.GetString());
  }
  return true;
}

base::Optional<std::string> GetFunctionNameFromProbeStatement(
    brillo::ErrorPtr* error, const std::string& probe_statement) {
  // The name of the probe function is the only key in the dictionary.
  auto probe_statement_dict = base::JSONReader::Read(probe_statement);
  if (!probe_statement_dict || !probe_statement_dict->is_dict()) {
    DEBUGD_ADD_ERROR_FMT(
        error, kErrorPath,
        "Failed to parse probe statement. Expected json but got: %s",
        probe_statement.c_str());
    return base::nullopt;
  }
  if (probe_statement_dict->DictSize() != 1) {
    DEBUGD_ADD_ERROR_FMT(
        error, kErrorPath,
        "Expected only one probe function in probe statement but got: %zu",
        probe_statement_dict->DictSize());
    return base::nullopt;
  }
  auto it = probe_statement_dict->DictItems().begin();
  const auto& function_name = it->first;
  return function_name;
}

std::unique_ptr<brillo::Process> CreateSandboxedProcess(
    brillo::ErrorPtr* error, const std::string& probe_statement) {
  const auto function_name =
      GetFunctionNameFromProbeStatement(error, probe_statement);
  if (!function_name)
    return nullptr;

  auto sandboxed_process = std::make_unique<SandboxedProcess>();
  // The following is the general minijail set up for runtime_probe in debugd
  // /dev/log needs to be bind mounted before any possible tmpfs mount on run
  // See:
  //   minijail0 manpage (`man 1 minijail0` in cros\_sdk)
  //   https://chromium.googlesource.com/chromiumos/docs/+/HEAD/sandboxing.md
  std::vector<std::string> parsed_args{
      "-G",                // Inherit all the supplementary groups
      "-P", "/mnt/empty",  // Set /mnt/empty as the root fs using pivot_root
      "-b", "/",           // Bind mount rootfs
      "-b", "/proc",       // Bind mount /proc
      "-b", "/dev/log",    // Enable logging
      "-t",                // Mount a tmpfs on /tmp
      "-r",                // Remount /proc readonly
      "-d"                 // Mount /dev with a minimal set of nodes.
  };
  if (!AppendSandboxArgs(error, *function_name, &parsed_args))
    return nullptr;

  sandboxed_process->SandboxAs(kRunAs, kRunAs);
  const auto seccomp_path = base::FilePath{kSandboxInfoDir}.Append(
      base::StringPrintf("%s-seccomp.policy", function_name->c_str()));
  if (!base::PathExists(seccomp_path)) {
    DEBUGD_ADD_ERROR_FMT(error, kErrorPath,
                         "Seccomp policy file of \"%s\" is not found at: %s",
                         function_name->c_str(), seccomp_path.value().c_str());
    return nullptr;
  }
  sandboxed_process->SetSeccompFilterPolicyFile(seccomp_path.MaybeAsASCII());
  DVLOG(1) << "Sandbox for " << (*function_name) << " is ready";
  if (!sandboxed_process->Init(parsed_args)) {
    DEBUGD_ADD_ERROR(error, kErrorPath,
                     "Sandboxed process initialization failure");
    return nullptr;
  }
  return sandboxed_process;
}

}  // namespace

bool ProbeTool::EvaluateProbeFunction(
    brillo::ErrorPtr* error,
    const std::string& probe_statement,
    brillo::dbus_utils::FileDescriptor* outfd) {
  // Details of sandboxing for probing should be centralized in a single
  // directory. Sandboxing is mandatory when we don't allow debug features.
  auto process = CreateSandboxedProcess(error, probe_statement);
  if (process == nullptr)
    return false;

  base::ScopedFD read_fd, write_fd;
  if (!CreateNonblockingPipe(&read_fd, &write_fd)) {
    DEBUGD_ADD_ERROR(error, kErrorPath, "Cannot create a pipe");
    return false;
  }

  for (auto arg : kBinaryAndArgs) {
    process->AddArg(arg);
  }
  process->AddArg(probe_statement);
  process->BindFd(write_fd.get(), STDOUT_FILENO);
  process->Start();
  process->Release();
  *outfd = std::move(read_fd);
  return true;
}

}  // namespace debugd
