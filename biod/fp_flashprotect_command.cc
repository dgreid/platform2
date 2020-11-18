// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/memory/ptr_util.h>
#include <chromeos/ec/ec_commands.h>

#include "biod/ec_command.h"
#include "biod/fp_flashprotect_command.h"

namespace biod {

std::unique_ptr<FpFlashProtectCommand> FpFlashProtectCommand::Create(
    const uint32_t flags, const uint32_t mask) {
  struct ec_params_flash_protect fp_req;
  fp_req.flags = flags;
  fp_req.mask = mask;

  // Using new to access non-public constructor. See https://abseil.io/tips/134.
  auto cmd = base::WrapUnique(new FpFlashProtectCommand());
  cmd->SetReq(fp_req);
  return cmd;
}

/**
 * @return string names of set flags
 */
std::string FpFlashProtectCommand::ParseFlags(uint32_t flags) {
  std::string output;
  if (flags & EC_FLASH_PROTECT_RO_AT_BOOT) {
    output += "RO_AT_BOOT  ";
  }
  if (flags & EC_FLASH_PROTECT_RO_NOW) {
    output += "RO_NOW  ";
  }
  if (flags & EC_FLASH_PROTECT_ALL_NOW) {
    output += "ALL_NOW  ";
  }
  if (flags & EC_FLASH_PROTECT_GPIO_ASSERTED) {
    output += "GPIO_ASSERTED  ";
  }
  if (flags & EC_FLASH_PROTECT_ERROR_STUCK) {
    output += "ERROR_STUCK  ";
  }
  if (flags & EC_FLASH_PROTECT_ERROR_INCONSISTENT) {
    output += "ERROR_INCONSISTENT  ";
  }
  if (flags & EC_FLASH_PROTECT_ALL_AT_BOOT) {
    output += "ALL_AT_BOOT  ";
  }
  if (flags & EC_FLASH_PROTECT_RW_AT_BOOT) {
    output += "RW_AT_BOOT  ";
  }
  if (flags & EC_FLASH_PROTECT_RW_NOW) {
    output += "RW_NOW  ";
  }
  if (flags & EC_FLASH_PROTECT_ROLLBACK_AT_BOOT) {
    output += "ROLLBACK_AT_BOOT  ";
  }
  if (flags & EC_FLASH_PROTECT_ROLLBACK_NOW) {
    output += "ROLLBACK_NOW  ";
  }

  return output;
}

}  // namespace biod
