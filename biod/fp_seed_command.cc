// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/fp_seed_command.h"

#include <algorithm>
#include <memory>

namespace biod {

std::unique_ptr<FpSeedCommand> FpSeedCommand::Create(
    const brillo::SecureVector& seed, uint16_t seed_version) {
  if (seed.size() != kTpmSeedSize) {
    return nullptr;
  }

  // Using new to access non-public constructor. See https://abseil.io/tips/134.
  auto seed_cmd = base::WrapUnique(new FpSeedCommand());
  auto* req = seed_cmd->Req();
  req->struct_version = seed_version;
  std::copy(seed.cbegin(), seed.cbegin() + sizeof(req->seed), req->seed);
  return seed_cmd;
}

}  // namespace biod
