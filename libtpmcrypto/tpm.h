// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTPMCRYPTO_TPM_H_
#define LIBTPMCRYPTO_TPM_H_

#include <stddef.h>

#include <memory>
#include <string>

#include <base/compiler_specific.h>
#include <base/macros.h>
#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>

namespace tpmcrypto {

class BRILLO_EXPORT Tpm {
 public:
  virtual ~Tpm() = default;

  // Seals a secret to PCR0 with the SRK.
  //
  // Parameters
  //   value - The value to be sealed.
  //   sealed_value - The sealed value.
  //
  // Returns true on success.
  virtual bool SealToPCR0(const brillo::SecureBlob& value,
                          brillo::SecureBlob* sealed_value)
      WARN_UNUSED_RESULT = 0;

  // Unseals a secret previously sealed with the SRK.
  //
  // Parameters
  //   sealed_value - The sealed value.
  //   value - The original value.
  //
  // Returns true on success.
  virtual bool Unseal(const brillo::SecureBlob& sealed_value,
                      brillo::SecureBlob* value) WARN_UNUSED_RESULT = 0;

  // Gets the attributes of an index in the NVRAM.
  //
  // Parameters
  //   index - The index in the NVRAM area.
  //   attributes - The output attributs of the NVRAM aread.
  //
  // Returns true on success;
  virtual bool GetNVAttributes(uint32_t index, uint32_t* attributes) = 0;

  // Reads the content of an NVRAM without authorization.
  //
  // Parameters
  //   index - The index in the NVRAM area.
  //   offset - The offset to read the data from.
  //   size - The size of the data in the NVRAM read.
  //   data - The output data read from the NVRAM area.
  //
  // Returns true on success;
  virtual bool NVReadNoAuth(uint32_t index,
                            uint32_t offset,
                            size_t size,
                            std::string* data) = 0;

 protected:
  Tpm() = default;
  Tpm(const Tpm&) = delete;
  Tpm& operator=(const Tpm&) = delete;
};

BRILLO_EXPORT std::unique_ptr<Tpm> CreateTpmInstance();

}  // namespace tpmcrypto

#endif  // LIBTPMCRYPTO_TPM_H_
