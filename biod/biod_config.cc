// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_config.h"

#include <string>

#include <base/optional.h>

namespace biod {

constexpr char kCrosConfigFPPath[] = "/fingerprint";
constexpr char kCrosConfigFPBoard[] = "board";
constexpr char kCrosConfigFPLocation[] = "sensor-location";

// Since /fingerprint/sensor-location is an optional field, the only information
// that is relevant to the updater is if fingerprint is explicitly not
// supported.
bool FingerprintUnsupported(brillo::CrosConfigInterface* cros_config) {
  std::string fingerprint_location;
  if (cros_config->GetString(kCrosConfigFPPath, kCrosConfigFPLocation,
                             &fingerprint_location)) {
    if (fingerprint_location == "none") {
      return true;
    }
  }

  return false;
}

base::Optional<std::string> FingerprintBoard(
    brillo::CrosConfigInterface* cros_config) {
  std::string board_name;
  if (!cros_config->GetString(kCrosConfigFPPath, kCrosConfigFPBoard,
                              &board_name)) {
    return base::nullopt;
  }
  return board_name;
}

}  // namespace biod
