// Copyright 2018 The Chromium OS Authors. All rights reserved.
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

#include "biod/tools/bio_crypto_init.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
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
#include "biod/cros_fp_device.h"
#include "biod/ec_command.h"

namespace {

constexpr int64_t kTimeoutSeconds = 30;
constexpr int64_t kTpmSeedSize = FP_CONTEXT_TPM_BYTES;
// File where the TPM seed is stored, that we have to read from.
constexpr char kBioTpmSeedTmpFile[] = "/run/bio_crypto_init/seed";

// Helper function to ensure data of a file is removed.
bool NukeFile(const base::FilePath& filepath) {
  // Write all zeros to the FD.
  bool ret = true;
  std::vector<uint8_t> zero_vec(kTpmSeedSize, 0);
  if (base::WriteFile(filepath, reinterpret_cast<const char*>(zero_vec.data()),
                      kTpmSeedSize) != kTpmSeedSize) {
    PLOG(ERROR) << "Failed to write all-zero to tmpfs file.";
    ret = false;
  }

  if (!base::DeleteFile(filepath, false)) {
    PLOG(ERROR) << "Failed to delete TPM seed file: " << filepath.value();
    ret = false;
  }

  return ret;
}

bool WriteSeedToCrosFp(const brillo::SecureVector& seed) {
  bool ret = true;
  auto fd =
      base::ScopedFD(open(biod::CrosFpDevice::kCrosFpPath, O_RDWR | O_CLOEXEC));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Couldn't open FP device for ioctl.";
    return false;
  }

  if (!biod::CrosFpDevice::WaitOnEcBoot(fd, EC_IMAGE_RW)) {
    LOG(ERROR) << "FP device did not boot to RW.";
    return false;
  }

  biod::EcCommand<biod::EmptyParam, struct ec_response_fp_info> cmd_fp_info(
      EC_CMD_FP_INFO, biod::kVersionOne);
  if (!cmd_fp_info.RunWithMultipleAttempts(
          fd.get(), biod::CrosFpDevice::kMaxIoAttempts)) {
    LOG(ERROR) << "Checking template format compatibility: failed to get FP "
                  "information.";
    return false;
  }

  const uint32_t firmware_fp_template_format_version =
      cmd_fp_info.Resp()->template_version;
  if (!biod::CrosFpTemplateVersionCompatible(
          firmware_fp_template_format_version, FP_TEMPLATE_FORMAT_VERSION)) {
    LOG(ERROR) << "Incompatible template version between FPMCU ("
               << firmware_fp_template_format_version << ") and biod ("
               << FP_TEMPLATE_FORMAT_VERSION << ").";
    return false;
  }

  biod::EcCommand<struct ec_params_fp_seed, biod::EmptyParam> cmd_seed(
      EC_CMD_FP_SEED);
  struct ec_params_fp_seed* req = cmd_seed.Req();
  // We have ensured that the format versions of the firmware and biod are
  // compatible, so use the format version of the firmware.
  req->struct_version =
      static_cast<uint16_t>(firmware_fp_template_format_version);
  std::copy(seed.cbegin(), seed.cend() + sizeof(req->seed), req->seed);

  if (!cmd_seed.Run(fd.get())) {
    LOG(ERROR) << "Failed to set TPM seed.";
    ret = false;
  } else {
    LOG(INFO) << "Successfully set FP seed.";
  }
  std::fill(req->seed, req->seed + sizeof(req->seed), 0);
  // Clear intermediate buffers. We expect the command to fail since the SBP
  // will reject the new seed.
  cmd_seed.Run(fd.get());

  return ret;
}

bool DoProgramSeed(const brillo::SecureVector& tpm_seed) {
  bool ret = true;

  if (!WriteSeedToCrosFp(tpm_seed)) {
    LOG(ERROR) << "Failed to send seed to CrOS FP device.";
    ret = false;
  }

  return ret;
}

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
#if BASE_VER < 780000
  logging_settings.log_file = log_file_path.value().c_str();
#else
  logging_settings.log_file_path = log_file_path.value().c_str();
#endif
  logging_settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  logging_settings.delete_old = logging::DELETE_OLD_LOG_FILE;
  logging::InitLogging(logging_settings);
  logging::SetLogItems(true,    // process ID
                       true,    // thread ID
                       true,    // timestamp
                       false);  // tickcount

  biod::LogVersion();

  // We fork the process so that can we program the seed in the child, and
  // terminate it if it hangs.
  pid_t pid = fork();
  if (pid == -1) {
    PLOG(ERROR) << "Failed to fork child process for bio_wash.";
    NukeFile(base::FilePath(kBioTpmSeedTmpFile));
    return -1;
  }

  if (pid == 0) {
    // The first thing we do is read the buffer, and delete the file.
    brillo::SecureVector tpm_seed(kTpmSeedSize);
    int bytes_read = base::ReadFile(base::FilePath(kBioTpmSeedTmpFile),
                                    reinterpret_cast<char*>(tpm_seed.data()),
                                    tpm_seed.size());
    NukeFile(base::FilePath(kBioTpmSeedTmpFile));

    if (bytes_read != kTpmSeedSize) {
      LOG(ERROR) << "Failed to read TPM seed from tmpfile: " << bytes_read;
      return -1;
    }
    return DoProgramSeed(tpm_seed) ? 0 : -1;
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
