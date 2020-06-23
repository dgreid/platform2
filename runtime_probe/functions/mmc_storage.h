// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_MMC_STORAGE_H_
#define RUNTIME_PROBE_FUNCTIONS_MMC_STORAGE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>

#include "runtime_probe/function_templates/storage.h"

namespace runtime_probe {

class MmcStorageFunction : public StorageFunction {
 public:
  static constexpr auto function_name = "mmc_storage";
  std::string GetFunctionName() const override { return function_name; }

  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value) {
    if (dict_value.DictSize() != 0) {
      LOG(ERROR) << function_name << " does not take any argument";
      return nullptr;
    }
    return std::make_unique<MmcStorageFunction>();
  }

 protected:
  base::Optional<base::Value> EvalByDV(
      const base::Value& storage_dv) const override;

  // Eval the eMMC storage indicated by |node_path| inside the
  // runtime_probe_helper.
  base::Optional<base::Value> EvalInHelperByPath(
      const base::FilePath& node_path) const override;

 private:
  bool CheckStorageTypeMatch(const base::FilePath& node_path) const;
  // Get the result of mmc extcsd from debugd and put it into |output|.
  bool GetOutputOfMmcExtcsd(std::string* output) const;
  std::string GetStorageFwVersion(const base::FilePath& node_path) const;

  static ProbeFunction::Register<MmcStorageFunction> register_;
};

// Register the MmcStorageFunction
REGISTER_PROBE_FUNCTION(MmcStorageFunction);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_MMC_STORAGE_H_
