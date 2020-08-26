// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/update_utils.h"

#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/optional.h>
#include <cros_config/cros_config_interface.h>

#include "biod/biod_config.h"
#include "biod/biod_version.h"

namespace {

constexpr char kFirmwareGlobSuffix[] = "_*.bin";
constexpr char kFirmwareLegacyBoardPattern[] = "*_fp";
constexpr char kUpdateDisableFile[] = "/opt/google/biod/fw/.disable_fp_updater";

}  // namespace

namespace biod {
namespace updater {

constexpr char kFirmwareDir[] = "/opt/google/biod/fw";

std::string UpdaterVersion() {
  const char* ver = VCSID;
  static_assert(sizeof(ver) > 0,
                "The updater requires VCSID for to work properly.");
  return std::string(ver);
}

bool UpdateDisallowed() {
  return base::PathExists(base::FilePath(kUpdateDisableFile));
}

// FindFirmwareFile searches |directory| for a single firmware file
// that matches the |board_name|+|kFirmwareGlobSuffix| file pattern.
// If a single matching firmware file is found is found,
// its path is written to |file|. Otherwise, |file| will be untouched.
static FindFirmwareFileStatus FindFirmwareFile(const base::FilePath& directory,
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
  base::Optional<std::string> board_name = biod::FingerprintBoard(cros_config);
  if (board_name) {
    LOG(INFO) << "Identified fingerprint board name as '" << *board_name
              << "'.";
  } else {
    LOG(WARNING) << "Fingerprint board name is unavailable, continuing with "
                    "legacy update.";
    board_name = kFirmwareLegacyBoardPattern;
  }

  return FindFirmwareFile(directory, *board_name, file);
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

}  // namespace updater
}  // namespace biod
