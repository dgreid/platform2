// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_MOJO_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_MOJO_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <base/single_thread_task_runner.h>
#include <base/synchronization/lock.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "diagnostics/cros_healthd/process/process_with_output.h"
#include "mojo/cros_healthd_executor.mojom.h"

namespace diagnostics {

// Production implementation of the
// chromeos::cros_healthd_executor::mojom::Executor Mojo interface.
class ExecutorMojoService final
    : public chromeos::cros_healthd_executor::mojom::Executor {
 public:
  ExecutorMojoService(
      const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner,
      chromeos::cros_healthd_executor::mojom::ExecutorRequest request);
  ExecutorMojoService(const ExecutorMojoService&) = delete;
  ExecutorMojoService& operator=(const ExecutorMojoService&) = delete;

  // chromeos::cros_healthd_executor::mojom::Executor overrides:
  void GetFanSpeed(GetFanSpeedCallback callback) override;
  void RunMemtester(RunMemtesterCallback callback) override;
  void KillMemtester() override;
  void GetProcessIOContents(const uint32_t pid,
                            GetProcessIOContentsCallback callback) override;

 private:
  // Runs the given binary with the given arguments and sandboxing. If
  // specified, |user| will be used as both the user and group for sandboxing
  // the binary. If not specified, the default cros_healthd:cros_healthd user
  // and group will be used. Does not track the process it launches, so the
  // launched process cannot be cancelled once it is started. If cancelling is
  // required, RunTrackedBinary() should be used instead.
  void RunUntrackedBinary(
      const base::FilePath& seccomp_policy_path,
      const std::vector<std::string>& sandboxing_args,
      const base::Optional<std::string>& user,
      const base::FilePath& binary_path,
      const std::vector<std::string>& binary_args,
      chromeos::cros_healthd_executor::mojom::ProcessResult result,
      base::OnceCallback<void(
          chromeos::cros_healthd_executor::mojom::ProcessResultPtr)> callback);
  // Like RunUntrackedBinary() above, but tracks the process internally so that
  // it can be cancelled if necessary.
  void RunTrackedBinary(
      const base::FilePath& seccomp_policy_path,
      const std::vector<std::string>& sandboxing_args,
      const base::Optional<std::string>& user,
      const base::FilePath& binary_path,
      const std::vector<std::string>& binary_args,
      chromeos::cros_healthd_executor::mojom::ProcessResult result,
      base::OnceCallback<void(
          chromeos::cros_healthd_executor::mojom::ProcessResultPtr)> callback);
  // Helper function for RunUntrackedBinary() and RunTrackedBinary().
  int RunBinaryInternal(
      const base::FilePath& seccomp_policy_path,
      const std::vector<std::string>& sandboxing_args,
      const base::Optional<std::string>& user,
      const base::FilePath& binary_path,
      const std::vector<std::string>& binary_args,
      chromeos::cros_healthd_executor::mojom::ProcessResult* result,
      ProcessWithOutput* process);

  // Task runner for all Mojo callbacks.
  const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner_;

  // Provides a Mojo endpoint that cros_healthd can call to access the
  // executor's Mojo methods.
  mojo::Binding<chromeos::cros_healthd_executor::mojom::Executor> binding_;

  // Prevents multiple simultaneous writes to |processes_|.
  base::Lock lock_;

  // Tracks running processes owned by the executor. Used to kill processes if
  // requested.
  std::map<std::string, std::unique_ptr<ProcessWithOutput>> processes_;

  // Must be the last member of the class.
  base::WeakPtrFactory<ExecutorMojoService> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_EXECUTOR_MOJO_SERVICE_H_
