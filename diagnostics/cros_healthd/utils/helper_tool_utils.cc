// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/helper_tool_utils.h"

#include <limits.h>

#include <base/strings/stringprintf.h>

namespace diagnostics {

bool GetHelperPath(const std::string& relative_path, std::string* full_path) {
  const char* helpers_dir = "/usr/libexec/healthd/helpers";
  std::string path =
      base::StringPrintf("%s/%s", helpers_dir, relative_path.c_str());

  if (path.length() >= PATH_MAX)
    return false;

  *full_path = path;
  return true;
}

}  // namespace diagnostics
