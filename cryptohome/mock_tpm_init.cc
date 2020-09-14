// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_tpm_init.h"

using testing::_;
using testing::Return;

namespace cryptohome {

MockTpmInit::MockTpmInit() : TpmInit(NULL, NULL) {
  ON_CALL(*this, SetupTpm(_)).WillByDefault(Return(true));
  ON_CALL(*this, HasCryptohomeKey()).WillByDefault(Return(true));
  ON_CALL(*this, ShallInitialize()).WillByDefault(Return(false));
}

MockTpmInit::~MockTpmInit() {}

}  // namespace cryptohome
