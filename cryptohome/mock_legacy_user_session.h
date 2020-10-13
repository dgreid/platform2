// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_LEGACY_USER_SESSION_H_
#define CRYPTOHOME_MOCK_LEGACY_USER_SESSION_H_

#include "cryptohome/legacy_user_session.h"

#include <string>

#include <base/logging.h>
#include <brillo/secure_blob.h>

#include <gmock/gmock.h>

namespace cryptohome {

class MockLegacyUserSession : public LegacyUserSession {
 public:
  MockLegacyUserSession();
  ~MockLegacyUserSession();
  MOCK_METHOD(void, Init, (const brillo::SecureBlob&), (override));
  MOCK_METHOD(bool, SetUser, (const Credentials&), (override));
  MOCK_METHOD(void, Reset, (), (override));
  MOCK_METHOD(bool, CheckUser, (const std::string&), (const, override));
  MOCK_METHOD(bool, Verify, (const Credentials&), (const, override));
  MOCK_METHOD(void, set_key_index, (int), (override));

 private:
  LegacyUserSession user_session_;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_LEGACY_USER_SESSION_H_
