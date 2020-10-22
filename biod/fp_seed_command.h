// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_FP_SEED_COMMAND_H_
#define BIOD_FP_SEED_COMMAND_H_

#include <algorithm>
#include <memory>

#include <brillo/secure_blob.h>
#include "biod/ec_command.h"

namespace biod {

class FpSeedCommand : public EcCommand<struct ec_params_fp_seed, EmptyParam> {
 public:
  static constexpr int kTpmSeedSize = FP_CONTEXT_TPM_BYTES;

  template <typename T = FpSeedCommand>
  static std::unique_ptr<T> Create(const brillo::SecureVector& seed,
                                   uint16_t seed_version) {
    static_assert(std::is_base_of<FpSeedCommand, T>::value,
                  "Only classes derived from FpSeedCommand can use Create");

    if (seed.size() != kTpmSeedSize) {
      return nullptr;
    }

    // Using new to access non-public constructor. See
    // https://abseil.io/tips/134.
    auto seed_cmd = base::WrapUnique(new T());
    auto* req = seed_cmd->Req();
    req->struct_version = seed_version;
    std::copy(seed.cbegin(), seed.cbegin() + sizeof(req->seed), req->seed);
    return seed_cmd;
  }
  ~FpSeedCommand() override;

  bool Run(int fd) override;

  /**
   * @warning Only intended to be used for testing.
   */
  const brillo::SecureVector seed() {
    return brillo::SecureVector(Req()->seed, Req()->seed + sizeof(Req()->seed));
  }

  /**
   * @warning Only intended to be used for testing.
   */
  const uint16_t seed_version() { return Req()->struct_version; }

 protected:
  virtual bool EcCommandRun(int fd);
  void ClearSeedBuffer();
  FpSeedCommand() : EcCommand(EC_CMD_FP_SEED) {}
};

}  // namespace biod

#endif  // BIOD_FP_SEED_COMMAND_H_
