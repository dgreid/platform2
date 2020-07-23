// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_mojo_service.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "diagnostics/cros_healthd/process/process_with_output.h"
#include "mojo/cros_healthd_executor.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd_executor::mojom;

// All SECCOMP policies should live in this directory.
constexpr char kSandboxDirPath[] = "/usr/share/policy/";
// SECCOMP policy for ectool pwmgetfanrpm:
constexpr char kFanSpeedSeccompPolicyPath[] =
    "ectool_pwmgetfanrpm-seccomp.policy";
constexpr char kRunAs[] = "healthd_ec";
constexpr char kEctoolBinary[] = "/usr/sbin/ectool";
// The ectool command used to collect fan speed in RPM.
constexpr char kGetFanRpmCommand[] = "pwmgetfanrpm";

// Runs ectool with the given arguments.
int RunEctool(const base::FilePath& seccomp_policy_path,
              const std::vector<std::string>& ectool_args,
              mojo_ipc::ProcessResult* result) {
  if (!base::PathExists(seccomp_policy_path)) {
    result->err = "Sandbox info is missing for this architecture.";
    return EXIT_FAILURE;
  }

  // Minijail setup for ectool.
  std::vector<std::string> parsed_args;
  parsed_args.push_back("-c");
  parsed_args.push_back("cap_sys_rawio=e");
  parsed_args.push_back("-b");
  parsed_args.push_back("/dev/cros_ec");

  ProcessWithOutput process;
  process.SandboxAs(kRunAs, kRunAs);
  process.SetSeccompFilterPolicyFile(seccomp_policy_path.MaybeAsASCII());
  process.InheritUsergroups();
  process.set_separate_stderr(true);
  if (!process.Init(parsed_args)) {
    result->err = "Process initialization failure.";
    return EXIT_FAILURE;
  }

  process.AddArg(kEctoolBinary);
  for (const auto& arg : ectool_args)
    process.AddArg(arg);
  int exit_code = process.Run();
  if (exit_code != EXIT_SUCCESS) {
    process.GetError(&result->err);
    result->err = "Failed to run process.";
    return exit_code;
  }

  if (!process.GetOutput(&result->out)) {
    result->err = "Failed to get output from process.";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

}  // namespace

ExecutorMojoService::ExecutorMojoService(mojo_ipc::ExecutorRequest request)
    : binding_{this /* impl */, std::move(request)} {
  binding_.set_connection_error_handler(
      base::BindOnce([]() { std::exit(EXIT_SUCCESS); }));
}

void ExecutorMojoService::GetFanSpeed(GetFanSpeedCallback callback) {
  mojo_ipc::ProcessResult result;

  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath).Append(kFanSpeedSeccompPolicyPath);
  result.return_code =
      RunEctool(seccomp_policy_path, {kGetFanRpmCommand}, &result);

  std::move(callback).Run(result.Clone());
}

}  // namespace diagnostics
