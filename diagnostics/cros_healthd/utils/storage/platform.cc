// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <linux/limits.h>

#include <base/logging.h>
#include <rootdev/rootdev.h>

#include "diagnostics/cros_healthd/utils/storage/platform.h"

namespace diagnostics {

namespace {

constexpr char kDevPrefix[] = "/dev/";

}

std::string Platform::GetRootDeviceName() const {
  char dev_path_cstr[PATH_MAX];

  // Get physical root device without partition
  int ret = rootdev(dev_path_cstr, sizeof(dev_path_cstr),
                    true /* resolve to physical */, true /* strip partition */);
  if (ret != 0) {
    PLOG(ERROR) << "Failed to retrieve root device";
    return "";
  }

  std::string dev_path = dev_path_cstr;

  if (dev_path.find(kDevPrefix) != 0) {
    PLOG(ERROR) << "Unexpected root device format " << dev_path;
    return "";
  }

  return dev_path.substr(std::string(kDevPrefix).length());
}

}  // namespace diagnostics
