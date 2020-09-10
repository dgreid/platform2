// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm1_static_utils.h"

#include <memory>

#include <base/logging.h>
#include <base/memory/free_deleter.h>
#include <base/strings/stringprintf.h>
#include <crypto/libcrypto-compat.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <trousers/trousers.h>

#include "cryptohome/cryptolib.h"

using brillo::Blob;

using ScopedByteArray = std::unique_ptr<BYTE, base::FreeDeleter>;

namespace cryptohome {

std::string FormatTrousersErrorCode(TSS_RESULT result) {
  return base::StringPrintf("TPM error 0x%x (%s)", result,
                            Trspi_Error_String(result));
}

crypto::ScopedRSA ParseRsaFromTpmPubkeyBlob(const Blob& pubkey) {
  // Parse the serialized TPM_PUBKEY.
  UINT64 offset = 0;
  BYTE* buffer = const_cast<BYTE*>(pubkey.data());
  TPM_PUBKEY parsed;
  TSS_RESULT tss_result =
      Trspi_UnloadBlob_PUBKEY_s(&offset, buffer, pubkey.size(), &parsed);
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Failed to parse TPM_PUBKEY: "
               << FormatTrousersErrorCode(tss_result);
    return nullptr;
  }
  ScopedByteArray scoped_key(parsed.pubKey.key);
  ScopedByteArray scoped_parms(parsed.algorithmParms.parms);
  if (offset != pubkey.size()) {
    LOG(ERROR) << "Found garbage data after the TPM_PUBKEY.";
    return nullptr;
  }
  TPM_RSA_KEY_PARMS parms;
  UINT64 parms_offset = 0;
  tss_result = Trspi_UnloadBlob_RSA_KEY_PARMS_s(
      &parms_offset, parsed.algorithmParms.parms,
      parsed.algorithmParms.parmSize, &parms);
  if (TPM_ERROR(tss_result)) {
    LOG(ERROR) << "Failed to parse RSA_KEY_PARMS: "
               << FormatTrousersErrorCode(tss_result);
    return nullptr;
  }
  if (parms_offset != parsed.algorithmParms.parmSize) {
    LOG(ERROR) << "Find garbage data after the TPM_PUBKEY.";
    return nullptr;
  }
  ScopedByteArray scoped_exponent(parms.exponent);
  // Get the public exponent.
  crypto::ScopedRSA rsa(RSA_new());
  crypto::ScopedBIGNUM e(BN_new()), n(BN_new());
  if (!rsa || !e || !n) {
    LOG(ERROR) << "Failed to create RSA or BIGNUM";
    return nullptr;
  }
  if (!parms.exponentSize) {
    if (!BN_set_word(e.get(), kWellKnownExponent)) {
      LOG(ERROR) << "Failed to set BN exponent to WellKnownExponent";
      return nullptr;
    }
  } else {
    if (!BN_bin2bn(parms.exponent, parms.exponentSize, e.get())) {
      LOG(ERROR) << "Failed to load BN exponent from TPM_PUBKEY";
      return nullptr;
    }
  }
  // Get the modulus.
  if (!BN_bin2bn(parsed.pubKey.key, parsed.pubKey.keyLength, n.get())) {
    LOG(ERROR) << "Failed to load BN modulus from TPM_PUBKEY";
    return nullptr;
  }

  if (!RSA_set0_key(rsa.get(), n.release(), e.release(), nullptr)) {
    LOG(ERROR) << "Failed to set parameters for RSA";
    return nullptr;
  }

  return rsa;
}

}  // namespace cryptohome
