// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_EDID_H_
#define RUNTIME_PROBE_FUNCTIONS_EDID_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Parse EDID files from DRM devices in sysfs.
//
// @param dir_path a list of paths to be evaluated. (Default:
// {"/sys/class/drm/<wildcard>"})
class EdidFunction : public ProbeFunction {
 public:
  NAME_PROBE_FUNCTION("edid");

  static std::unique_ptr<EdidFunction> FromKwargsValue(
      const base::Value& dict_value);

  DataType Eval() const override;

  int EvalInHelper(std::string* output) const override;

 private:
  // The path of target sysfs device, the last component can contain '*'.
  std::vector<std::string> dir_path_;

  std::vector<base::FilePath> GetEdidPaths(
      const base::FilePath& glob_path) const;

  base::Value EvalInHelperByPath(const base::FilePath& node_path) const;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_EDID_H_
