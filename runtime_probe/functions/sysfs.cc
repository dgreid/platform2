/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
    /* While testing, |sysfs_path_for_testing_| can be set to allow additional
     * path.
     */
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
