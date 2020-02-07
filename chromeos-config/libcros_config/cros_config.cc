// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Library to provide access to the Chrome OS master configuration

#include "chromeos-config/libcros_config/cros_config.h"

#include <string>
#include <sys/mount.h>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include "chromeos-config/libcros_config/configfs.h"
#include "chromeos-config/libcros_config/cros_config_fallback.h"
#include "chromeos-config/libcros_config/cros_config_json.h"
#include "chromeos-config/libcros_config/identity.h"

namespace {
const char kCustomizationId[] = "/sys/firmware/vpd/ro/customization_id";
const char kWhitelabelTag[] = "/sys/firmware/vpd/ro/whitelabel_tag";
const char kProductName[] = "/sys/devices/virtual/dmi/id/product_name";
const char kProductSku[] = "/sys/devices/virtual/dmi/id/product_sku";
const char kArmSkuId[] = "/proc/device-tree/firmware/coreboot/sku-id";
const char kDeviceTreeCompatiblePath[] = "/proc/device-tree/compatible";
const char kConfigJsonPath[] = "/usr/share/chromeos-config/config.json";
}  // namespace

namespace brillo {

CrosConfig::CrosConfig() {}

CrosConfig::~CrosConfig() {}

bool CrosConfig::InitCheck() const {
  if (!cros_config_) {
    CROS_CONFIG_LOG(ERROR)
        << "Init*() must be called before accessing configuration";
    return false;
  }
  return true;
}

bool CrosConfig::GetDefaultIdentityFiles(const SystemArchitecture arch,
                                         base::FilePath* vpd_file_out,
                                         base::FilePath* product_name_file_out,
                                         base::FilePath* product_sku_file_out) {
  *vpd_file_out = base::FilePath(kWhitelabelTag);
  if (!base::PathExists(*vpd_file_out)) {
    *vpd_file_out = base::FilePath(kCustomizationId);
  }
  if (arch == SystemArchitecture::kX86) {
    *product_name_file_out = base::FilePath(kProductName);
    *product_sku_file_out = base::FilePath(kProductSku);
  } else if (arch == SystemArchitecture::kArm) {
    *product_name_file_out = base::FilePath(kDeviceTreeCompatiblePath);
    *product_sku_file_out = base::FilePath(kArmSkuId);
  } else {
    CROS_CONFIG_LOG(ERROR) << "System architecture is unknown";
    return false;
  }
  return true;
}

bool CrosConfig::Init(const int sku_id) {
  base::FilePath vpd_file;
  base::FilePath product_name_file;
  base::FilePath product_sku_file;
  const auto arch = CrosConfigIdentity::CurrentSystemArchitecture();
  if (!GetDefaultIdentityFiles(arch, &vpd_file, &product_name_file,
                               &product_sku_file)) {
    return false;
  }
  base::FilePath json_path(kConfigJsonPath);
  return InitInternal(sku_id, json_path, arch, product_name_file,
                      product_sku_file, vpd_file);
}

bool CrosConfig::InitForTest(const int sku_id,
                             const base::FilePath& json_path,
                             const SystemArchitecture arch,
                             const std::string& name,
                             const std::string& customization_id) {
  auto identity = CrosConfigIdentity::FromArchitecture(arch);
  if (!identity) {
    CROS_CONFIG_LOG(ERROR) << "Provided architecture is unknown";
    return false;
  }
  base::FilePath product_name_file, product_sku_file, vpd_file;
  if (!identity->FakeVpdFileForTesting(customization_id, &vpd_file)) {
    CROS_CONFIG_LOG(ERROR) << "FakeVpdFileForTesting() failed";
    return false;
  }
  if (!identity->FakeProductFilesForTesting(name, sku_id, &product_name_file,
                                            &product_sku_file)) {
    CROS_CONFIG_LOG(ERROR) << "FakeProductFilesForTesting() failed";
    return false;
  }
  return InitInternal(sku_id, json_path, arch, product_name_file,
                      product_sku_file, vpd_file);
}

bool CrosConfig::MountConfigFS(const base::FilePath& image_path,
                               const base::FilePath& mount_path) {
  base::FilePath private_dir;
  base::FilePath v1_dir;

  if (!SetupMountPath(mount_path, &private_dir, &v1_dir)) {
    return false;
  }

  base::FilePath loop_device;
  if (!SetupLoopDevice(image_path, &loop_device)) {
    return false;
  }

  if (!Mount(loop_device, private_dir, kConfigFSPrivateFSType, MS_RDONLY)) {
    return false;
  }

  const auto private_v1_dir = private_dir.Append(kConfigFSV1DirName);

  if (!cros_config_) {
    // Init hasn't been called yet (which is the typical case of using
    // MountConfigFS).  We can use the identity stored inside of the
    // ConfigFS for faster initialization.
    const auto identity_path = private_v1_dir.Append(kConfigFSIdentityName);

    if (!base::PathExists(identity_path)) {
      // We have checks at build-time in cros_config_host that makes
      // sure this file exists in the filesystem.  But it's worth the
      // sanity check here too (in case a developer-constructed image
      // was invalid).
      CROS_CONFIG_LOG(ERROR) << identity_path.value() << " is missing!";
      return false;
    }
    const auto arch = CrosConfigIdentity::CurrentSystemArchitecture();
    base::FilePath vpd_file;
    base::FilePath product_name_file;
    base::FilePath product_sku_file;
    if (!GetDefaultIdentityFiles(arch, &vpd_file, &product_name_file,
                                 &product_sku_file)) {
      return false;
    }
    if (!InitInternal(kDefaultSkuId, identity_path, arch, product_name_file,
                      product_sku_file, vpd_file)) {
      CROS_CONFIG_LOG(ERROR) << "Identity probing failed!";
      return false;
    }
  }

  int device_index;
  if (!GetDeviceIndex(&device_index)) {
    return false;
  }
  const auto device_config_dir =
      private_v1_dir.Append(CrosConfigJson::kRootName)
          .Append(CrosConfigJson::kConfigListName)
          .Append(std::to_string(device_index));
  return Bind(device_config_dir, v1_dir);
}

bool CrosConfig::MountFallbackConfigFS(const base::FilePath& mount_path) {
  base::FilePath private_dir;
  base::FilePath v1_dir;

  if (!SetupMountPath(mount_path, &private_dir, &v1_dir)) {
    return false;
  }

  if (!Mount(base::FilePath("tmpfs"), private_dir, "tmpfs", 0)) {
    return false;
  }

  const auto fallback_dir = private_dir.Append("fallback");
  if (!base::CreateDirectory(fallback_dir)) {
    CROS_CONFIG_LOG(ERROR) << "Failed to create directory "
                           << fallback_dir.value();
    return false;
  }

  CrosConfigFallback cros_config;
  if (!cros_config.WriteConfigFS(fallback_dir)) {
    CROS_CONFIG_LOG(ERROR) << "Creating fallback ConfigFS failed!";
    return false;
  }

  if (!Remount(private_dir, MS_RDONLY)) {
    CROS_CONFIG_LOG(ERROR) << "Unable to make fallback ConfigFS read-only "
                           << "after writing out files.";
    return false;
  }

  return Bind(fallback_dir, v1_dir);
}

bool CrosConfig::Unmount(const base::FilePath& mount_path) const {
  bool success = true;
  for (const auto& dir : {kConfigFSV1DirName, kConfigFSPrivateDirName}) {
    const auto mountpoint = mount_path.Append(dir).value();
    if (umount2(mountpoint.c_str(), MNT_DETACH) < 0) {
      success = false;
      CROS_CONFIG_LOG(ERROR) << "Failed to unmount " << mountpoint << ": "
                             << logging::SystemErrorCodeToString(
                                    logging::GetLastSystemErrorCode());
    }
  }
  return success;
}

bool CrosConfig::InitInternal(const int sku_id,
                              const base::FilePath& json_path,
                              const SystemArchitecture arch,
                              const base::FilePath& product_name_file,
                              const base::FilePath& product_sku_file,
                              const base::FilePath& vpd_file) {
  if (!base::PathExists(json_path)) {
    // Fallback to mosys platform on non-unibuild systems
    cros_config_ = std::make_unique<CrosConfigFallback>();
    fallback_mode_ = true;
    return true;
  }

  auto cros_config_json = std::make_unique<CrosConfigJson>();
  CROS_CONFIG_LOG(INFO) << ">>>>> reading config file: path="
                        << json_path.MaybeAsASCII();
  if (!cros_config_json->ReadConfigFile(json_path))
    return false;
  CROS_CONFIG_LOG(INFO) << ">>>>> config file successfully read";

  CROS_CONFIG_LOG(INFO) << ">>>>> Starting to read identity";
  auto identity = CrosConfigIdentity::FromArchitecture(arch);
  if (!identity->ReadVpd(vpd_file)) {
    CROS_CONFIG_LOG(ERROR) << "Cannot read VPD identity";
    return false;
  }
  if (!identity->ReadInfo(product_name_file, product_sku_file)) {
    CROS_CONFIG_LOG(ERROR) << "Cannot read SMBIOS or dt-compatible info";
    return false;
  }
  if (sku_id != kDefaultSkuId) {
    identity->SetSkuId(sku_id);
    CROS_CONFIG_LOG(INFO) << "Set sku_id to explicitly assigned value "
                          << sku_id;
  }
  if (!cros_config_json->SelectConfigByIdentity(*identity)) {
    CROS_CONFIG_LOG(ERROR) << "Cannot find config for "
                           << identity->DebugString() << " (VPD ID from "
                           << vpd_file.MaybeAsASCII() << ")";
    return false;
  }
  CROS_CONFIG_LOG(INFO) << ">>>>> Completed initialization";

  // Downgrade CrosConfigJson to CrosConfigInterface now that
  // initialization has finished
  cros_config_ = std::move(cros_config_json);
  return true;
}

bool CrosConfig::GetString(const std::string& path,
                           const std::string& property,
                           std::string* val_out) {
  if (!InitCheck()) {
    return false;
  }
  return cros_config_->GetString(path, property, val_out);
}

bool CrosConfig::GetDeviceIndex(int* device_index_out) {
  if (!InitCheck()) {
    return false;
  }
  return cros_config_->GetDeviceIndex(device_index_out);
}

}  // namespace brillo
