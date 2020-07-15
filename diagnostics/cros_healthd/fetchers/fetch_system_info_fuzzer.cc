// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>

#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {

class Environment {
 public:
  Environment() {
    logging::SetMinLogLevel(logging::LOG_FATAL);  // Disable logging.
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  // Generate a random string.
  std::string file_path(data, data + size);

  MockContext mock_context;
  mock_context.Initialize();
  mock_context.fake_system_config()->SetHasSkuNumber(true);
  mock_context.fake_system_config()->SetMarketingName("fake_marketing_name");
  SystemFetcher system_fetcher{&mock_context};
  auto system_info = system_fetcher.FetchSystemInfo(base::FilePath(file_path));

  return 0;
}

}  // namespace diagnostics
