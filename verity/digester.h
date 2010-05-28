// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
#ifndef VERITY_DIGESTER_H__
#define VERITY_DIGESTER_H__

#include <stdint.h>
#include <sys/types.h>

namespace verity {

// Digester
// Abstract class providing cryptographic hash digest
// functionality to Verity.
class Digester {
 public:
  Digester();
  virtual ~Digester();
  virtual bool Initialize() = 0;
  // TODO(wad) refactor to support multiple simultaneous
  //           Init/Update/Final workflows to speed up FileHasher.
  virtual bool Check(const uint8_t *data,
                     size_t length,
                     const uint8_t *expected_digest) = 0;
  virtual bool Compute(const uint8_t *data,
                       size_t length,
                       uint8_t *digest,
                       unsigned int available) = 0;
  virtual unsigned int Size() const = 0;
  virtual const char *algorithm() const = 0;
};

}  // namespace verity

#endif  // VERITY_DIGESTER_H__
