// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/ata_storage.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/strings/string_utils.h>

#include "runtime_probe/utils/file_utils.h"
#include "runtime_probe/utils/value_utils.h"

namespace runtime_probe {
namespace {
// Storage-speicific fields to probe for SATA.
const std::vector<std::string> kAtaFields{"vendor", "model"};
constexpr auto kAtaType = "ATA";
constexpr auto kAtaPrefix = "ata_";

}  // namespace

// TODO(b/134981078): Get storage fw version by D-Bus call to debugd for
// smartctl.
std::string AtaStorageFunction::GetStorageFwVersion(
    const base::FilePath& node_path) const {
  return "";
}

bool AtaStorageFunction::CheckStorageTypeMatch(
    const base::FilePath& node_path) const {
  VLOG(2) << "Checking if \"" << node_path.value() << "\" is SATA.";
  if (node_path.empty())
    return false;
  const auto vendor_path = node_path.Append("device").Append("vendor");

  std::string vendor_in_sysfs;
  if (!base::ReadFileToString(vendor_path, &vendor_in_sysfs) ||
      base::TrimWhitespaceASCII(vendor_in_sysfs,
                                base::TrimPositions::TRIM_ALL) != kAtaType) {
    VLOG(2) << "\"" << node_path.value() << "\" is not SATA.";
    return false;
  }
  VLOG(2) << "Vendor exposed in sysfs is \"" << vendor_in_sysfs << "\"";
  VLOG(2) << "\"" << node_path.value() << "\" is SATA.";
  return true;
}

base::Optional<base::Value> AtaStorageFunction::EvalInHelperByPath(
    const base::FilePath& node_path) const {
  VLOG(2) << "Processnig the node \"" << node_path.value() << "\"";

  if (!CheckStorageTypeMatch(node_path))
    return base::nullopt;

  const auto ata_path = node_path.Append("device");

  if (!base::PathExists(ata_path)) {
    VLOG(1) << "ATA-specific path does not exist on storage device \""
            << node_path.value() << "\"";
    return base::nullopt;
  }

  auto ata_res = MapFilesToDict(ata_path, kAtaFields, {});

  if (!ata_res) {
    VLOG(1) << "ATA-specific fields do not exist on storage \""
            << node_path.value() << "\"";
    return base::nullopt;
  }
  PrependToDVKey(&*ata_res, kAtaPrefix);
  ata_res->SetStringKey("type", kAtaType);
  const std::string storage_fw_version = GetStorageFwVersion(node_path);
  if (!storage_fw_version.empty())
    ata_res->SetStringKey("storage_fw_version", storage_fw_version);
  return ata_res;
}

}  // namespace runtime_probe
