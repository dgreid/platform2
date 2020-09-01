// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/crypto.h"

#include <string_view>
#include <utility>

#include <base/strings/string_util.h>

#include "shill/logging.h"

using std::string;

namespace shill {

namespace {

constexpr char kRot47Id[] = "rot47:";

// ROT47 is self-reciprocal.
string Rot47(std::string_view input) {
  const int kRotSize = 94;
  const int kRotHalf = kRotSize / 2;
  const char kRotMin = '!';
  const char kRotMax = kRotMin + kRotSize - 1;

  string output;
  output.reserve(input.size());
  for (auto ch : input) {
    if (kRotMin <= ch && ch <= kRotMax) {
      int rot = ch + kRotHalf;
      ch = (rot > kRotMax) ? rot - kRotSize : rot;
    }
    output.push_back(ch);
  }
  return output;
}

}  // namespace

namespace Crypto {

string Encrypt(const string& plaintext) {
  return string(kRot47Id) + Rot47(plaintext);
}

base::Optional<string> Decrypt(const string& ciphertext) {
  if (!base::StartsWith(ciphertext, kRot47Id, base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "Cannot decrypt non-ROT47 ciphertext";
    return base::nullopt;
  }

  std::string_view to_decrypt = ciphertext;
  to_decrypt.remove_prefix(sizeof(kRot47Id) - 1);
  return Rot47(std::move(to_decrypt));
}

}  // namespace Crypto

}  // namespace shill
