// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTION_TEMPLATES_STORAGE_H_
#define RUNTIME_PROBE_FUNCTION_TEMPLATES_STORAGE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/optional.h>
#include <base/values.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

class StorageFunction : public ProbeFunction {
 public:
  // Must be implemented by derived function to invoke the helper properly.
  std::string GetFunctionName() const override = 0;

  // This class is a template for storage probing workflow and should never be
  // instantiated.
  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value) = delete;

  // Override `Eval` function, which should return a list of Value.
  DataType Eval() const final;
  int EvalInHelper(std::string* output) const override;

 protected:
  StorageFunction() = default;
  // The following are storage-type specific building blocks.
  // Must be implemented on each derived storage probe function class.

  // Evaluate the storage indicated by |storage_dv| to retrieve auxiliary
  // information. This is reserved for probing we may want to do OUTSIDE of
  // runtime_probe_helper.
  virtual base::Optional<base::Value> EvalByDV(
      const base::Value& storage_dv) const;

  // Evals the network indicated by |node_path| in runtime_probe_helper.
  // Returns a dictionary type Value with device attributes of |node_path|,
  // which must contain at least the "type" key. On error, it returns
  // base::nullopt.
  virtual base::Optional<base::Value> EvalInHelperByPath(
      const base::FilePath& node_path) const = 0;

 private:
  // The following functions are shared across different types of storage.
  std::vector<base::FilePath> GetFixedDevices() const;
  base::Optional<int64_t> GetStorageSectorCount(
      const base::FilePath& node_path) const;
  int32_t GetStorageLogicalBlockSize(const base::FilePath& node_path) const;

  friend class GenericStorageFunction;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTION_TEMPLATES_STORAGE_H_
