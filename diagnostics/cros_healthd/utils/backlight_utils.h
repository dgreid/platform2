// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_BACKLIGHT_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_BACKLIGHT_UTILS_H_

#include <vector>

#include <base/files/file_path.h>
#include <chromeos/chromeos-config/libcros_config/cros_config_interface.h>

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

class BacklightFetcher final {
 public:
  explicit BacklightFetcher(brillo::CrosConfigInterface* cros_config);
  BacklightFetcher(const BacklightFetcher&) = delete;
  BacklightFetcher& operator=(const BacklightFetcher&) = delete;
  ~BacklightFetcher();

  // Returns a structure with either the device's backlight info or the error
  // that occurred fetching the information.
  chromeos::cros_healthd::mojom::BacklightResultPtr FetchBacklightInfo(
      const base::FilePath& root_dir);

 private:
  // Unowned pointer that should outlive this instance.
  brillo::CrosConfigInterface* cros_config_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_BACKLIGHT_UTILS_H_
