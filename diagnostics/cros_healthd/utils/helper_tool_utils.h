// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_HELPER_TOOL_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_HELPER_TOOL_UTILS_H_

#include <string>

namespace diagnostics {

// Get the full path of a helper executable located at the |relative_path|
// relative to the cros_healthd helpers directory. Return false if the full path
// is too long.
bool GetHelperPath(const std::string& relative_path, std::string* full_path);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_HELPER_TOOL_UTILS_H_
