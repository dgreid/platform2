// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <base/at_exit.h>
#include <mojo/core/embedder/embedder.h>

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  base::AtExitManager at_exit;

  mojo::core::Init();

  return RUN_ALL_TESTS();
}
