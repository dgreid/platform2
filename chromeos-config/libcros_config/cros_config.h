// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Library to provide access to the Chrome OS master configuration

#ifndef CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_H_
#define CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <brillo/brillo_export.h>
#include "chromeos-config/libcros_config/cros_config_interface.h"
#include "chromeos-config/libcros_config/identity.h"

namespace base {
class FilePath;
}  // namespace base

namespace brillo {

static const int kDefaultSkuId = -1;

class BRILLO_EXPORT CrosConfig : public CrosConfigInterface {
 public:
  CrosConfig();
  CrosConfig(const CrosConfig&) = delete;
  CrosConfig& operator=(const CrosConfig&) = delete;

  ~CrosConfig() override;

  // Prepare the configuration system for access to the configuration for
  // the model this is running on. This reads the configuration file into
  // memory.
  // @return true if OK, false on error.
  bool Init();

  // Prepare the configuration system for testing.
  // This reads in the given configuration file and selects the config
  // based on the supplied identifiers.
  // @sku_id: SKU ID.
  // @json_path: Path to configuration file.
  // @arch: brillo::SystemArchitecture (kX86 or kArm).
  // @name: Platform name.
  // @customization_id: VPD customization ID
  // @return true if OK, false on error.
  bool InitForTest(const int sku_id,
                   const base::FilePath& json_path,
                   const SystemArchitecture arch,
                   const std::string& name,
                   const std::string& customization_id);

  // Mount a ConfigFS image. This method can be called before or
  // instead of Init, and the optimized identity file inside of the
  // ConfigFS image will be used for initialization instead of the
  // default JSON file.
  // @image_path: The path to the ConfigFS image.
  // @mount_path: The directory to mount ConfigFS at.
  // @return true on success, false on error.
  bool MountConfigFS(const base::FilePath& image_path,
                     const base::FilePath& mount_path);

  // Mount a ConfigFS image using the legacy (non-unibuild)
  // interface. This method can be called before (but not instead of)
  // Init, as no runtime probing is needed on non-unibuild boards.
  // @mount_path: The directory to mount ConfigFS at.
  // @return true on success, false on error.
  bool MountFallbackConfigFS(const base::FilePath& mount_path);

  // Undo a MountConfigFS or MountFallbackConfigFS by unmounting all
  // associated filesystems. Uses MNT_DETACH (equivalent to umount
  // --lazy) to prevent blocking if another process has left open a
  // file descriptor.
  // @mount_path: The directory which ChromeOS ConfigFS is mounted at.
  // @return true on success, false on error.
  bool Unmount(const base::FilePath& mount_path) const;

  // CrosConfigInterface:
  bool GetString(const std::string& path,
                 const std::string& property,
                 std::string* val_out) override;

  bool GetDeviceIndex(int* device_index_out) override;

 private:
  // Get the default identity files for the specified architecture.
  // @arch: The current (or tested) system architecture.
  // @vpd_file_out: Output file path for whitelabel tag or
  //     customization id.
  // @product_name_file_out: Output file path for SMBIOS name or
  //     dt-compatible file.
  // @product_sku_file_out: Output file path for SKU ID file.
  bool GetDefaultIdentityFiles(const SystemArchitecture arch,
                               base::FilePath* vpd_file_out,
                               base::FilePath* product_name_file_out,
                               base::FilePath* product_sku_file_out);

  // Internal init function used by Init and InitForTest.
  // @sku_id: SKU ID.
  // @json_path: Path to configuration file.
  // @arch: brillo::SystemArchitecture (kX86 or kArm).
  // @product_name_file: The file to read product name, or device-tree
  //     compatible name, from
  // @product_sku_file: The file to read sku id from
  // @vpd_file: The file to read VPD customization ID from
  // @return true if OK, false on error.
  bool InitInternal(const int sku_id,
                    const base::FilePath& json_path,
                    const SystemArchitecture arch,
                    const base::FilePath& product_name_file,
                    const base::FilePath& product_sku_file,
                    const base::FilePath& vpd_file);

  // When InitForTest is is called, the underlying CrosConfigJson that
  // is used.
  std::unique_ptr<CrosConfigInterface> cros_config_;
};

}  // namespace brillo

#endif  // CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_H_
