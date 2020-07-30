// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/disk_read/disk_read.h"

#include <list>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/numerics/safe_conversions.h>
#include <base/strings/string_number_conversions.h>
#include <base/system/sys_info.h>

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kTmpPath[] = "/var/cache/diagnostics";
constexpr char kTestFileName[] = "fio-test-file";
constexpr char kFioExePath[] = "/usr/bin/fio";
constexpr float kFileCreationTimePerMB = 0.005;
constexpr uint32_t kSpaceLowMB = 1024;

}  // namespace

namespace diagnostics {

std::unique_ptr<DiagnosticRoutine> CreateDiskReadRoutine(
    chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb) {
  std::vector<std::string> prepare_cmd{
      kFioExePath,
      "--name=prepare",
      "--filename=" + base::FilePath(kTmpPath).Append(kTestFileName).value(),
      "--size=" + base::NumberToString(file_size_mb) + "MB",
      "--verify=md5",
      "--rw=write",
      "--end_fsync=1"};

  std::vector<std::string> run_cmd{
      kFioExePath,
      "--name=run",
      "--filename=" + base::FilePath(kTmpPath).Append(kTestFileName).value(),
      "--time_based=1",
      "--runtime=" + base::NumberToString(exec_duration.InSeconds()),
      "--direct=1",
      (type == mojo_ipc::DiskReadRoutineTypeEnum::kLinearRead)
          ? "--rw=read"
          : "--rw=randread"};

  auto subproc_routine_ptr = std::make_unique<SubprocRoutine>(
      std::list<base::CommandLine>{base::CommandLine(prepare_cmd),
                                   base::CommandLine(run_cmd)},
      exec_duration.InSeconds() +
          static_cast<int>(kFileCreationTimePerMB * file_size_mb));

  // Ensure DUT has sufficient storage space and prevent storage space state
  // from falling into 'low' state during test.
  auto storage_space_check = base::BindOnce(
      [=](uint32_t file_size_mb) {
        int64_t available_storage_space_byte =
            base::SysInfo::AmountOfFreeDiskSpace(base::FilePath(kTmpPath));
        if (available_storage_space_byte == -1) {
          LOG(ERROR) << "Failed to retrieve available disk space";
          return false;
        }
        uint32_t available_storage_space_mb =
            base::checked_cast<uint32_t>(available_storage_space_byte / 1024 /
                                         1024) -
            kSpaceLowMB;
        if (available_storage_space_mb < file_size_mb) {
          LOG(ERROR) << "Insufficient storage space";
          return false;
        }
        return true;
      },
      file_size_mb);

  // Clean up test file created by fio.
  auto test_file_deletion = base::BindOnce([]() {
    auto test_file = base::FilePath(kTmpPath).Append(kTestFileName);
    if (base::PathExists(test_file)) {
      base::DeleteFile(test_file, false);
    }
  });

  subproc_routine_ptr->RegisterPreStartCallback(std::move(storage_space_check));
  subproc_routine_ptr->RegisterPostStopCallback(std::move(test_file_deletion));

  return std::move(subproc_routine_ptr);
}

}  // namespace diagnostics
