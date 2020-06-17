// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Look up identity information for the current device
// Also provide a way to fake identity for testing.

#include "chromeos-config/libcros_config/identity_x86.h"

#include <cstdio>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include "chromeos-config/libcros_config/cros_config_interface.h"

namespace brillo {

CrosConfigIdentityX86::CrosConfigIdentityX86() {}

CrosConfigIdentityX86::~CrosConfigIdentityX86() {}

bool CrosConfigIdentityX86::FakeProductFilesForTesting(
    const std::string& name,
    const int sku_id,
    base::FilePath* product_name_file_out,
    base::FilePath* product_sku_file_out) {
  *product_name_file_out = base::FilePath("product_name");
  // Add a newline to mimic the kernel file.
  std::string content = name + "\n";
  if (base::WriteFile(*product_name_file_out, content.c_str(),
                      content.length()) != content.length()) {
    CROS_CONFIG_LOG(ERROR) << "Failed to write product_name file";
    return false;
  }
  *product_sku_file_out = base::FilePath("product_sku");
  std::string sku_id_str = base::StringPrintf("sku%d", sku_id);
  if (base::WriteFile(*product_sku_file_out, sku_id_str.c_str(),
                      sku_id_str.length()) != sku_id_str.length()) {
    CROS_CONFIG_LOG(ERROR) << "Failed to write product_sku file";
    return false;
  }

  return true;
}

bool CrosConfigIdentityX86::ReadInfo(const base::FilePath& product_name_file,
                                     const base::FilePath& product_sku_file) {
  // Drop the newline from the end of the name.
  std::string raw_name;
  if (!base::ReadFileToString(product_name_file, &raw_name)) {
    CROS_CONFIG_LOG(WARNING) << "Cannot read product_name file ";
    raw_name = "";
  }
  base::TrimWhitespaceASCII(raw_name, base::TRIM_TRAILING, &name_);

  std::string sku_str;
  if (!base::ReadFileToString(product_sku_file, &sku_str)) {
    CROS_CONFIG_LOG(WARNING) << "Cannot read product_sku file ";
    sku_str = "";
  }
  if (std::sscanf(sku_str.c_str(), "sku%d", &sku_id_) != 1) {
    CROS_CONFIG_LOG(WARNING) << "Invalid SKU string: " << sku_str;
    sku_id_ = -1;
  }
  CROS_CONFIG_LOG(INFO) << "Read SMBIOS Identity - name: " << name_
                        << ", sku_id: " << sku_id_;
  return true;
}

bool CrosConfigIdentityX86::PlatformIdentityMatch(
    const base::Value& identity_dict) const {
  const std::string* name_match =
      identity_dict.FindStringKey("smbios-name-match");
  if (!name_match) {
    // If smbios-name-match is left unspecified, this means that any
    // SMBIOS product name will match.  This is useful on platforms
    // like Betty, where the product name is something arbitrary QEMU
    // (or some other emulator firmware) chooses.
    return true;
  }
  return base::EqualsCaseInsensitiveASCII(name_, *name_match);
}

std::string CrosConfigIdentityX86::DebugString() const {
  return base::StringPrintf("x86-identity[name=\"%s\", sku=%d, vpd=\"%s\"]",
                            name_.c_str(), sku_id_, GetVpdId().c_str());
}

}  // namespace brillo
