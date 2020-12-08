// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/mock_mount.h"

using ::testing::Invoke;
using ::testing::Return;

namespace cryptohome {

MockMount::MockMount() {
  // We'll call the real method by default for pkcs11_state()
  ON_CALL(*this, pkcs11_state())
      .WillByDefault(Invoke(this, &MockMount::Real_pkcs11_state));
}

MockMount::~MockMount() {}

}  // namespace cryptohome
