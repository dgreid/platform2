// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Library to provide access to the Chrome OS master configuration

#ifndef CHROMEOS_CONFIG_LIBCROS_CONFIG_IDENTITY_X86_H_
#define CHROMEOS_CONFIG_LIBCROS_CONFIG_IDENTITY_X86_H_

#include <string>

#include <base/macros.h>
#include "chromeos-config/libcros_config/identity.h"

namespace base {
class FilePath;
class Value;
}  // namespace base

namespace brillo {

class CrosConfigIdentityX86 : public CrosConfigIdentity {
 public:
  CrosConfigIdentityX86();
  ~CrosConfigIdentityX86();

  // @return Name value read via ReadSmbios
  const std::string& GetName() const { return name_; }

  bool ReadInfo(const base::FilePath& product_name_file,
                const base::FilePath& product_sku_file) override;

  bool FakeProductFilesForTesting(
      const std::string& name,
      const int sku_id,
      base::FilePath* product_name_file_out,
      base::FilePath* product_sku_file_out) override;

  // CrosConfigIdentity:
  // Check that the SMBIOS name matches the one specified in the
  // identity dictionary
  bool PlatformIdentityMatch(const base::Value& identity_dict) const override;

  std::string DebugString() const override;

 private:
  std::string name_;

  DISALLOW_COPY_AND_ASSIGN(CrosConfigIdentityX86);
};

}  // namespace brillo

#endif  // CHROMEOS_CONFIG_LIBCROS_CONFIG_IDENTITY_X86_H_
