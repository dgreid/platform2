// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_UPDATER_H_
#define BIOD_CROS_FP_UPDATER_H_

#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <brillo/enum_flags.h>
#include <chromeos/ec/ec_commands.h>
#include <cros_config/cros_config_interface.h>

#include "biod/cros_fp_device.h"
#include "biod/cros_fp_firmware.h"
#include "biod/update_reason.h"
#include "biod/update_status.h"

namespace biod {

extern const char kCrosConfigFPPath[];
extern const char kCrosConfigFPBoard[];
extern const char kCrosConfigFPLocation[];

// These utilities should be absorbed by CrosFpDevice.
// This is a temporary holding place until they can be absorbed.
class CrosFpDeviceUpdate {
 public:
  virtual ~CrosFpDeviceUpdate() = default;
  virtual bool GetVersion(biod::CrosFpDevice::EcVersion* ecver) const;
  virtual bool IsFlashProtectEnabled(bool* status) const;
  virtual bool Flash(const CrosFpFirmware& fw,
                     enum ec_current_image image) const;
  static std::string EcCurrentImageToString(enum ec_current_image image);
};

// CrosFpBootUpdateCtrl holds the interfaces for the
// external boot-time environment.
class CrosFpBootUpdateCtrl {
 public:
  virtual ~CrosFpBootUpdateCtrl() = default;
  virtual bool TriggerBootUpdateSplash() const;
  virtual bool ScheduleReboot() const;
};

namespace updater {

extern const char kFirmwareDir[];

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

// Checks for external firmware disable mechanism.
bool UpdateDisallowed();
bool FingerprintUnsupported(brillo::CrosConfigInterface* cros_config);

struct UpdateResult {
  UpdateStatus status;
  UpdateReason reason;
};

UpdateResult DoUpdate(const CrosFpDeviceUpdate& ec_dev,
                      const CrosFpBootUpdateCtrl& boot_ctrl,
                      const CrosFpFirmware& fw);

}  // namespace updater

}  // namespace biod

#endif  // BIOD_CROS_FP_UPDATER_H_
