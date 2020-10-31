// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/syslog_logging.h>

#include "oobe_config/rollback_constants.h"
#include "oobe_config/rollback_helper.h"

namespace {

void InitLog() {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  logging::SetLogItems(true /* enable_process_id */,
                       true /* enable_thread_id */, true /* enable_timestamp */,
                       true /* enable_tickcount */);
}

}  // namespace

// The caller of this function, |chromeos_startup| script, will reset the TPM on
// non-zero return value.
int main(int argc, char* argv[]) {
  InitLog();

  if (base::PathExists(oobe_config::kOobeCompletedFile)) {
    // OOBE has already been completed so cleanup all restore files.
    LOG(INFO) << "OOBE is already complete. Cleaning up restore files.";
    oobe_config::CleanupRestoreFiles(
        base::FilePath() /* root_path */,
        std::set<std::string>() /* excluded_files */);
    return 0;
  }

  // Something went wrong in the first stage of rollback restore. Reset TPM.
  if (base::PathExists(oobe_config::kFirstStageErrorFile)) {
    return 1;
  }

  if (!oobe_config::FinishRestore(base::FilePath() /* root_path */,
                                  false /* ignore_permissions_for_testing */)) {
    return 1;
  }
  return 0;
}
