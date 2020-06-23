/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef RUNTIME_PROBE_FUNCTIONS_SHELL_H_
#define RUNTIME_PROBE_FUNCTIONS_SHELL_H_

#include <memory>
#include <string>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

/* All probe functions should inherit runtime_probe::ProbeFunction */
class ShellFunction : public ProbeFunction {
 public:
  /* The identifier / function name of this probe function.
   *
   * It will be used for both parsing and logging.
   */
  static constexpr auto function_name = "shell";

  /* Define a parser for this function.
   *
   * This function takes the arguments in `const base::Value&` type.
   * This function should parse the `dict_value`, if the `dict_value` has
   * correct format, this function should return a new instance of
   * `ShellFunction` whose members are decided by `dict_value`.
   *
   * @args dict_value: a JSON dictionary to parse arguments from.
   *
   * @return pointer to new `ShellFunction` instance on success, nullptr
   *   otherwise.
   */
  static std::unique_ptr<ProbeFunction> FromValue(
      const base::Value& dict_value) {
    /* Create an instance of ShellFunction.
     * **NOTE: The name should always be "instance" for PARSE_ARGUMENT to work**
     */
    auto instance = std::make_unique<ShellFunction>();

    bool result = true;

    result &= PARSE_ARGUMENT(command);
    result &= PARSE_ARGUMENT(key, std::string{"shell_raw"});
    result &= PARSE_ARGUMENT(split_line, false);

    if (result)
      return instance;
    /* Otherwise, return nullptr */
    return nullptr;
  }

  /* Override `Eval` function, which should return a list of Value  */
  DataType Eval() const override {
    VLOG(1) << "command: " << command_;
    /* TODO(stimim): implement this */

    return DataType{};
  }

 private:
  /* Declare a register for this function.  This has to be defined for
   * `REGISTER_PROBE_FUNCTION(...)` to work.
   */
  static ProbeFunction::Register<ShellFunction> register_;

  /* Declare function arguments */
  std::string command_;
  std::string key_;
  bool split_line_;
};

/* Register the ShellFunction */
REGISTER_PROBE_FUNCTION(ShellFunction);

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_SHELL_H_
