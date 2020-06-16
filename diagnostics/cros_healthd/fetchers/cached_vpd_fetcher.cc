// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/cached_vpd_fetcher.h"

#include <string>

#include <base/logging.h>

#include "diagnostics/common/file_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::CachedVpdInfo;
using ::chromeos::cros_healthd::mojom::CachedVpdResult;
using ::chromeos::cros_healthd::mojom::CachedVpdResultPtr;
using ::chromeos::cros_healthd::mojom::ErrorType;

constexpr char kRelativeSkuNumberDir[] = "sys/firmware/vpd/ro/";
constexpr char kSkuNumberFileName[] = "sku_number";

}  // namespace

CachedVpdFetcher::CachedVpdFetcher(Context* context) : context_(context) {
  DCHECK(context_);
}

CachedVpdFetcher::~CachedVpdFetcher() = default;

CachedVpdResultPtr CachedVpdFetcher::FetchCachedVpdInfo(
    const base::FilePath& root_dir) {
  CachedVpdInfo vpd_info;
  if (context_->system_config()->HasSkuNumberProperty()) {
    std::string sku_number;
    if (!ReadAndTrimString(root_dir.Append(kRelativeSkuNumberDir),
                           kSkuNumberFileName, &sku_number)) {
      return CachedVpdResult::NewError(CreateAndLogProbeError(
          ErrorType::kFileReadError,
          "Unable to read VPD file " + std::string(kSkuNumberFileName) +
              " at path " + std::string(kRelativeSkuNumberDir)));
    }
    vpd_info.sku_number = sku_number;
  }

  return CachedVpdResult::NewVpdInfo(CachedVpdInfo::New(vpd_info));
}

}  // namespace diagnostics
