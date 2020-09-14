// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_vault_keyset.h"

namespace cryptohome {

using ::testing::Invoke;
using ::testing::Return;
using ::testing::ReturnRef;

MockVaultKeyset::MockVaultKeyset() {
  ON_CALL(*this, serialized()).WillByDefault(ReturnRef(stub_serialized_));
  ON_CALL(*this, mutable_serialized()).WillByDefault(Return(&stub_serialized_));
}

MockVaultKeyset::~MockVaultKeyset() {}

}  // namespace cryptohome
