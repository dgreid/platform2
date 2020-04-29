// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWSEC_TEST_UTILS_COMMON_OPENSSL_UTILITY_H_
#define HWSEC_TEST_UTILS_COMMON_OPENSSL_UTILITY_H_

#include <string>

#include <base/optional.h>
#include <crypto/scoped_openssl_types.h>

namespace hwsec_test_utils {

// Initializes the possibly needed initialization for use of OpenSSL library. If
// it is called before, then performs no-ops. This function is by design not
// thread-safe though the calls, for currently we don't have the usecase.
void InitializeOpenSSL();

// TODO(b/155150344): Use the libhwsec one after improving that implementation.
std::string GetOpenSSLError();

// Parses |pem| into |crypto::ScopedEVP_PKEY| . In case of failure, the returned
// object contains |nullptr|.
crypto::ScopedEVP_PKEY PemToEVP(const std::string& pem);

// Generates random bytes with size of |length|. In case of failure, returns
// |base::nullopt|.
base::Optional<std::string> GetRandom(size_t length);

// Reads |pem| string and parse it to X509 object. In case of any error, the
// returned object contains |nullptr|.
crypto::ScopedX509 PemToX509(const std::string& pem);

// Performs the sequence of EVP_DigestSign(Init|Update|Final) operations using
// |key| as the signing or HMAC key. Returns nullopt if any error; otherwise
// returns the signature or HMAC.
base::Optional<std::string> EVPDigestSign(const crypto::ScopedEVP_PKEY& key,
                                          const EVP_MD* md_type,
                                          const std::string& data);

// Performs the sequence of EVP_DigestVerify(Init|Update|Final) operations using
// |key| as the signing key to verify |signature| against |data|. Returns |true|
// iff the signature is verified.
bool EVPDigestVerify(const crypto::ScopedEVP_PKEY& key,
                     const EVP_MD* md_type,
                     const std::string& data,
                     const std::string& signature);

// Performs the sequence of EVP_PKEY_decrypt(_init)? operations using |key| as
// the decryption key of a RSA key. |rsa_padding| is set after
// |EVP_PKEY_decrypt_init|.
base::Optional<std::string> EVPRsaDecrypt(const crypto::ScopedEVP_PKEY& key,
                                          const std::string& encrypted_data,
                                          int rsa_padding);

}  // namespace hwsec_test_utils

#endif  // HWSEC_TEST_UTILS_COMMON_OPENSSL_UTILITY_H_
