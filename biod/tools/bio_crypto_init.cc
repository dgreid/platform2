// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/tools/bio_crypto_init.h"

#include <fcntl.h>
#include <sys/types.h>

#include <algorithm>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/process.h>
#include <brillo/daemons/daemon.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>

#include "biod/cros_fp_device.h"
#include "biod/ec_command.h"

namespace {
constexpr int64_t kTpmSeedSize = FP_CONTEXT_TPM_BYTES;
}  // namespace

namespace biod {

// Helper function to ensure data of a file is removed.
bool BioCryptoInit::NukeFile(const base::FilePath& filepath) {
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

bool BioCryptoInit::WriteSeedToCrosFp(const brillo::SecureVector& seed) {
  bool ret = true;
  auto fd = OpenCrosFpDevice();
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Couldn't open FP device for ioctl.";
    return false;
  }

  if (!WaitOnEcBoot(fd, EC_IMAGE_RW)) {
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
  if (!CrosFpTemplateVersionCompatible(firmware_fp_template_format_version,
                                       FP_TEMPLATE_FORMAT_VERSION)) {
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
  std::copy(seed.cbegin(), seed.cbegin() + sizeof(req->seed), req->seed);

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

bool BioCryptoInit::DoProgramSeed(const brillo::SecureVector& tpm_seed) {
  bool ret = true;

  if (!WriteSeedToCrosFp(tpm_seed)) {
    LOG(ERROR) << "Failed to send seed to CrOS FP device.";
    ret = false;
  }

  return ret;
}

base::ScopedFD BioCryptoInit::OpenCrosFpDevice() {
  return base::ScopedFD(
      open(biod::CrosFpDevice::kCrosFpPath, O_RDWR | O_CLOEXEC));
}

bool BioCryptoInit::WaitOnEcBoot(const base::ScopedFD& cros_fp_fd,
                                 ec_image expected_image) {
  return biod::CrosFpDevice::WaitOnEcBoot(cros_fp_fd, expected_image);
}

bool BioCryptoInit::CrosFpTemplateVersionCompatible(
    const uint32_t firmware_fp_template_format_version,
    const uint32_t biod_fp_template_format_version) {
  // We should modify the rule here when we uprev the template format version.
  switch (firmware_fp_template_format_version) {
    case 3:
    case 4:
      break;
    default:
      return false;
  }
  switch (biod_fp_template_format_version) {
    case 3:
    case 4:
      break;
    default:
      return false;
  }
  // If biod has template version 4, firmware with version 3 is still
  // compatible until we deprecate it.
  if (firmware_fp_template_format_version == 3 &&
      biod_fp_template_format_version == 4)
    return true;
  return firmware_fp_template_format_version == biod_fp_template_format_version;
}

}  // namespace biod
