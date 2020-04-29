// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWSEC_TEST_UTILS_COMMON_OPENSSL_UTILITY_H_
#define HWSEC_TEST_UTILS_COMMON_OPENSSL_UTILITY_H_

#include <string>

#include <base/optional.h>
#include <crypto/scoped_openssl_types.h>

namespace hwsec_test_utils {

// TODO(b/155150344): Use the libhwsec one after improving that implementation.
std::string GetOpenSSLError();

// Parses |pem| into |crypto::ScopedEVP_PKEY| . In case of failure, the returned
// object contains |nullptr|.
crypto::ScopedEVP_PKEY PemToEVP(const std::string& pem);

// Performs the sequence of EVP_DigestSign(Init|Update|Final) operations using
// |key| as the signing or HMAC key. Returns nullopt if any error; otherwise
// returns the signature or HMAC.
base::Optional<std::string> EVPDigestSign(const crypto::ScopedEVP_PKEY& key,
                                          const EVP_MD* md_type,
                                          const std::string& data);

}  // namespace hwsec_test_utils

#endif  // HWSEC_TEST_UTILS_COMMON_OPENSSL_UTILITY_H_
