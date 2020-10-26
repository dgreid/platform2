// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/crypto_init/bio_crypto_init.h"

#include <memory>

#include <testing/gtest/include/gtest/gtest.h>

namespace biod {

TEST(BioCryptoInitTest, CheckTemplateVersionCompatible) {
  BioCryptoInit bio_crypto_init(std::make_unique<EcCommandFactory>());
  EXPECT_TRUE(bio_crypto_init.CrosFpTemplateVersionCompatible(3, 3));
  EXPECT_TRUE(bio_crypto_init.CrosFpTemplateVersionCompatible(4, 4));
  // Format version 2 should not be in the field.
  EXPECT_FALSE(bio_crypto_init.CrosFpTemplateVersionCompatible(2, 2));

  // This should change when we deprecate firmware with template format v3
  EXPECT_TRUE(bio_crypto_init.CrosFpTemplateVersionCompatible(3, 4));

  // These are false because of the current rule and should change when we
  // launch format version 5.
  EXPECT_FALSE(bio_crypto_init.CrosFpTemplateVersionCompatible(4, 5));
  EXPECT_FALSE(bio_crypto_init.CrosFpTemplateVersionCompatible(5, 5));

  // This should break and be fixed when we uprev format version to 5 so that
  // we are guarding against unplanned uprev.
  EXPECT_TRUE(bio_crypto_init.CrosFpTemplateVersionCompatible(
      4, FP_TEMPLATE_FORMAT_VERSION));
}

}  // namespace biod
