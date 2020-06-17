// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Library to provide access to the Chrome OS master configuration

#ifndef CHROMEOS_CONFIG_LIBCROS_CONFIG_IDENTITY_H_
#define CHROMEOS_CONFIG_LIBCROS_CONFIG_IDENTITY_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <brillo/brillo_export.h>

namespace base {
class FilePath;
class Value;
}  // namespace base

namespace brillo {

enum class SystemArchitecture {
  kX86,
  kArm,
  kUnknown,
};

class BRILLO_EXPORT CrosConfigIdentity {
 public:
  CrosConfigIdentity();
  virtual ~CrosConfigIdentity();

  // Get the current system architecture
  // @return brillo::SystemArchitecture
  static SystemArchitecture CurrentSystemArchitecture();

  // Get the system architecture from a string
  // @arch: string that is similar to the one provided by "uname -m"
  // @return brillo::SystemArchitecture
  static SystemArchitecture CurrentSystemArchitecture(const std::string& arch);

  // Factory method to produce a CrosConfigIdentity from a
  // SystemArchitecture
  // @return CrosConfigIdentity, or NULL if passed an unknown
  //     architecture
  static std::unique_ptr<CrosConfigIdentity> FromArchitecture(
      const SystemArchitecture& arch);

  // Reads the VPD identity information from the supplied VPD file.
  // @vpd_file: File containing the customization_id from VPD. Typically this
  //     is '/sys/firmware/vpd/ro/customization_id'.
  //     or '/sys/firmware/vpd/ro/whitelabel_tag'.
  bool ReadVpd(const base::FilePath& vpd_file);

  // Writes out a fake VPD file for the purposes of testing.
  // @customization_id: Whitelabel name to write
  // @vpd_file_out: Returns the '/sys/firmware/vpd/ro/customization_id'-style
  //     file that was written
  // @return true if OK, false on error
  bool FakeVpdFileForTesting(const std::string& customization_id,
                             base::FilePath* vpd_file_out);

  // Read the compatible devices list from the kernel or device-tree
  // compatible file.
  //
  // @product_name_file: File to read for product name or device-tree
  //     compatible info
  // @product_sku_file: File containing SKU ID
  // @return true if OK, false on error
  virtual bool ReadInfo(const base::FilePath& product_name_file,
                        const base::FilePath& product_sku_file) = 0;

  // Write out a fake product name (x86) or device-tree compatible
  // file (arm), as well as a sku-id file, for testing purposes.
  // @device_name: Name or device-tree compatible string to write to
  //     the compatible file (e.g., "Reef")
  // @sku_id: SKU ID number to write
  // @product_name_file_out: Returns the file that was written
  // @product_sku_file_out: File that the SKU ID integer was written into
  // @return true if OK, false on error
  virtual bool FakeProductFilesForTesting(
      const std::string& device_name,
      const int sku_id,
      base::FilePath* product_name_file_out,
      base::FilePath* product_sku_file_out) = 0;

  // Architecture-specific check that the identity specified in the
  // identity_dict is compatible with this identity
  // @identity_dict: a dictionary specified under /chromeos/configs
  // @return: true if the config is a possible identity, false otherwise
  virtual bool PlatformIdentityMatch(
      const base::Value& identity_dict) const = 0;

  // Get a string representation of the identity for logging purposes
  // @return A string representing the identity
  virtual std::string DebugString() const = 0;

  // @return SKU ID value
  int GetSkuId() const { return sku_id_; }

  // Initially, the SKU ID will be read from SMBIOS or FDT, but it can
  // be overridden using this method.
  void SetSkuId(const int sku_id) { sku_id_ = sku_id; }

  // Gets the VPD identitier value read via ReadVpd
  // @return VPD ID value
  const std::string& GetVpdId() const { return vpd_id_; }

 protected:
  int sku_id_;

 private:
  std::string vpd_id_;

  DISALLOW_COPY_AND_ASSIGN(CrosConfigIdentity);
};

}  // namespace brillo

#endif  // CHROMEOS_CONFIG_LIBCROS_CONFIG_IDENTITY_H_
