// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/ec_command_factory.h"
#include "biod/fp_flashprotect_command.h"
#include "biod/fp_info_command.h"

namespace biod {

std::unique_ptr<EcCommandInterface> EcCommandFactory::FpContextCommand(
    CrosFpDeviceInterface* cros_fp, const std::string& user_id) {
  return FpContextCommandFactory::Create(cros_fp, user_id);
}

std::unique_ptr<FpFlashProtectCommand> EcCommandFactory::FpFlashProtectCommand(
    const uint32_t flags, const uint32_t mask) {
  return FpFlashProtectCommand::Create(flags, mask);
}

std::unique_ptr<FpInfoCommand> EcCommandFactory::FpInfoCommand() {
  return std::make_unique<biod::FpInfoCommand>();
}

}  // namespace biod
