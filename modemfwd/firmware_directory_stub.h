// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_FIRMWARE_DIRECTORY_STUB_H_
#define MODEMFWD_FIRMWARE_DIRECTORY_STUB_H_

#include <map>
#include <string>
#include <utility>

#include <base/macros.h>

#include "modemfwd/firmware_directory.h"

namespace modemfwd {

class FirmwareDirectoryStub : public FirmwareDirectory {
 public:
  FirmwareDirectoryStub() = default;
  FirmwareDirectoryStub(const FirmwareDirectoryStub&) = delete;
  FirmwareDirectoryStub& operator=(const FirmwareDirectoryStub&) = delete;

  void AddMainFirmware(const std::string& device_id, FirmwareFileInfo info);
  void AddMainFirmwareForCarrier(const std::string& device_id,
                                 const std::string& carrier_id,
                                 FirmwareFileInfo info);
  void AddCarrierFirmware(const std::string& device_id,
                          const std::string& carrier_id,
                          FirmwareFileInfo info);

  // modemfwd::FirmwareDirectory overrides.
  FirmwareDirectory::Files FindFirmware(const std::string& device_id,
                                        std::string* carrier_id) override;
  // modemfwd::IsUsingSameFirmware overrides.
  bool IsUsingSameFirmware(const std::string& device_id,
                           const std::string& carrier_a,
                           const std::string& carrier_b) override;

 private:
  using CarrierFirmwareMap =
      std::map<std::pair<std::string, std::string>, FirmwareFileInfo>;

  bool FindCarrierFirmware(const std::string& device_id,
                           std::string* carrier_id,
                           FirmwareFileInfo* out_info);

  std::map<std::string, FirmwareFileInfo> main_fw_info_;
  CarrierFirmwareMap main_fw_info_for_carrier_;
  CarrierFirmwareMap carrier_fw_info_;
};

}  // namespace modemfwd

#endif  // MODEMFWD_FIRMWARE_DIRECTORY_STUB_H_
