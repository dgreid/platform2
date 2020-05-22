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

#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/vpd_utils.h"

namespace diagnostics {

namespace {

constexpr char kCachedVpdPropertiesPath[] = "/cros-healthd/cached-vpd";
constexpr char kHasSkuNumberProperty[] = "has-sku-number";

}  // namespace

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
  mock_context.fake_cros_config()->SetString(kCachedVpdPropertiesPath,
                                             kHasSkuNumberProperty, "true");
  CachedVpdFetcher cached_vpd_fetcher{&mock_context};
  auto cached_vpd_info =
      cached_vpd_fetcher.FetchCachedVpdInfo(base::FilePath(file_path));

  return 0;
}

}  // namespace diagnostics
