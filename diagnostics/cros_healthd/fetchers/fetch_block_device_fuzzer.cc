// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>

#include "diagnostics/cros_healthd/fetchers/disk_fetcher.h"

namespace diagnostics {

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);  // Disable logging.
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  // Generate a random string.
  std::string file_path(data, data + size);

  DiskFetcher disk_fetcher;
  auto block_device_info =
      disk_fetcher.FetchNonRemovableBlockDevicesInfo(base::FilePath(file_path));

  return 0;
}

}  // namespace diagnostics
