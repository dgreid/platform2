// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/common/openssl_utility.h"

#include <base/logging.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>

namespace hwsec_test_utils {

std::string GetOpenSSLError() {
  crypto::ScopedBIO bio(BIO_new(BIO_s_mem()));
  ERR_print_errors(bio.get());
  char* data = nullptr;
  int data_len = BIO_get_mem_data(bio.get(), &data);
  std::string error_string(data, data_len);
  return error_string;
}

crypto::ScopedEVP_PKEY PemToEVP(const std::string& pem) {
  crypto::ScopedBIO bio(
      BIO_new_mem_buf(const_cast<char*>(pem.data()), pem.size()));
  if (!bio) {
    LOG(ERROR) << __func__
               << ": Failed to create mem BIO: " << GetOpenSSLError();
    return nullptr;
  }
  crypto::ScopedEVP_PKEY key(
      PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (!key) {
    LOG(ERROR) << __func__
               << ": Failed to read key with PEM_read_bio_PrivateKey: "
               << GetOpenSSLError();
    return nullptr;
  }
  return key;
}

}  // namespace hwsec_test_utils
