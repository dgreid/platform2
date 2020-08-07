// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "biod/ec_command.h"
#include "biod/fp_flashprotect_command.h"

namespace biod {
namespace {

TEST(FpFlashProtectCommand, FpFlashProtectCommand) {
  uint32_t flags = 0xdeadbeef;
  uint32_t mask = 0xfeedc0de;
  auto cmd = FpFlashProtectCommand::Create(flags, mask);
  EXPECT_TRUE(cmd);
  EXPECT_EQ(cmd->Version(), EC_VER_FLASH_PROTECT);
  EXPECT_EQ(cmd->Command(), EC_CMD_FLASH_PROTECT);
  EXPECT_EQ(cmd->Req()->flags, flags);
  EXPECT_EQ(cmd->Req()->mask, mask);
}

TEST(FpFlashProtectCommand, ParseFlags) {
  std::string result;

  // test each flag string individually
  uint32_t flags = EC_FLASH_PROTECT_RO_AT_BOOT;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RO_AT_BOOT  ");

  flags = EC_FLASH_PROTECT_RO_NOW;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RO_NOW  ");

  flags = EC_FLASH_PROTECT_ALL_NOW;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ALL_NOW  ");

  flags = EC_FLASH_PROTECT_GPIO_ASSERTED;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "GPIO_ASSERTED  ");

  flags = EC_FLASH_PROTECT_ERROR_STUCK;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ERROR_STUCK  ");

  flags = EC_FLASH_PROTECT_ERROR_INCONSISTENT;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ERROR_INCONSISTENT  ");

  flags = EC_FLASH_PROTECT_ALL_AT_BOOT;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ALL_AT_BOOT  ");

  flags = EC_FLASH_PROTECT_RW_AT_BOOT;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RW_AT_BOOT  ");

  flags = EC_FLASH_PROTECT_RW_NOW;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RW_NOW  ");

  flags = EC_FLASH_PROTECT_ROLLBACK_AT_BOOT;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ROLLBACK_AT_BOOT  ");

  flags = EC_FLASH_PROTECT_ROLLBACK_NOW;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "ROLLBACK_NOW  ");

  // test a combination of flags
  flags = EC_FLASH_PROTECT_RO_AT_BOOT | EC_FLASH_PROTECT_RO_NOW |
          EC_FLASH_PROTECT_GPIO_ASSERTED;
  result = FpFlashProtectCommand::ParseFlags(flags);
  EXPECT_EQ(result, "RO_AT_BOOT  RO_NOW  GPIO_ASSERTED  ");
}

}  // namespace
}  // namespace biod
