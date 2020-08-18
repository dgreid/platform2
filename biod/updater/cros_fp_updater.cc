// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/cros_fp_updater.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <utility>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/time/time.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <chromeos/ec/ec_commands.h>
#include <cros_config/cros_config_interface.h>

#include "biod/cros_fp_device.h"
#include "biod/cros_fp_firmware.h"
#include "biod/ec_command.h"
#include "biod/updater/update_reason.h"
#include "biod/updater/update_status.h"

namespace {

constexpr base::TimeDelta kBootSplashScreenLaunchTimeout =
    base::TimeDelta::FromSeconds(10);

constexpr char kFlashromPath[] = "/usr/sbin/flashrom";
constexpr char kRebootFile[] = "/tmp/force_reboot_after_fw_update";

constexpr char kUpdateDisableFile[] = "/opt/google/biod/fw/.disable_fp_updater";
constexpr char kFirmwareLegacyBoardPattern[] = "*_fp";
constexpr char kFirmwareGlobSuffix[] = "_*.bin";

bool UpdateImage(const biod::CrosFpDeviceUpdate& ec_dev,
                 const biod::CrosFpBootUpdateCtrl& boot_ctrl,
                 const biod::CrosFpFirmware& fw,
                 enum ec_current_image image) {
  if (boot_ctrl.TriggerBootUpdateSplash()) {
    DLOG(INFO) << "Successfully launched update splash screen.";
  } else {
    DLOG(ERROR) << "Failed to launch boot update splash screen, continuing.";
  }
  if (!ec_dev.Flash(fw, image)) {
    LOG(ERROR) << "Failed to flash "
               << biod::CrosFpDeviceUpdate::EcCurrentImageToString(image)
               << ", aborting.";
    return false;
  }

  // If we updated the FW, we need to reboot (b/119222361).
  // We only reboot if we succeed, since we do not want to
  // create a reboot loop.
  if (boot_ctrl.ScheduleReboot()) {
    DLOG(INFO) << "Successfully scheduled reboot after update.";
  } else {
    DLOG(ERROR) << "Failed to schedule reboot after update, continuing.";
  }
  return true;
}

}  // namespace

namespace biod {

constexpr char kCrosConfigFPPath[] = "/fingerprint";
constexpr char kCrosConfigFPBoard[] = "board";
constexpr char kCrosConfigFPLocation[] = "sensor-location";

std::string CrosFpDeviceUpdate::EcCurrentImageToString(
    enum ec_current_image image) {
  switch (image) {
    case EC_IMAGE_UNKNOWN:
      return "UNKNOWN";
    case EC_IMAGE_RO:
      return "RO";
    case EC_IMAGE_RW:
      return "RW";
    default:
      return "INVALID";
  }
  NOTREACHED();
}

bool CrosFpDeviceUpdate::GetVersion(CrosFpDevice::EcVersion* ecver) const {
  DCHECK(ecver != nullptr);

  auto fd = base::ScopedFD(open(CrosFpDevice::kCrosFpPath, O_RDWR | O_CLOEXEC));
  if (!fd.is_valid()) {
    LOG(ERROR) << "Failed to open fingerprint device, while fetching version.";
    return false;
  }

  if (!biod::CrosFpDevice::GetVersion(fd, ecver)) {
    LOG(ERROR) << "Failed to read fingerprint version.";
    return false;
  }
  return true;
}

bool CrosFpDeviceUpdate::IsFlashProtectEnabled(bool* status) const {
  DCHECK(status != nullptr);

  auto fd = base::ScopedFD(open(CrosFpDevice::kCrosFpPath, O_RDWR | O_CLOEXEC));
  if (!fd.is_valid()) {
    LOG(ERROR) << "Failed to open fingerprint device, while fetching "
                  "flashprotect status.";
    return false;
  }

  biod::EcCommand<struct ec_params_flash_protect,
                  struct ec_response_flash_protect>
      fp_cmd(EC_CMD_FLASH_PROTECT, EC_VER_FLASH_PROTECT);
  fp_cmd.Req()->mask = 0;
  fp_cmd.Req()->flags = 0;
  if (!fp_cmd.Run(fd.get())) {
    LOG(ERROR) << "Failed to fetch fingerprint flashprotect flags.";
    return false;
  }
  *status = fp_cmd.Resp()->flags & EC_FLASH_PROTECT_RO_NOW;
  return true;
}

bool CrosFpDeviceUpdate::Flash(const CrosFpFirmware& fw,
                               enum ec_current_image image) const {
  DCHECK(image == EC_IMAGE_RO || image == EC_IMAGE_RW);

  std::string image_str = EcCurrentImageToString(image);

  LOG(INFO) << "Flashing " << image_str << " of FPMCU.";

  base::CommandLine cmd{base::FilePath(kFlashromPath)};
  cmd.AppendSwitch("fast-verify");
  cmd.AppendSwitchASCII("programmer", "ec:type=fp");
  cmd.AppendSwitchASCII("image", "EC_" + image_str);

  // The write switch does not work with --write=<PATH> syntax.
  // It must appear as --write <PATH>.
  cmd.AppendSwitch("write");
  cmd.AppendArgPath(fw.GetPath());

  DLOG(INFO) << "Launching '" << cmd.GetCommandLineString() << "'.";

  // TODO(b/130026657): Impose timeout on flashrom.
  std::string cmd_output;
  bool status = base::GetAppOutputAndError(cmd, &cmd_output);
  const auto lines = base::SplitStringPiece(
      cmd_output, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto line : lines) {
    LOG(INFO) << cmd.GetProgram().BaseName().value() << ": " << line;
  }
  if (!status) {
    LOG(ERROR) << "FPMCU flash utility failed.";
    return false;
  }
  return true;
}

// Show splashscreen about critical update to the user so they don't
// reboot in the middle, potentially during RO update.
bool CrosFpBootUpdateCtrl::TriggerBootUpdateSplash() const {
  LOG(INFO) << "Launching update splash screen.";

  int exit_code;
  base::CommandLine cmd{base::FilePath("chromeos-boot-alert")};
  cmd.AppendArg("update_firmware");

  DLOG(INFO) << "Launching '" << cmd.GetCommandLineString() << "'.";

  // libchrome does not include a wrapper for capturing a process output
  // and having an active timeout.
  // Since boot splash screen can hang forever, it is more important
  // to have a dedicated timeout in this process launch than to log
  // the launch process's output.
  // TODO(b/130026657): Capture stdout/stderr and forward to logger.
  base::LaunchOptions opt;
  auto p = base::LaunchProcess(cmd, opt);
  if (!p.WaitForExitWithTimeout(kBootSplashScreenLaunchTimeout, &exit_code)) {
    LOG(ERROR) << "Update splash screen launcher timeout met.";
    return false;
  }
  if (exit_code != EXIT_SUCCESS) {
    LOG(ERROR) << "Update splash screen launcher exited with bad status.";
    return false;
  }
  return true;
}

bool CrosFpBootUpdateCtrl::ScheduleReboot() const {
  LOG(INFO) << "Scheduling post update reboot.";

  // Trigger a file create.
  base::File file(base::FilePath(kRebootFile),
                  base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!file.IsValid()) {
    LOG(ERROR) << "Failed to schedule post update reboot: "
               << base::File::ErrorToString(file.error_details());
    return false;
  }
  return true;
}

namespace updater {

constexpr char kFirmwareDir[] = "/opt/google/biod/fw";

// FindFirmwareFile searches |directory| for a single firmware file
// that matches the |board_name|+|kFirmwareGlobSuffix| file pattern.
// If a single matching firmware file is found is found,
// its path is written to |file|. Otherwise, |file| will be untouched.
FindFirmwareFileStatus FindFirmwareFile(const base::FilePath& directory,
                                        const std::string& board_name,
                                        base::FilePath* file) {
  if (!base::DirectoryExists(directory)) {
    return FindFirmwareFileStatus::kNoDirectory;
  }

  std::string glob(board_name + std::string(kFirmwareGlobSuffix));
  base::FileEnumerator fw_bin_list(directory, false,
                                   base::FileEnumerator::FileType::FILES, glob);

  // Find provided firmware file
  base::FilePath fw_bin = fw_bin_list.Next();
  if (fw_bin.empty()) {
    return FindFirmwareFileStatus::kFileNotFound;
  }
  LOG(INFO) << "Found firmware file '" << fw_bin.value() << "'.";

  // Ensure that there are no other firmware files
  bool extra_fw_files = false;
  for (base::FilePath fw_extra = fw_bin_list.Next(); !fw_extra.empty();
       fw_extra = fw_bin_list.Next()) {
    extra_fw_files = true;
    LOG(ERROR) << "Found firmware file '" << fw_extra.value() << "'.";
  }
  if (extra_fw_files) {
    return FindFirmwareFileStatus::kMultipleFiles;
  }

  *file = fw_bin;
  return FindFirmwareFileStatus::kFoundFile;
}

FindFirmwareFileStatus FindFirmwareFile(
    const base::FilePath& directory,
    brillo::CrosConfigInterface* cros_config,
    base::FilePath* file) {
  std::string board_name;
  if (cros_config->GetString(kCrosConfigFPPath, kCrosConfigFPBoard,
                             &board_name)) {
    LOG(INFO) << "Identified fingerprint board name as '" << board_name << "'.";
  } else {
    LOG(WARNING) << "Fingerprint board name is unavailable, continuing with "
                    "legacy update.";
    board_name = kFirmwareLegacyBoardPattern;
  }

  return FindFirmwareFile(directory, board_name, file);
}

std::string FindFirmwareFileStatusToString(FindFirmwareFileStatus status) {
  switch (status) {
    case FindFirmwareFileStatus::kFoundFile:
      return "Firmware file found.";
    case FindFirmwareFileStatus::kNoDirectory:
      return "Firmware directory does not exist.";
    case FindFirmwareFileStatus::kFileNotFound:
      return "Firmware file not found.";
    case FindFirmwareFileStatus::kMultipleFiles:
      return "More than one firmware file was found.";
  }

  NOTREACHED();
  return "Unknown find firmware file status encountered.";
}

bool UpdateDisallowed() {
  return base::PathExists(base::FilePath(kUpdateDisableFile));
}

// Since /fingerprint/sensor-location is an optional field, the only information
// that is relevant to the updater is if fingerprint is explicitly not
// supported.
bool FingerprintUnsupported(brillo::CrosConfigInterface* cros_config) {
  std::string fingerprint_location;
  if (cros_config->GetString(kCrosConfigFPPath, kCrosConfigFPLocation,
                             &fingerprint_location)) {
    if (fingerprint_location == "none") {
      return true;
    }
  }

  return false;
}

UpdateResult DoUpdate(const CrosFpDeviceUpdate& ec_dev,
                      const CrosFpBootUpdateCtrl& boot_ctrl,
                      const CrosFpFirmware& fw) {
  bool attempted = false;
  UpdateResult result = {UpdateStatus::kUpdateNotNecessary,
                         UpdateReason::kNone};

  // Grab the new firmware file's versions.
  CrosFpFirmware::ImageVersion fw_version = fw.GetVersion();

  // Grab the FPMCU's current firmware version and current active image.
  CrosFpDevice::EcVersion ecver;
  if (!ec_dev.GetVersion(&ecver)) {
    result.status = UpdateStatus::kUpdateFailedGetVersion;
    return result;
  }

  // If write protection is not enabled, the RO firmware should
  // be updated first, as this allows for re-keying (dev->premp->mp)
  // and non-forward compatible changes.
  bool flashprotect_enabled;
  if (!ec_dev.IsFlashProtectEnabled(&flashprotect_enabled)) {
    result.status = UpdateStatus::kUpdateFailedFlashProtect;
    return result;
  }
  if (!flashprotect_enabled) {
    LOG(INFO) << "Flashprotect is disabled.";
    if (ecver.ro_version != fw_version.ro_version) {
      result.reason |= UpdateReason::kMismatchROVersion;
      attempted = true;
      LOG(INFO) << "FPMCU RO firmware mismatch, updating.";
      if (!UpdateImage(ec_dev, boot_ctrl, fw, EC_IMAGE_RO)) {
        result.status = UpdateStatus::kUpdateFailedRO;
        return result;
      }
    } else {
      LOG(INFO) << "FPMCU RO firmware is up to date.";
    }
  } else {
    LOG(INFO) << "FPMCU RO firmware is protected: no update.";
  }

  // The firmware should be updated if RO is active (i.e. RW is corrupted) or if
  // the firmware version available on the rootfs is different from the RW.
  bool active_image_ro = ecver.current_image != EC_IMAGE_RW;
  bool rw_mismatch = ecver.rw_version != fw_version.rw_version;
  if (active_image_ro) {
    result.reason |= UpdateReason::kActiveImageRO;
  }
  if (rw_mismatch) {
    result.reason |= UpdateReason::kMismatchRWVersion;
  }
  if (active_image_ro || rw_mismatch) {
    attempted = true;
    LOG(INFO)
        << "FPMCU RW firmware mismatch or failed RW boot detected, updating.";
    if (!UpdateImage(ec_dev, boot_ctrl, fw, EC_IMAGE_RW)) {
      result.status = UpdateStatus::kUpdateFailedRW;
      return result;
    }
  } else {
    LOG(INFO) << "FPMCU RW firmware is up to date.";
  }

  result.status = attempted ? UpdateStatus::kUpdateSucceeded
                            : UpdateStatus::kUpdateNotNecessary;
  return result;
}

}  // namespace updater

}  // namespace biod
