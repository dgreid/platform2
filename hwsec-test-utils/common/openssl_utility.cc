// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwsec-test-utils/common/openssl_utility.h"

#include <memory>

#include <base/logging.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

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

base::Optional<std::string> GetRandom(size_t length) {
  std::unique_ptr<unsigned char[]> buffer =
      std::make_unique<unsigned char[]>(length);
  if (RAND_bytes(buffer.get(), length) != 1) {
    return {};
  }
  return std::string(buffer.get(), buffer.get() + length);
}

base::Optional<std::string> EVPDigestSign(const crypto::ScopedEVP_PKEY& key,
                                          const EVP_MD* md_type,
                                          const std::string& data) {
  CHECK(key.get());
  CHECK(md_type != nullptr);

  crypto::ScopedEVP_MD_CTX mdctx(EVP_MD_CTX_new());
  if (!mdctx) {
    LOG(ERROR) << __func__
               << ": Failed to allocate EVP_MD_CTX: " << GetOpenSSLError();
    return {};
  }

  if (EVP_DigestSignInit(mdctx.get(), nullptr, md_type, nullptr, key.get()) !=
      1) {
    LOG(ERROR) << __func__
               << ": Failed to call EVP_DigestSignInit: " << GetOpenSSLError();
    return {};
  }
  if (EVP_DigestSignUpdate(mdctx.get(), data.data(), data.length()) != 1) {
    LOG(ERROR) << __func__ << ": Failed to call EVP_DigestSignUpdate: "
               << GetOpenSSLError();
    return {};
  }
  size_t output_length = 0;
  if (EVP_DigestSignFinal(mdctx.get(), nullptr, &output_length) != 1) {
    LOG(ERROR)
        << __func__
        << ": Failed to call EVP_DigestSignFinal to get signature length: "
        << GetOpenSSLError();
    return {};
  }
  std::unique_ptr<unsigned char[]> output =
      std::make_unique<unsigned char[]>(output_length);
  if (EVP_DigestSignFinal(mdctx.get(), output.get(), &output_length) != 1) {
    LOG(ERROR) << __func__ << ": Failed to call EVP_DigestVerifyFinal: "
               << GetOpenSSLError();
    return {};
  }
  return std::string(output.get(), output.get() + output_length);
}

bool EVPDigestVerify(const crypto::ScopedEVP_PKEY& key,
                     const EVP_MD* md_type,
                     const std::string& data,
                     const std::string& signature) {
  CHECK(key.get());
  CHECK(md_type != nullptr);

  crypto::ScopedEVP_MD_CTX mdctx(EVP_MD_CTX_new());
  if (!mdctx) {
    LOG(ERROR) << __func__
               << ": Failed to allocate EVP_MD_CTX: " << GetOpenSSLError();
    return false;
  }

  if (EVP_DigestVerifyInit(mdctx.get(), nullptr, md_type, nullptr, key.get()) !=
      1) {
    LOG(ERROR) << __func__ << ": Failed to call EVP_DigestVerifyInit: "
               << GetOpenSSLError();
    return false;
  }
  if (EVP_DigestVerifyUpdate(mdctx.get(), data.data(), data.length()) != 1) {
    LOG(ERROR) << __func__ << ": Failed to call EVP_DigestVerifyUpdate: "
               << GetOpenSSLError();
    return false;
  }

  if (EVP_DigestVerifyFinal(
          mdctx.get(), reinterpret_cast<const unsigned char*>(signature.data()),
          signature.length()) != 1) {
    LOG(ERROR) << __func__ << ": Failed to call EVP_DigestVerifyFinal: "
               << GetOpenSSLError();
    return false;
  }
  return true;
}

}  // namespace hwsec_test_utils
