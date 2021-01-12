// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "ml/soda.h"

TEST(SodaLibraryTest, CannotLoadLibraryAndLookupFunction) {
  // By default, the default instance shouldn't be instantiable since we're in a
  // test and the file shouldn't exist, etc.
  auto* const instance = ml::SodaLibrary::GetInstance();
  EXPECT_EQ(instance->GetStatus(), ml::SodaLibrary::Status::kLoadLibraryFailed);
}
