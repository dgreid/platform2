// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/file_utils.h"

#include <base/files/file_util.h>
#include <base/strings/string_util.h>

namespace diagnostics {

bool ReadAndTrimString(const base::FilePath& directory,
                       const std::string& filename,
                       std::string* out) {
  if (!base::ReadFileToString(directory.Append(filename), out))
    return false;

  base::TrimWhitespaceASCII(*out, base::TRIM_TRAILING, out);
  return true;
}

}  // namespace diagnostics
