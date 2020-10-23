// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This is a program to set the various biometric managers with a TPM
// seed obtained from the TPM hardware. It is expected to execute once
// on every boot.
// This binary is expected to be called from the mount-encrypted utility
// during boot.
// It is expected to receive the tpm seed buffer from mount-encrypted via a
// file written to tmpfs. The FD for the tmpfs file is mapped to STDIN_FILENO
// by mount-encrypted. It is considered to have been unlinked by
// mount-encrypted. Consequently, closing the FD should be enough to delete
// the file.

#include "biod/crypto_init/bio_crypto_init.h"

#include <sys/types.h>

#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/process.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/daemons/daemon.h>
#include <brillo/flag_helper.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>

#include "biod/biod_version.h"
#include "biod/ec_command.h"
#include "biod/fp_seed_command.h"

namespace {
constexpr int64_t kTimeoutSeconds = 30;
// File where the TPM seed is stored, that we have to read from.
constexpr char kBioTpmSeedTmpFile[] = "/run/bio_crypto_init/seed";
}  // namespace

int main(int argc, char* argv[]) {
  // Set up logging settings.
  DEFINE_string(log_dir, "/var/log/bio_crypto_init",
                "Directory where logs are written.");

  brillo::FlagHelper::Init(argc, argv,
                           "bio_crypto_init, the Chromium OS binary to program "
                           "bio sensors with TPM secrets.");

  const auto log_dir_path = base::FilePath(FLAGS_log_dir);
  const auto log_file_path = log_dir_path.Append(base::StringPrintf(
      "bio_crypto_init.%s",
      brillo::GetTimeAsLogString(base::Time::Now()).c_str()));

  brillo::UpdateLogSymlinks(log_dir_path.Append("bio_crypto_init.LATEST"),
                            log_dir_path.Append("bio_crypto_init.PREVIOUS"),
                            log_file_path);

  logging::LoggingSettings logging_settings;
  logging_settings.logging_dest = logging::LOG_TO_FILE;
  logging_settings.log_file_path = log_file_path.value().c_str();
  logging_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  logging_settings.delete_old = logging::DELETE_OLD_LOG_FILE;
  logging::InitLogging(logging_settings);
  logging::SetLogItems(true,    // process ID
                       true,    // thread ID
                       true,    // timestamp
                       false);  // tickcount

  biod::LogVersion();

  biod::BioCryptoInit bio_crypto_init;

  // We fork the process so that can we program the seed in the child, and
  // terminate it if it hangs.
  pid_t pid = fork();
  if (pid == -1) {
    PLOG(ERROR) << "Failed to fork child process for bio_wash.";
    bio_crypto_init.NukeFile(base::FilePath(kBioTpmSeedTmpFile));
    return -1;
  }

  if (pid == 0) {
    // The first thing we do is read the buffer, and delete the file.
    brillo::SecureVector tpm_seed(biod::FpSeedCommand::kTpmSeedSize);
    int bytes_read = base::ReadFile(base::FilePath(kBioTpmSeedTmpFile),
                                    reinterpret_cast<char*>(tpm_seed.data()),
                                    tpm_seed.size());
    bio_crypto_init.NukeFile(base::FilePath(kBioTpmSeedTmpFile));

    if (bytes_read != biod::FpSeedCommand::kTpmSeedSize) {
      LOG(ERROR) << "Failed to read TPM seed from tmpfile: " << bytes_read;
      return -1;
    }
    return bio_crypto_init.DoProgramSeed(tpm_seed) ? 0 : -1;
  }

  auto process = base::Process::Open(pid);
  int exit_code;
  if (!process.WaitForExitWithTimeout(
          base::TimeDelta::FromSeconds(kTimeoutSeconds), &exit_code)) {
    LOG(ERROR) << "bio_crypto_init timeout, exit code: " << exit_code;
    process.Terminate(-1, false);
  }

  return exit_code;
}
