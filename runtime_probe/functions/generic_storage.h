// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_GENERIC_STORAGE_H_
#define RUNTIME_PROBE_FUNCTIONS_GENERIC_STORAGE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>

#include "runtime_probe/function_templates/storage.h"
#include "runtime_probe/functions/ata_storage.h"
#include "runtime_probe/functions/mmc_storage.h"
#include "runtime_probe/functions/nvme_storage.h"

namespace runtime_probe {

class GenericStorageFunction : public StorageFunction {
 public:
  NAME_PROBE_FUNCTION("generic_storage");

  static std::unique_ptr<GenericStorageFunction> FromKwargsValue(
      const base::Value& dict_value);

 protected:
  base::Optional<base::Value> EvalByDV(
      const base::Value& storage_dv) const override;
  // Eval the storage indicated by |node_path| inside the
  // runtime_probe_helper.
  base::Optional<base::Value> EvalInHelperByPath(
      const base::FilePath& node_path) const override;

 private:
  // Use FromKwargsValue to ensure the arg is correctly parsed.
  GenericStorageFunction() = default;

  std::unique_ptr<AtaStorageFunction> ata_prober_;
  std::unique_ptr<MmcStorageFunction> mmc_prober_;
  std::unique_ptr<NvmeStorageFunction> nvme_prober_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_GENERIC_STORAGE_H_
