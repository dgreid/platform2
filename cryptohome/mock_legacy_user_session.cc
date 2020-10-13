// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_legacy_user_session.h"

using testing::_;
using testing::Invoke;

namespace cryptohome {

MockLegacyUserSession::MockLegacyUserSession() {
  ON_CALL(*this, Init(_))
      .WillByDefault(Invoke(&user_session_, &LegacyUserSession::Init));
  ON_CALL(*this, SetUser(_))
      .WillByDefault(Invoke(&user_session_, &LegacyUserSession::SetUser));
  ON_CALL(*this, Reset())
      .WillByDefault(Invoke(&user_session_, &LegacyUserSession::Reset));
  ON_CALL(*this, CheckUser(_))
      .WillByDefault(Invoke(&user_session_, &LegacyUserSession::CheckUser));
  ON_CALL(*this, Verify(_))
      .WillByDefault(Invoke(&user_session_, &LegacyUserSession::Verify));
  ON_CALL(*this, set_key_index(_))
      .WillByDefault(Invoke(&user_session_, &LegacyUserSession::set_key_index));
}

MockLegacyUserSession::~MockLegacyUserSession() {}

}  // namespace cryptohome
