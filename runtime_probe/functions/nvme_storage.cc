// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/nvme_storage.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/strings/string_utils.h>
#include <pcrecpp.h>

#include "runtime_probe/utils/file_utils.h"
#include "runtime_probe/utils/value_utils.h"

namespace runtime_probe {
namespace {
// Storage-speicific fields to probe for NVMe.
const std::vector<std::string> kNvmeFields{"vendor", "device", "class"};
constexpr auto kNvmeType = "NVMe";
constexpr auto kNvmePrefix = "pci_";

}  // namespace

// TODO(hmchu): Consider falling back to Smartctl if this fails.
std::string NvmeStorageFunction::GetStorageFwVersion(
    const base::FilePath& node_path) const {
  std::string fw_ver_res;
  VLOG(2) << "Checking NVMe firmware version of "
          << node_path.BaseName().value();
  if (!base::ReadFileToString(node_path.Append("device").Append("firmware_rev"),
                              &fw_ver_res)) {
    VLOG(2) << "Failed to read NVMe firmware version from sysfs.";
    return std::string{""};
  }
  return base::TrimWhitespaceASCII(fw_ver_res, base::TrimPositions::TRIM_ALL)
      .as_string();
}

bool NvmeStorageFunction::CheckStorageTypeMatch(
    const base::FilePath& node_path) const {
  VLOG(2) << "Checking if \"" << node_path.value() << "\" is NVMe.";
  base::FilePath driver_symlink_target;
  const auto nvme_driver_path =
      node_path.Append("device").Append("device").Append("driver");
  if (!base::ReadSymbolicLink(nvme_driver_path, &driver_symlink_target)) {
    VLOG(1) << "\"" << nvme_driver_path.value() << "\" is not a symbolic link";
    VLOG(2) << "\"" << node_path.value() << "\" is not NVMe.";
    return false;
  }
  pcrecpp::RE nvme_driver_re(R"(drivers/nvme)", pcrecpp::RE_Options());
  if (!nvme_driver_re.PartialMatch(driver_symlink_target.value())) {
    return false;
  }

  VLOG(2) << "\"" << node_path.value() << "\" is NVMe.";
  return true;
}

base::Optional<base::Value> NvmeStorageFunction::EvalInHelperByPath(
    const base::FilePath& node_path) const {
  VLOG(2) << "Processnig the node \"" << node_path.value() << "\"";

  if (!CheckStorageTypeMatch(node_path))
    return base::nullopt;

  // For NVMe device, "<node_path>/device/device/.." is exactly where we want to
  // look at.
  const auto nvme_path = node_path.Append("device").Append("device");

  if (!base::PathExists(nvme_path)) {
    VLOG(1) << "NVMe-speific path does not exist on storage device \""
            << node_path.value() << "\"";
    return base::nullopt;
  }

  auto nvme_res = MapFilesToDict(nvme_path, kNvmeFields, {});

  if (!nvme_res) {
    VLOG(1) << "Cannot find NVMe-specific fields on storage \""
            << node_path.value() << "\"";
    return base::nullopt;
  }
  PrependToDVKey(&*nvme_res, kNvmePrefix);
  nvme_res->SetStringKey("type", kNvmeType);
  const std::string storage_fw_version = GetStorageFwVersion(node_path);
  if (!storage_fw_version.empty())
    nvme_res->SetStringKey("storage_fw_version", storage_fw_version);
  return nvme_res;
}

}  // namespace runtime_probe
