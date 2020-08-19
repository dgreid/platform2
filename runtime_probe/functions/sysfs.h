// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_SYSFS_H_
#define RUNTIME_PROBE_FUNCTIONS_SYSFS_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Read system directory into key value pairs.
//
// Kernel modules export attributes as files under /sys, this funciton is
// aimed to read this kind of structure.
//
// For example,
//   /sys/bus/cool/devices/Da/ contains file "1",
//
//   /sys/bus/cool/devices/Db/ contains file "1",
//   /sys/bus/cool/devices/Db/ contains file "2",
//
//   /sys/bus/cool/devices/Dc/ contains file "2",
//   /sys/bus/cool/devices/Dc/ contains file "4",
//
// And the probe statement is::
//   {
//     "dir_path": "/sys/bus/cool/devices/D*",
//     "keys": ["1"],
//     "optional_keys": ["2"]
//   }
//
// Then the probe result will be::
//   [
//     {
//       "1": "<content of Da/1>"
//       // no entry "2" because "Da/2" doesn't exists.
//     },
//     {
//       "1": "<content of Db/1>",
//       "2": "<content of Db/2>"
//     }
//     // No report for "Dc" because "Dc/1" doesn't exists.
//   ]
class SysfsFunction : public ProbeFunction {
  // All probe functions should inherit runtime_probe::ProbeFunction
 public:
  // The identifier / function name of this probe function.
  //
  // It will be used for both parsing and logging.
  NAME_PROBE_FUNCTION("sysfs");

  // Define a parser for this function.
  //
  // This function takes the arguments in `const base::Value&` type.
  // This function should parse the `dict_value`, if the `dict_value` has
  // correct format, this function should return a new instance of
  // `SysfsFunction` whose members are decided by `dict_value`.
  //
  // @args dict_value: a JSON dictionary to parse arguments from.
  //
  // @return pointer to new `SysfsFunction` instance on success, nullptr
  //   otherwise.
  static std::unique_ptr<SysfsFunction> FromKwargsValue(
      const base::Value& dict_value);

  // Override `Eval` function, which should return a list of Value.
  DataType Eval() const override;

 private:
  // Declare function arguments

  // The path of target sysfs folder, the last component can contain '*'.
  std::string dir_path_;
  // Required file names in the sysfs folder.
  std::vector<std::string> keys_;
  // Optional fils names in the sysfs folder.
  std::vector<std::string> optional_keys_;

  // A mocked sysfs path that we allow to read while testing.
  base::FilePath sysfs_path_for_testing_;

  // Set mocked sysfs path for testing.
  //
  // Normally, this probe function will fail if |dir_path_| is not a
  // subdirectory of /sys/.  Call this function to allow an additional path.
  //
  // This function will fail if the mock path is set twice.
  void MockSysfsPathForTesting(base::FilePath mock_path);

  FRIEND_TEST(SysfsFunctionTest, TestRead);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_SYSFS_H_
