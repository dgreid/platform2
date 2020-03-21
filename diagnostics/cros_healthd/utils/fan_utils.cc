// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/fan_utils.h"

#include <cstdint>
#include <string>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::FanInfo;
using ::chromeos::cros_healthd::mojom::FanInfoPtr;

// The maximum amount of time to wait for a debugd response.
constexpr base::TimeDelta kDebugdDBusTimeout = base::TimeDelta::FromSeconds(10);

}  // namespace

FanFetcher::FanFetcher(org::chromium::debugdProxyInterface* debugd_proxy)
    : debugd_proxy_(debugd_proxy) {
  DCHECK(debugd_proxy_);
}

FanFetcher::~FanFetcher() = default;

std::vector<FanInfoPtr> FanFetcher::FetchFanInfo() {
  std::vector<FanInfoPtr> fan_info;
  std::string debugd_result;
  brillo::ErrorPtr error;
  if (!debugd_proxy_->CollectFanSpeed(&debugd_result, &error,
                                      kDebugdDBusTimeout.InMilliseconds())) {
    LOG(ERROR) << "Failed to collect fan speed from debugd: "
               << error->GetCode() << " " << error->GetMessage();
    return fan_info;
  }

  base::StringPairs pairs;
  base::SplitStringIntoKeyValuePairs(debugd_result, ':', '\n', &pairs);
  for (auto& pair : pairs) {
    base::TrimWhitespaceASCII(pair.second, base::TRIM_ALL, &pair.second);
    uint32_t speed;
    if (base::StringToUint(pair.second, &speed)) {
      fan_info.push_back(FanInfo::New(speed));
    } else {
      LOG(ERROR) << "Failed to convert string to integer: " << pair.second;
    }
  }

  return fan_info;
}

}  // namespace diagnostics
