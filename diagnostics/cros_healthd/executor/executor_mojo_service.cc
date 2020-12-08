// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_mojo_service.h"

#include <inttypes.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/time/time.h>
#include <brillo/process/process.h>

#include "diagnostics/cros_healthd/process/process_with_output.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"
#include "mojo/cros_healthd_executor.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd_executor::mojom;

// Amount of time we wait for a process to respond to SIGTERM before killing it.
constexpr base::TimeDelta kTerminationTimeout = base::TimeDelta::FromSeconds(2);

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

// All Mojo callbacks need to be ran by the Mojo task runner, so this provides a
// convenient wrapper that can be bound and ran by that specific task runner.
void RunMojoProcessResultCallback(
    mojo_ipc::ProcessResult mojo_result,
    base::OnceCallback<void(mojo_ipc::ProcessResultPtr)> callback) {
  std::move(callback).Run(mojo_result.Clone());
}

}  // namespace

ExecutorMojoService::ExecutorMojoService(
    const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner,
    mojo_ipc::ExecutorRequest request)
    : mojo_task_runner_(mojo_task_runner),
      binding_{this /* impl */, std::move(request)} {
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

  std::vector<std::string> binary_args = {kGetFanRpmCommand};
  base::FilePath binary_path = base::FilePath(kEctoolBinary);

  base::OnceClosure closure = base::BindOnce(
      &ExecutorMojoService::RunUntrackedBinary, weak_factory_.GetWeakPtr(),
      seccomp_policy_path, sandboxing_args, kEctoolUserAndGroup, binary_path,
      binary_args, std::move(result), std::move(callback));

  base::ThreadPool::PostTask(FROM_HERE, {base::MayBlock()}, std::move(closure));
}

void ExecutorMojoService::RunMemtester(RunMemtesterCallback callback) {
  mojo_ipc::ProcessResult result;

  // Only allow one instance of memtester at a time. This is reasonable, because
  // memtester mlocks almost the entirety of the device's memory, and a second
  // memtester process wouldn't have any memory to test.
  auto itr = processes_.find(kMemtesterBinary);
  if (itr != processes_.end()) {
    result.return_code = EXIT_FAILURE;
    result.err = "Memtester process already running.";
    std::move(callback).Run(result.Clone());
    return;
  }

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
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock()},
      base::BindOnce(&ExecutorMojoService::RunTrackedBinary,
                     weak_factory_.GetWeakPtr(), kSeccompPolicyPath,
                     sandboxing_args, base::nullopt,
                     base::FilePath(kMemtesterBinary), memtester_args,
                     std::move(result), std::move(callback)));
}

void ExecutorMojoService::KillMemtester() {
  base::AutoLock auto_lock(lock_);
  auto itr = processes_.find(kMemtesterBinary);
  if (itr == processes_.end())
    return;

  brillo::Process* process = itr->second.get();
  // If the process has ended, don't try to kill anything.
  if (!process->pid())
    return;

  // Try to terminate the process nicely, then kill it if necessary.
  if (!process->Kill(SIGTERM, kTerminationTimeout.InSeconds()))
    process->Kill(SIGKILL, kTerminationTimeout.InSeconds());
}

void ExecutorMojoService::GetProcessIOContents(
    const uint32_t pid, GetProcessIOContentsCallback callback) {
  std::string result;

  ReadAndTrimString(base::FilePath("/proc/")
                        .Append(base::StringPrintf("%" PRId32, pid))
                        .AppendASCII("io"),
                    &result);

  std::move(callback).Run(result);
}

void ExecutorMojoService::RunUntrackedBinary(
    const base::FilePath& seccomp_policy_path,
    const std::vector<std::string>& sandboxing_args,
    const base::Optional<std::string>& user,
    const base::FilePath& binary_path,
    const std::vector<std::string>& binary_args,
    mojo_ipc::ProcessResult result,
    base::OnceCallback<void(mojo_ipc::ProcessResultPtr)> callback) {
  auto process = std::make_unique<ProcessWithOutput>();
  result.return_code =
      RunBinaryInternal(seccomp_policy_path, sandboxing_args, user, binary_path,
                        binary_args, &result, process.get());
  mojo_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&RunMojoProcessResultCallback,
                                std::move(result), std::move(callback)));
}

void ExecutorMojoService::RunTrackedBinary(
    const base::FilePath& seccomp_policy_path,
    const std::vector<std::string>& sandboxing_args,
    const base::Optional<std::string>& user,
    const base::FilePath& binary_path,
    const std::vector<std::string>& binary_args,
    mojo_ipc::ProcessResult result,
    base::OnceCallback<void(mojo_ipc::ProcessResultPtr)> callback) {
  std::string binary_path_str = binary_path.value();
  DCHECK(!processes_.count(binary_path_str));

  {
    auto process = std::make_unique<ProcessWithOutput>();
    base::AutoLock auto_lock(lock_);
    processes_[binary_path_str] = std::move(process);
  }

  result.return_code = RunBinaryInternal(
      seccomp_policy_path, sandboxing_args, user, binary_path, binary_args,
      &result, processes_[binary_path_str].get());

  // Get rid of the process.
  base::AutoLock auto_lock(lock_);
  auto itr = processes_.find(binary_path_str);
  DCHECK(itr != processes_.end());
  processes_.erase(itr);
  mojo_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&RunMojoProcessResultCallback,
                                std::move(result), std::move(callback)));
}

int ExecutorMojoService::RunBinaryInternal(
    const base::FilePath& seccomp_policy_path,
    const std::vector<std::string>& sandboxing_args,
    const base::Optional<std::string>& user,
    const base::FilePath& binary_path,
    const std::vector<std::string>& binary_args,
    mojo_ipc::ProcessResult* result,
    ProcessWithOutput* process) {
  DCHECK(result);
  DCHECK(process);

  if (!base::PathExists(seccomp_policy_path)) {
    result->err = "Sandbox info is missing for this architecture.";
    return EXIT_FAILURE;
  }

  // Sandboxing setup for the process.
  if (user.has_value())
    process->SandboxAs(user.value(), user.value());
  process->SetSeccompFilterPolicyFile(seccomp_policy_path.MaybeAsASCII());
  process->set_separate_stderr(true);
  if (!process->Init(sandboxing_args)) {
    result->err = "Process initialization failure.";
    return EXIT_FAILURE;
  }

  process->AddArg(binary_path.MaybeAsASCII());
  for (const auto& arg : binary_args)
    process->AddArg(arg);
  int exit_code = process->Run();
  if (exit_code != EXIT_SUCCESS) {
    process->GetError(&result->err);
    return exit_code;
  }

  if (!process->GetOutput(&result->out)) {
    result->err = "Failed to get output from process.";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

}  // namespace diagnostics
