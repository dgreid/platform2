// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_UPDATER_UPDATE_UTILS_H_
#define BIOD_UPDATER_UPDATE_UTILS_H_

#include <string>

#include <base/files/file_path.h>
#include <cros_config/cros_config_interface.h>

namespace biod {
namespace updater {

extern const char kFirmwareDir[];

std::string UpdaterVersion();

// Checks for external firmware disable mechanism.
bool UpdateDisallowed();

enum class FindFirmwareFileStatus {
  kFoundFile,
  kNoDirectory,
  kFileNotFound,
  kMultipleFiles,
};

// Searches for the externally packaged firmware binary using a glob.
// The returned firmware has not been validated.
FindFirmwareFileStatus FindFirmwareFile(
    const base::FilePath& directory,
    brillo::CrosConfigInterface* cros_config,
    base::FilePath* file);
std::string FindFirmwareFileStatusToString(FindFirmwareFileStatus status);

}  // namespace updater
}  // namespace biod

#endif  // BIOD_UPDATER_UPDATE_UTILS_H_
