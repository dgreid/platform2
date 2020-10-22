// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>

#include "biod/fp_seed_command.h"

namespace biod {
namespace {

TEST(FpSeedCommand, Create_Success) {
  const brillo::SecureVector kSeed = {
      1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
      17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
  constexpr uint16_t kSeedVersion = 1;
  auto cmd = FpSeedCommand::Create(kSeed, kSeedVersion);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), 0);
  EXPECT_EQ(cmd->Command(), EC_CMD_FP_SEED);

  brillo::SecureVector seed_vec(cmd->Req()->seed,
                                cmd->Req()->seed + sizeof(cmd->Req()->seed));
  EXPECT_EQ(seed_vec, kSeed);
  EXPECT_EQ(cmd->Req()->struct_version, kSeedVersion);
}

TEST(FpSeedCommand, Create_InvalidSeedSize_TooSmall) {
  const brillo::SecureVector kSeed = {1, 2, 3};
  constexpr uint16_t kSeedVersion = 1;
  auto cmd = FpSeedCommand::Create(kSeed, kSeedVersion);
  EXPECT_FALSE(cmd);
}

TEST(FpSeedCommand, Create_InvalidSeedSize_TooLarge) {
  const brillo::SecureVector kSeed(256);
  constexpr uint16_t kSeedVersion = 1;
  auto cmd = FpSeedCommand::Create(kSeed, kSeedVersion);
  EXPECT_FALSE(cmd);
}

}  // namespace
}  // namespace biod
