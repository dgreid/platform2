// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/firmware_manifest.h"

#include <utility>

#include <base/strings/string_number_conversions.h>
#include <brillo/proto_file_io.h>

#include "modemfwd/firmware_directory.h"

#include "modemfwd/proto_bindings/firmware_manifest.pb.h"

namespace modemfwd {

namespace {

struct DeviceEntries {
  const MainFirmware* main_firmware;
  std::vector<const CarrierFirmware*> carrier_firmware;
};

bool SortByDevice(const FirmwareManifest& manifest,
                  std::map<DeviceType, DeviceEntries>* out_sorted) {
  for (const MainFirmware& info : manifest.main_firmware()) {
    if (info.device_id().empty() || info.filename().empty() ||
        info.version().empty() || !Compression_IsValid(info.compression())) {
      LOG(ERROR) << "Found malformed main firmware manifest entry";
      return false;
    }

    DeviceType type{info.device_id(), info.variant()};
    if ((*out_sorted)[type].main_firmware) {
      LOG(ERROR) << "Device " << type.device_id()
                 << (type.variant().empty() ? ""
                                            : " for variant " + type.variant())
                 << " has multiple main firmwares";
      return false;
    }

    (*out_sorted)[type].main_firmware = &info;
  }

  for (const CarrierFirmware& info : manifest.carrier_firmware()) {
    if (info.device_id().empty() || info.filename().empty() ||
        info.version().empty() || info.carrier_id().empty() ||
        !Compression_IsValid(info.compression())) {
      LOG(ERROR) << "Found malformed carrier firmware manifest entry";
      return false;
    }

    DeviceType type{info.device_id(), info.variant()};
    (*out_sorted)[type].carrier_firmware.push_back(&info);
  }

  return true;
}

bool ConstructCache(const DeviceEntries& entries,
                    const base::FilePath& directory_path,
                    DeviceFirmwareCache* out_cache) {
  std::unique_ptr<FirmwareFileInfo> main_info;
  if (entries.main_firmware) {
    // Create the firmware file info for the main firmware.
    auto compression =
        ToFirmwareFileInfoCompression(entries.main_firmware->compression());
    if (!compression.has_value())
      return false;

    main_info = std::make_unique<FirmwareFileInfo>(
        directory_path.Append(entries.main_firmware->filename()),
        entries.main_firmware->version(), compression.value());
  }

  DeviceFirmwareCache::CarrierIndex* index = &out_cache->carrier_firmware;
  for (const CarrierFirmware* carrier_firmware : entries.carrier_firmware) {
    // Convert the manifest entry into a FirmwareFileInfo.
    auto compression =
        ToFirmwareFileInfoCompression(carrier_firmware->compression());
    if (!compression.has_value())
      return false;

    auto carrier_info = std::make_unique<FirmwareFileInfo>(
        directory_path.Append(carrier_firmware->filename()),
        carrier_firmware->version(), compression.value());

    // Add the carrier (and if applicable, main) firmware to the cache under
    // the carrier ID for this entry.
    for (const std::string& supported_carrier :
         carrier_firmware->carrier_id()) {
      if (index->count(supported_carrier) > 0) {
        LOG(ERROR) << "Duplicate carrier firmware entry for carrier "
                   << supported_carrier;
        // It's possible that we've left a dangling pointer to carrier_info
        // in another carrier's mapping, so we should clear the index here in
        // case a user does the wrong thing and uses the invalid result.
        index->clear();
        return false;
      }

      index->emplace(supported_carrier, carrier_info.get());
      if (main_info)
        out_cache->main_firmware.emplace(supported_carrier, main_info.get());
    }
    out_cache->all_files.push_back(std::move(carrier_info));
  }

  // Add the main FW for generic carriers if we didn't already do that, and
  // ensure that we put the main firmware in the cache's list.
  if (main_info) {
    if (index->count(FirmwareDirectory::kGenericCarrierId) == 0)
      index->emplace(FirmwareDirectory::kGenericCarrierId, main_info.get());
    out_cache->all_files.push_back(std::move(main_info));
  }

  return true;
}

}  // namespace

base::Optional<FirmwareFileInfo::Compression> ToFirmwareFileInfoCompression(
    Compression compression) {
  switch (compression) {
    case Compression::NONE:
      return FirmwareFileInfo::Compression::NONE;
    case Compression::XZ:
      return FirmwareFileInfo::Compression::XZ;
    default:
      std::string name = Compression_Name(compression);
      if (name.empty())
        name = base::NumberToString(compression);
      LOG(ERROR) << "Unsupported compression: " << name;
      return base::nullopt;
  }
}

bool ParseFirmwareManifest(const base::FilePath& manifest,
                           FirmwareIndex* index) {
  FirmwareManifest manifest_proto;
  if (!brillo::ReadTextProtobuf(manifest, &manifest_proto))
    return false;

  base::FilePath directory = manifest.DirName();
  std::map<DeviceType, DeviceEntries> sorted;

  if (!SortByDevice(manifest_proto, &sorted))
    return false;

  for (const auto& device : sorted) {
    DeviceFirmwareCache cache;
    if (!ConstructCache(device.second, directory, &cache))
      return false;

    (*index)[device.first] = std::move(cache);
  }

  return true;
}

}  // namespace modemfwd
