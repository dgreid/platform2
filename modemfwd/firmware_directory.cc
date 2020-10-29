// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/firmware_directory.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/macros.h>
#include <cros_config/cros_config.h>

#include "modemfwd/firmware_manifest.h"
#include "modemfwd/logging.h"

namespace modemfwd {

namespace {

const char kManifestName[] = "firmware_manifest.prototxt";

// Returns the modem firmware variant for the current model of the device by
// reading the /modem/firmware-variant property of the current model via
// chromeos-config. Returns an empty string if it fails to read the modem
// firmware variant from chromeos-config or no modem firmware variant is
// specified.
std::string GetModemFirmwareVariant() {
  brillo::CrosConfig config;
  if (!config.Init()) {
    LOG(WARNING) << "Failed to load Chrome OS configuration";
    return std::string();
  }

  std::string variant;
  if (!config.GetString("/modem", "firmware-variant", &variant)) {
    LOG(INFO) << "No modem firmware variant is specified";
    return std::string();
  }

  LOG(INFO) << "Use modem firmware variant: " << variant;
  return variant;
}

}  // namespace

const char FirmwareDirectory::kGenericCarrierId[] = "generic";

class FirmwareDirectoryImpl : public FirmwareDirectory {
 public:
  FirmwareDirectoryImpl(FirmwareIndex index, const base::FilePath& directory)
      : index_(std::move(index)),
        directory_(directory),
        variant_(GetModemFirmwareVariant()) {}

  // modemfwd::FirmwareDirectory overrides.
  FirmwareDirectory::Files FindFirmware(const std::string& device_id,
                                        std::string* carrier_id) override {
    FirmwareDirectory::Files result;

    DeviceType type{device_id, variant_};
    auto device_it = index_.find(type);
    if (device_it == index_.end()) {
      ELOG(INFO) << "Firmware directory has no firmware for device ID ["
                 << device_id << "]";
      return result;
    }

    const DeviceFirmwareCache& cache = device_it->second;
    FirmwareFileInfo info;

    // Null carrier ID -> just go for generic main firmware.
    if (!carrier_id) {
      if (FindSpecificFirmware(cache.main_firmware, kGenericCarrierId, &info))
        result.main_firmware = info;
      return result;
    }

    // Searching for carrier firmware may change the carrier to generic. This
    // is fine, and the main firmware should use the same one in that case.
    if (FindFirmwareForCarrier(cache.carrier_firmware, carrier_id, &info))
      result.carrier_firmware = info;
    if (FindFirmwareForCarrier(cache.main_firmware, carrier_id, &info))
      result.main_firmware = info;

    return result;
  }

  // modemfwd::IsUsingSameFirmware overrides.
  bool IsUsingSameFirmware(const std::string& device_id,
                           const std::string& carrier_a,
                           const std::string& carrier_b) override {
    // easy case: identical carrier UUID
    if (carrier_a == carrier_b)
      return true;

    DeviceType type{device_id, variant_};
    auto device_it = index_.find(type);
    // no firmware for this device
    if (device_it == index_.end())
      return true;

    const DeviceFirmwareCache& cache = device_it->second;
    auto main_a = cache.main_firmware.find(carrier_a);
    auto main_b = cache.main_firmware.find(carrier_b);
    auto cust_a = cache.carrier_firmware.find(carrier_a);
    auto cust_b = cache.carrier_firmware.find(carrier_b);
    // one or several firmwares are missing
    if (main_a == cache.main_firmware.end() ||
        main_b == cache.main_firmware.end() ||
        cust_a == cache.carrier_firmware.end() ||
        cust_b == cache.carrier_firmware.end())
      return main_a == main_b && cust_a == cust_b;
    // same firmware if they are pointing to the 2 same files.
    return main_a->second == main_b->second && cust_a->second == cust_b->second;
  }

 private:
  bool FindFirmwareForCarrier(
      const DeviceFirmwareCache::CarrierIndex& carrier_index,
      std::string* carrier_id,
      FirmwareFileInfo* out_info) {
    if (FindSpecificFirmware(carrier_index, *carrier_id, out_info))
      return true;

    if (FindSpecificFirmware(carrier_index, kGenericCarrierId, out_info)) {
      *carrier_id = kGenericCarrierId;
      return true;
    }

    return false;
  }

  bool FindSpecificFirmware(
      const DeviceFirmwareCache::CarrierIndex& carrier_index,
      const std::string& carrier_id,
      FirmwareFileInfo* out_info) {
    auto it = carrier_index.find(carrier_id);
    if (it == carrier_index.end())
      return false;

    *out_info = *it->second;
    return true;
  }

  FirmwareIndex index_;
  base::FilePath directory_;
  std::string variant_;

  DISALLOW_COPY_AND_ASSIGN(FirmwareDirectoryImpl);
};

std::unique_ptr<FirmwareDirectory> CreateFirmwareDirectory(
    const base::FilePath& directory) {
  FirmwareIndex index;
  if (!ParseFirmwareManifestV2(directory.Append(kManifestName), &index)) {
    LOG(INFO) << "Firmware manifest did not parse as V2, falling back to V1";
    if (!ParseFirmwareManifest(directory.Append(kManifestName), &index))
      return nullptr;
  }

  return std::make_unique<FirmwareDirectoryImpl>(std::move(index), directory);
}

}  // namespace modemfwd
