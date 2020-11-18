// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_UTIL_H_
#define U2FD_UTIL_H_

#include <algorithm>
#include <string>
#include <vector>

#include <base/optional.h>
#include <base/logging.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/sha.h>

namespace u2f {
namespace util {

//
// Utility functions for copying data to/from vector<uint8_t>.
//
//////////////////////////////////////////////////////////////////////

// Utility function to copy an object, as raw bytes, to a vector.
template <typename FromType>
void AppendToVector(const FromType& from, std::vector<uint8_t>* to) {
  const uint8_t* from_bytes = reinterpret_cast<const uint8_t*>(&from);
  std::copy(from_bytes, from_bytes + sizeof(from), std::back_inserter(*to));
}

// Specializations of above function for copying from vector and string.
template <>
void AppendToVector(const std::vector<uint8_t>& from, std::vector<uint8_t>* to);
template <>
void AppendToVector(const std::string& from, std::vector<uint8_t>* to);

// Utility function to transform a string to a vector.
std::vector<uint8_t> ToVector(const std::string& str);

// Utility function to copy bytes from a vector to an object. This is the
// inverse of AppendToVector.
template <typename VectorAllocator, typename ToType>
bool VectorToObject(const std::vector<uint8_t, VectorAllocator>& from,
                    ToType* to,
                    const size_t size) {
  if (size < from.size()) {
    return false;
  }
  memcpy(to, &from.front(), from.size());
  return true;
}

// Utility function to copy part of a string to a vector.
void AppendSubstringToVector(const std::string& from,
                             int start,
                             int length,
                             std::vector<uint8_t>* to);

//
// Crypto utilities.
//
//////////////////////////////////////////////////////////////////////

// Attempts to convert the specified ECDSA signature (specified as r and s
// values) to DER encoding; returns base::nullopt on error.
base::Optional<std::vector<uint8_t>> SignatureToDerBytes(const uint8_t* r,
                                                         const uint8_t* s);

// Returns the SHA-256 of the specified data.
template <typename Blob>
std::vector<uint8_t> Sha256(const Blob& data) {
  std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
  SHA256_CTX sha_context;

  SHA256_Init(&sha_context);
  SHA256_Update(&sha_context, &data.front(), data.size());
  SHA256_Final(&hash.front(), &sha_context);

  return hash;
}

// Creates a new EC key to use for U2F attestation.
crypto::ScopedEC_KEY CreateAttestationKey();

// Signs data using attestion_key, and returns the DER-encoded signature,
// or base::nullopt on error.
base::Optional<std::vector<uint8_t>> AttestToData(
    const std::vector<uint8_t>& data, EC_KEY* attestation_key);

// Returns an X509 certificate for the specified attestation_key, to be included
// in a U2F register response, or base::nullopt on error.
base::Optional<std::vector<uint8_t>> CreateAttestationCertificate(
    EC_KEY* attestation_key);

// Parses the specified certificate and re-serializes it to the same vector,
// removing any padding that was present.
bool RemoveCertificatePadding(std::vector<uint8_t>* cert);

}  // namespace util
}  // namespace u2f

#endif  // U2FD_UTIL_H_
