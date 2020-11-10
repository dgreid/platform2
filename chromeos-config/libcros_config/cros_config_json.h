// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_JSON_H_
#define CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_JSON_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/values.h>

#include "chromeos-config/libcros_config/cros_config_impl.h"
#include "chromeos-config/libcros_config/identity.h"
#include "chromeos-config/libcros_config/identity_arm.h"
#include "chromeos-config/libcros_config/identity_x86.h"

namespace base {
class FilePath;
}  // namespace base

namespace brillo {

// JSON implementation of master configuration
class CrosConfigJson : public CrosConfigImpl {
 public:
  static constexpr char kRootName[] = "chromeos";
  static constexpr char kConfigListName[] = "configs";

  CrosConfigJson();
  CrosConfigJson(const CrosConfigJson&) = delete;
  CrosConfigJson& operator=(const CrosConfigJson&) = delete;

  ~CrosConfigJson() override;

  // CrosConfigInterface:
  bool GetString(const std::string& path,
                 const std::string& prop,
                 std::string* val_out) override;

  bool GetDeviceIndex(int* device_index_out) override;

  // CrosConfigImpl:
  bool SelectConfigByIdentity(const CrosConfigIdentity& identity) override;
  bool ReadConfigFile(const base::FilePath& filepath) override;

 private:
  // Helper used by SelectConfigByIdentity
  // @identity: The identity to match
  // @return: true on success, false otherwise
  bool SelectConfigByIdentityInternal(const CrosConfigIdentity& identity);

  base::Value json_config_;
  // Owned by json_config_
  const base::Value* config_dict_;  // Root of configs

  int device_index_;
};

}  // namespace brillo

#endif  // CHROMEOS_CONFIG_LIBCROS_CONFIG_CROS_CONFIG_JSON_H_
