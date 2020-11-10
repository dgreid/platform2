// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_MOCK_WEBAUTHN_STORAGE_H_
#define U2FD_MOCK_WEBAUTHN_STORAGE_H_

#include "u2fd/webauthn_storage.h"

#include <string>

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

namespace u2f {

class MockWebAuthnStorage : public WebAuthnStorage {
 public:
  MockWebAuthnStorage() = default;
  ~MockWebAuthnStorage() override = default;

  MOCK_METHOD(bool, WriteRecord, (const WebAuthnRecord& record), (override));

  MOCK_METHOD(bool, LoadRecords, (), (override));

  MOCK_METHOD(void, Reset, (), (override));

  MOCK_METHOD(base::Optional<brillo::SecureBlob>,
              GetSecretByCredentialId,
              (const std::string& credential_id),
              (override));

  MOCK_METHOD(base::Optional<WebAuthnRecord>,
              GetRecordByCredentialId,
              (const std::string& credential_id),
              (override));
};

}  // namespace u2f

#endif  // U2FD_MOCK_WEBAUTHN_STORAGE_H_
