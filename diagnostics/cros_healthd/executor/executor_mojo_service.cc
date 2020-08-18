// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_mojo_service.h"

#include <inttypes.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>

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
constexpr char kEctoolUserAndGroup[] = "healthd_ec";
constexpr char kEctoolBinary[] = "/usr/sbin/ectool";
// The ectool command used to collect fan speed in RPM.
constexpr char kGetFanRpmCommand[] = "pwmgetfanrpm";

// SECCOMP policy for memtester, relative to kSandboxDirPath.
constexpr char kMemtesterSeccompPolicyPath[] = "memtester-seccomp.policy";
constexpr char kMemtesterBinary[] = "/usr/sbin/memtester";

// Runs the given binary with the given arguments and sandboxing. If specified,
// |user| will be used as both the user and group for sandboxing the binary. If
// not specified, the default cros_healthd:cros_healthd user and group will be
// used.
int RunBinary(const base::FilePath& seccomp_policy_path,
              const std::vector<std::string>& sandboxing_args,
              const base::Optional<std::string>& user,
              const base::FilePath& binary_path,
              const std::vector<std::string>& binary_args,
              mojo_ipc::ProcessResult* result) {
  if (!base::PathExists(seccomp_policy_path)) {
    result->err = "Sandbox info is missing for this architecture.";
    return EXIT_FAILURE;
  }

  // Sandboxing setup for the process.
  ProcessWithOutput process;
  if (user.has_value())
    process.SandboxAs(user.value(), user.value());
  process.SetSeccompFilterPolicyFile(seccomp_policy_path.MaybeAsASCII());
  process.set_separate_stderr(true);
  if (!process.Init(sandboxing_args)) {
    result->err = "Process initialization failure.";
    return EXIT_FAILURE;
  }

  process.AddArg(binary_path.MaybeAsASCII());
  for (const auto& arg : binary_args)
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

  // Minijail setup for ectool.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-G");
  sandboxing_args.push_back("-c");
  sandboxing_args.push_back("cap_sys_rawio=e");
  sandboxing_args.push_back("-b");
  sandboxing_args.push_back("/dev/cros_ec");

  result.return_code =
      RunBinary(seccomp_policy_path, sandboxing_args, kEctoolUserAndGroup,
                base::FilePath(kEctoolBinary), {kGetFanRpmCommand}, &result);

  std::move(callback).Run(result.Clone());
}

void ExecutorMojoService::RunMemtester(RunMemtesterCallback callback) {
  mojo_ipc::ProcessResult result;

  int64_t available_mem = base::SysInfo::AmountOfAvailablePhysicalMemory();
  // Convert from bytes to MB.
  available_mem /= (1024 * 1024);
  // Make sure the operating system is left with at least 200 MB.
  available_mem -= 200;
  if (available_mem <= 0) {
    result.err = "Not enough available memory to run memtester.";
    result.return_code = EXIT_FAILURE;
    std::move(callback).Run(result.Clone());
    return;
  }

  // Minijail setup for memtester.
  std::vector<std::string> sandboxing_args;
  sandboxing_args.push_back("-c");
  sandboxing_args.push_back("cap_ipc_lock=e");

  // Additional args for memtester.
  std::vector<std::string> memtester_args;
  // Run with all free memory, except that which we left to the operating system
  // above.
  memtester_args.push_back(base::StringPrintf("%" PRId64, available_mem));
  // Run for one loop.
  memtester_args.push_back("1");

  const auto kSeccompPolicyPath =
      base::FilePath(kSandboxDirPath).Append(kMemtesterSeccompPolicyPath);

  // Since no user:group is specified, this will run with the default
  // cros_healthd:cros_healthd user and group.
  result.return_code =
      RunBinary(kSeccompPolicyPath, sandboxing_args, base::nullopt,
                base::FilePath(kMemtesterBinary), memtester_args, &result);

  std::move(callback).Run(result.Clone());
}

}  // namespace diagnostics
