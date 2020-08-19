// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/values.h>

#include "runtime_probe/functions/sysfs.h"
#include "runtime_probe/utils/file_utils.h"

namespace runtime_probe {

std::unique_ptr<SysfsFunction> SysfsFunction::FromKwargsValue(
    const base::Value& dict_value) {
  // Create an instance of SysfsFunction.
  // **NOTE: The name should always be "instance" for PARSE_ARGUMENT to work**
  auto instance = std::make_unique<SysfsFunction>();

  bool result = true;

  // Parse each argument one by one.
  //
  //  1. Due to the template declaration, the type of default value and member
  //  must match exactly.  That is, the default value of a double argument must
  //  be double (3.0 instead of 3).  And default value of string argument must
  //  be std::string{...}.
  //
  //  2. Due to the behavior of "&=", all parser will be executed even if some
  //  of them failed.
  result &= PARSE_ARGUMENT(dir_path);
  result &= PARSE_ARGUMENT(keys);
  result &= PARSE_ARGUMENT(optional_keys, {});

  if (result)
    return instance;
  return nullptr;
}

SysfsFunction::DataType SysfsFunction::Eval() const {
  DataType result{};

  const base::FilePath glob_path{dir_path_};
  const auto glob_root = glob_path.DirName();
  const auto glob_pattern = glob_path.BaseName();

  if (!base::FilePath{"/sys/"}.IsParent(glob_root)) {
    if (sysfs_path_for_testing_.empty()) {
      LOG(ERROR) << glob_root.value() << " is not under /sys/";
      return {};
    }
    // While testing, |sysfs_path_for_testing_| can be set to allow additional
    // path.
    if (sysfs_path_for_testing_.IsParent(glob_root) ||
        sysfs_path_for_testing_ == glob_root) {
      LOG(WARNING) << glob_root.value() << " is allowed because "
                   << "sysfs_path_for_testing_ is set to "
                   << sysfs_path_for_testing_.value();
    } else {
      LOG(ERROR) << glob_root.value() << " is neither under under /sys/ nor "
                 << sysfs_path_for_testing_.value();
      return {};
    }
  }

  base::FileEnumerator sysfs_it(glob_root, false,
                                base::FileEnumerator::FileType::DIRECTORIES,
                                glob_pattern.value());
  while (true) {
    auto sysfs_path = sysfs_it.Next();
    if (sysfs_path.empty())
      break;

    auto dict_value = MapFilesToDict(sysfs_path, keys_, optional_keys_);
    if (dict_value)
      result.push_back(std::move(*dict_value));
  }
  return result;
}

}  // namespace runtime_probe
