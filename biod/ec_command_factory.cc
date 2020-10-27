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
std::unique_ptr<biod::FpFrameCommand> EcCommandFactory::FpFrameCommand(
    int index, uint32_t frame_size, uint16_t max_read_size) {
  return std::make_unique<biod::FpFrameCommand>(index, frame_size,
                                                max_read_size);
}

std::unique_ptr<biod::FpSeedCommand> EcCommandFactory::FpSeedCommand(
    const brillo::SecureVector& seed, uint16_t seed_version) {
  return FpSeedCommand::Create(seed, seed_version);
}

}  // namespace biod
