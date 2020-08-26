// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "ml/soda.h"

TEST(SodaLibraryTest, CanLoadLibraryAndLookupFunction) {
  auto* const instance = ml::SodaLibrary::GetInstance();
  EXPECT_EQ(instance->GetStatus(), ml::SodaLibrary::Status::kOk);
}
