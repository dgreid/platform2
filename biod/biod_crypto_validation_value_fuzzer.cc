// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <cstdint>

#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <openssl/sha.h>

#include "biod/biod_crypto.h"

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOG_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  std::vector<uint8_t> result;
  FuzzedDataProvider data_provider(data, size);

  std::string user_id = data_provider.ConsumeRandomLengthString(size);
  brillo::SecureVector secret(data_provider.ConsumeRemainingBytes<uint8_t>());

  biod::BiodCrypto::ComputeValidationValue(secret, user_id, &result);

  return 0;
}
