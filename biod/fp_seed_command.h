// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_FP_SEED_COMMAND_H_
#define BIOD_FP_SEED_COMMAND_H_

#include <memory>

#include <brillo/secure_blob.h>
#include "biod/ec_command.h"

namespace biod {

class FpSeedCommand : public EcCommand<struct ec_params_fp_seed, EmptyParam> {
 public:
  static std::unique_ptr<FpSeedCommand> Create(const brillo::SecureVector& seed,
                                               uint16_t seed_version);
  static constexpr int kTpmSeedSize = FP_CONTEXT_TPM_BYTES;
  ~FpSeedCommand() override = default;

 private:
  FpSeedCommand() : EcCommand(EC_CMD_FP_SEED) {}
};

}  // namespace biod

#endif  // BIOD_FP_SEED_COMMAND_H_
