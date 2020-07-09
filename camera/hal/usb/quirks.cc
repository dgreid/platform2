/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/quirks.h"

#include <map>
#include <utility>

#include <base/no_destructor.h>

namespace cros {

namespace {

using VidPidPair = std::pair<std::string, std::string>;
using QuirksMap = std::map<VidPidPair, uint32_t>;

const QuirksMap& GetQuirksMap() {
  static const base::NoDestructor<QuirksMap> kQuirksMap({
      // Logitech Webcam Pro 9000 (b/138159048)
      {{"046d", "0809"}, kQuirkPreferMjpeg},
      // Huddly GO (crbug.com/1010557)
      {{"2bd9", "0011"}, kQuirkRestartOnTimeout},
      // Liteon 5M AF 6BA502N2 (b/147397859)
      {{"0bda", "5646"}, kQuirkReportLeastFpsRanges},
      // Liteon AR CCD 8BA842N2A (b/147397859)
      {{"0bda", "5647"}, kQuirkReportLeastFpsRanges},
      // Genesys Logic, Inc. (b/160544169)
      {{"05e3", "f11a"}, kQuirkReportLeastFpsRanges},
      // Logitech Tap HDMI Capture (b/146590270)
      {{"046d", "0876"}, kQuirkRestartOnTimeout},
      // IPEVO Ziggi-HD Plus
      {{"1778", "0225"}, kQuirkDisableFrameRateSetting},
      // Chicony CNFFH37 (b/158957477)
      {{"0c45", "6a05"}, kQuirkUserSpaceTimestamp},
  });
  return *kQuirksMap;
}

}  // namespace

uint32_t GetQuirks(const std::string& vid, const std::string& pid) {
  const QuirksMap& quirks_map = GetQuirksMap();
  auto it = quirks_map.find({vid, pid});
  if (it != quirks_map.end()) {
    return it->second;
  }
  return 0;
}

}  // namespace cros
