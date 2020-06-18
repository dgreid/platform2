// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webservd/encryptor.h"

#include <memory>

#include <brillo/data_encoding.h>

namespace webservd {

// Encryptor which simply base64-encodes the plaintext to get the
// ciphertext.  Obviously, this should be used only for testing.
class FakeEncryptor : public Encryptor {
 public:
  bool EncryptWithAuthentication(const std::string& plaintext,
                                 std::string* ciphertext) override {
    *ciphertext = brillo::data_encoding::Base64Encode(plaintext);
    return true;
  }

  bool DecryptWithAuthentication(const std::string& ciphertext,
                                 std::string* plaintext) override {
    return brillo::data_encoding::Base64Decode(ciphertext, plaintext);
  }
};

std::unique_ptr<Encryptor> Encryptor::CreateDefaultEncryptor() {
  return std::unique_ptr<Encryptor>{new FakeEncryptor};
}

}  // namespace webservd
