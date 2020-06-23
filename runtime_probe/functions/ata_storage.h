// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_ATA_STORAGE_H_
#define RUNTIME_PROBE_FUNCTIONS_ATA_STORAGE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>

#include "runtime_probe/function_templates/storage.h"

namespace runtime_probe {

class AtaStorageFunction : public StorageFunction {
 public:
  static constexpr auto function_name = "ata_storage";
  std::string GetFunctionName() const override { return function_name; }

  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value) {
    if (dict_value.DictSize() != 0) {
      LOG(ERROR) << function_name << " does not take any argument";
      return nullptr;
    }
    return std::make_unique<AtaStorageFunction>();
  }

 protected:
  // Eval the ATA storage indicated by |node_path| inside the
  // runtime_probe_helper.
  base::Optional<base::Value> EvalInHelperByPath(
      const base::FilePath& node_path) const override;

 private:
  bool CheckStorageTypeMatch(const base::FilePath& node_path) const;
  std::string GetStorageFwVersion(const base::FilePath& node_path) const;
  static ProbeFunction::Register<AtaStorageFunction> register_;
};

// Register the AtaStorageFunction
REGISTER_PROBE_FUNCTION(AtaStorageFunction);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_ATA_STORAGE_H_
