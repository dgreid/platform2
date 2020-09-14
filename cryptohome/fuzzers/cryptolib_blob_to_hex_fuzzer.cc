// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <cryptohome/cryptolib.h>

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOG_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  brillo::Blob blob(data, data + size);
  auto ret = cryptohome::CryptoLib::BlobToHex(blob);
  return 0;
}
