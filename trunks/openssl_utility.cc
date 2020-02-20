// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/openssl_utility.h"

#include <base/logging.h>
#include <libhwsec/crypto_utility.h>
#include <openssl/bn.h>
#include <openssl/ec.h>

#include "trunks/tpm_generated.h"

namespace {

// Converts an ECC point |coordinate| in the OpenSSL BIGNUM format to the
// TPM2B_ECC_PARAMETER format. If succeeded, stores the result in |param| and
// returns true; otherwise, returns false.
bool BignumCoordinateToEccParameter(
    const BIGNUM* coordinate, trunks::TPM2B_ECC_PARAMETER* param) {
  int key_size = BN_num_bytes(coordinate);
  if (key_size > MAX_ECC_KEY_BYTES) {
    LOG(ERROR) << "Bad coordinate size: " << key_size;
    return false;
  }

  if (BN_bn2bin(coordinate,
                reinterpret_cast<unsigned char*>(param->buffer)) != key_size) {
    LOG(ERROR) << "BN_bn2bin() doesn't write a correct size: "
               << hwsec::GetOpensslError();
    return false;
  }

  param->size = key_size;
  return true;
}

}  // namespace

namespace trunks {

bool TpmToOpensslEccPoint(const TPMS_ECC_POINT& point,
                          const EC_GROUP* ec_group,
                          EC_POINT* ec_point) {
  DCHECK(ec_group && ec_point) << "Uninitialized input argument(s).";

  hwsec::ScopedBN_CTX ctx;
  BIGNUM* x = BN_CTX_get(ctx.get());
  BIGNUM* y = BN_CTX_get(ctx.get());
  if (!x || !y) {
    LOG(ERROR) << "Failed to create bignums for x or y when converting to "
               << "openssl ECC point: " << hwsec::GetOpensslError();
    return false;
  }

  if (!BN_bin2bn(reinterpret_cast<const unsigned char*>(point.x.buffer),
                 point.x.size, x) ||
      !BN_bin2bn(reinterpret_cast<const unsigned char*>(point.y.buffer),
                 point.y.size, y) ||
      !EC_POINT_set_affine_coordinates_GFp(
          ec_group, ec_point, x, y, ctx.get())) {
    LOG(ERROR) << "Failed to convert TPMS_ECC_POINT to OpenSSL EC_POINT: "
               << hwsec::GetOpensslError();
    return false;
  }

  return true;
}

bool OpensslToTpmEccPoint(const EC_GROUP* ec_group,
                          const EC_POINT* point,
                          TPMS_ECC_POINT* ecc_point) {
  DCHECK(ec_group && point && ecc_point) << "Uninitialized input argument(s).";

  hwsec::ScopedBN_CTX ctx;
  BIGNUM* x = BN_CTX_get(ctx.get());
  BIGNUM* y = BN_CTX_get(ctx.get());
  if (!x || !y) {
    LOG(ERROR) << "Failed to create bignums for x or y when converting to TPM "
               << "ECC point: " << hwsec::GetOpensslError();
    return false;
  }

  if (!EC_POINT_get_affine_coordinates_GFp(ec_group, point, x, y, ctx.get())) {
    LOG(ERROR) << "Failed to get X and Y from OpenSSL EC_POINT: "
               << hwsec::GetOpensslError();
    return false;
  }

  if (!BignumCoordinateToEccParameter(x, &ecc_point->x) ||
      !BignumCoordinateToEccParameter(y, &ecc_point->y)) {
    LOG(ERROR) << "Bad EC_POINT coordinate value.";
    return false;
  }

  return true;
}

}  // namespace trunks
