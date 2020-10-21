// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_TOOLS_MOCK_BIO_CRYPTO_INIT_H_
#define BIOD_TOOLS_MOCK_BIO_CRYPTO_INIT_H_

#include <gmock/gmock.h>

#include "biod/tools/bio_crypto_init.h"

namespace biod {

class MockBioCryptoInit : public BioCryptoInit {
 public:
  using BioCryptoInit::BioCryptoInit;
  MOCK_METHOD(bool,
              DoProgramSeed,
              (const brillo::SecureVector& tpm_seed),
              (override));
  MOCK_METHOD(bool, NukeFile, (const base::FilePath& filepath), (override));
  MOCK_METHOD(bool,
              WriteSeedToCrosFp,
              (const brillo::SecureVector& seed),
              (override));
  MOCK_METHOD(base::ScopedFD, OpenCrosFpDevice, (), (override));
  MOCK_METHOD(bool,
              WaitOnEcBoot,
              (const base::ScopedFD& cros_fp_fd,
               ec_current_image expected_image),
              (override));

  bool WriteSeedToCrosFpDelegate(const brillo::SecureVector& seed) {
    return BioCryptoInit::WriteSeedToCrosFp(seed);
  }
};

}  // namespace biod

#endif  // BIOD_TOOLS_MOCK_BIO_CRYPTO_INIT_H_
