// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_ECTOOL_I2CREAD_H_
#define RUNTIME_PROBE_FUNCTIONS_ECTOOL_I2CREAD_H_

#include <memory>
#include <string>

#include <base/values.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Execute "ectool i2cread" command.
// TODO(b/120826467) : Access /dev/cros_ec directly.
//
// This probe function takes the following arguments:
//   size: Return bits, it can be either 8 or 16.
//   port: The port of the I2C connected to EC.
//   addr: The I2C address
//   offset: The register offset.
//   key: The key of saved output. Output will be saved in string.
//
// More details can be found under command "ectool i2cread help"
class EctoolI2Cread : public ProbeFunction {
 public:
  static constexpr auto function_name = "ectool_i2cread";

  std::string GetFunctionName() const override { return function_name; }

  // Define a parser for this function.
  //
  // @args dict_value: a JSON dictionary to parse arguments from.
  //
  // @return pointer to new `EctoolI2Cread` instance on success, nullptr
  //   otherwise.
  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value) {
    if (dict_value.DictSize() != 5) {
      LOG(ERROR) << function_name << " expect 5 arguments.";
      return nullptr;
    }

    auto instance = std::make_unique<EctoolI2Cread>();
    bool result = true;

    result &= PARSE_ARGUMENT(size);
    result &= PARSE_ARGUMENT(port);
    result &= PARSE_ARGUMENT(addr);
    result &= PARSE_ARGUMENT(offset);
    result &= PARSE_ARGUMENT(key);

    if (result)
      return instance;
    return nullptr;
  }

  DataType Eval() const override;
  int EvalInHelper(std::string*) const override;

 private:
  int addr_;
  std::string key_;
  int offset_;
  int port_;
  int size_;

  static ProbeFunction::Register<EctoolI2Cread> register_;
};

// Register the EctoolI2Cread
REGISTER_PROBE_FUNCTION(EctoolI2Cread);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_ECTOOL_I2CREAD_H_
