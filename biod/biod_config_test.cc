// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_config.h"

#include <cros_config/fake_cros_config.h>
#include <gtest/gtest.h>

namespace biod {

TEST(FingerprintUnsupportedTest, FingerprintLocationUnset) {
  // Given a device that does not indicate fingerprint sensor location
  brillo::FakeCrosConfig cros_config;
  // expect FingerprintUnsupported to report false.
  EXPECT_FALSE(FingerprintUnsupported(&cros_config));
}

TEST(FingerprintUnsupportedTest, FingerprintLocationSet) {
  brillo::FakeCrosConfig cros_config;
  cros_config.SetString(kCrosConfigFPPath, kCrosConfigFPLocation,
                        "power-button-top-left");
  EXPECT_FALSE(FingerprintUnsupported(&cros_config));
}

TEST(FingerprintUnsupportedTest, FingerprintLocationSetNone) {
  brillo::FakeCrosConfig cros_config;
  cros_config.SetString(kCrosConfigFPPath, kCrosConfigFPLocation, "none");
  EXPECT_TRUE(FingerprintUnsupported(&cros_config));
}

}  // namespace biod
