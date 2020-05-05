// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/crypto_rot47.h"

using std::string;

namespace shill {

namespace {
constexpr char kId[] = "rot47";
}  // namespace

CryptoRot47::CryptoRot47() = default;

string CryptoRot47::GetId() const {
  return kId;
}

bool CryptoRot47::Encrypt(const string& plaintext, string* ciphertext) const {
  const int kRotSize = 94;
  const int kRotHalf = kRotSize / 2;
  const char kRotMin = '!';
  const char kRotMax = kRotMin + kRotSize - 1;

  *ciphertext = plaintext;
  for (auto& ch : *ciphertext) {
    if (ch < kRotMin || ch > kRotMax) {
      continue;
    }
    int rot = ch + kRotHalf;
    ch = (rot > kRotMax) ? rot - kRotSize : rot;
  }
  return true;
}

bool CryptoRot47::Decrypt(const string& ciphertext, string* plaintext) const {
  // ROT47 is self-reciprocal.
  return Encrypt(ciphertext, plaintext);
}

}  // namespace shill
