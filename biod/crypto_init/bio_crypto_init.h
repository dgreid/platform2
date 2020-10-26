// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CRYPTO_INIT_BIO_CRYPTO_INIT_H_
#define BIOD_CRYPTO_INIT_BIO_CRYPTO_INIT_H_

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <brillo/secure_blob.h>
#include <chromeos/ec/ec_commands.h>

namespace biod {

class BioCryptoInit {
 public:
  BioCryptoInit() = default;
  virtual ~BioCryptoInit() = default;

  virtual bool DoProgramSeed(const brillo::SecureVector& tpm_seed);
  virtual bool NukeFile(const base::FilePath& filepath);
  virtual bool CrosFpTemplateVersionCompatible(
      const uint32_t firmware_fp_template_format_version,
      const uint32_t biod_fp_template_format_version);

 protected:
  virtual bool WriteSeedToCrosFp(const brillo::SecureVector& seed);
  virtual base::ScopedFD OpenCrosFpDevice();
  virtual bool WaitOnEcBoot(const base::ScopedFD& cros_fp_fd,
                            ec_current_image expected_image);
};

}  // namespace biod

#endif  // BIOD_CRYPTO_INIT_BIO_CRYPTO_INIT_H_
