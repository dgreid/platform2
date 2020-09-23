// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/key_objects.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

namespace cryptohome {

brillo::SecureBlob LibScryptCompatKeyObjects::derived_key() {
  return derived_key_;
}

brillo::SecureBlob LibScryptCompatKeyObjects::ConsumeSalt() {
  CHECK(salt_ != base::nullopt);

  // The salt may not be re-used.
  brillo::SecureBlob value = salt_.value();
  salt_.reset();
  return value;
}

}  // namespace cryptohome
