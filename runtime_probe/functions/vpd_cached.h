// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_VPD_CACHED_H_
#define RUNTIME_PROBE_FUNCTIONS_VPD_CACHED_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Read cached VPD information from sysfs.
//
// System usually boots with VPD cached, we read the cached version to avoid
// the delay of accessing the flashrom. If VPD data changed after boot, this
// function will not reflect that.
//
// In this first implementation, only one argument will be taken, that is the
// key in the RO_VPD area to read.

class VPDCached : public ProbeFunction {
 public:
  static constexpr auto function_name = "vpd_cached";

  std::string GetFunctionName() const override { return function_name; }

  // Define a parser for this function.
  //
  // @args dict_value: a JSON dictionary to parse arguments from.
  //
  // @return pointer to new `VPDCached` instance on success, nullptr otherwise.

  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value) {
    if (dict_value.DictSize() != 1) {
      LOG(ERROR) << function_name << " expect 1 arguments.";
      return nullptr;
    }

    auto instance = std::make_unique<VPDCached>();
    bool result = true;

    result &= PARSE_ARGUMENT(vpd_name);

    if (result)
      return instance;
    return nullptr;
  }

  DataType Eval() const override;
  int EvalInHelper(std::string* output) const override;

 private:
  std::string vpd_name_;

  static ProbeFunction::Register<VPDCached> register_;
};

// Register the VPDCached
REGISTER_PROBE_FUNCTION(VPDCached);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_VPD_CACHED_H_
