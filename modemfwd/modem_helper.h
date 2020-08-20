// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MODEM_HELPER_H_
#define MODEMFWD_MODEM_HELPER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>

namespace modemfwd {

struct FirmwareInfo {
  FirmwareInfo() = default;
  FirmwareInfo(const std::string& main_version,
               const std::string& carrier_uuid,
               const std::string& carrier_version)
      : main_version(main_version),
        carrier_uuid(carrier_uuid),
        carrier_version(carrier_version) {}

  std::string main_version;
  std::string carrier_uuid;
  std::string carrier_version;
};

struct HelperInfo {
  explicit HelperInfo(const base::FilePath& executable_path)
      : executable_path(executable_path) {}

  base::FilePath executable_path;
  std::vector<std::string> extra_arguments;
};

class ModemHelper {
 public:
  virtual ~ModemHelper() = default;

  virtual bool GetFirmwareInfo(FirmwareInfo* out_info) = 0;

  virtual bool FlashMainFirmware(const base::FilePath& path_to_fw,
                                 const std::string& version) = 0;
  virtual bool FlashCarrierFirmware(const base::FilePath& path_to_fw,
                                    const std::string& version) = 0;

  virtual bool Reboot() = 0;
  virtual bool FlashModeCheck() = 0;
  virtual bool ClearAttachAPN(const std::string& carrier_uuid) = 0;
};

std::unique_ptr<ModemHelper> CreateModemHelper(const HelperInfo& helper_info);

}  // namespace modemfwd

#endif  // MODEMFWD_MODEM_HELPER_H_
