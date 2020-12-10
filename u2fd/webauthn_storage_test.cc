// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_storage.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace u2f {
namespace {

constexpr char kSanitizedUser[] = "SanitizedUser";

constexpr char kCredentialId[] = "CredentialId";
constexpr char kCredentialSecret[65] = {[0 ... 63] = 'E', '\0'};
constexpr char kRpId[] = "example.com";
constexpr char kUserId[] = "deadbeef";
constexpr char kUserDisplayName[] = "example_user";
constexpr double kCreatedTime = 12345;

brillo::Blob HexArrayToBlob(const char* array) {
  brillo::Blob blob;
  CHECK(base::HexStringToBytes(array, &blob));
  return blob;
}

using ::testing::_;
using ::testing::Return;

class WebAuthnStorageTest : public ::testing::Test {
 public:
  WebAuthnStorageTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    root_path_ =
        temp_dir_.GetPath().AppendASCII("webauthn_storage_unittest_root");
    webauthn_storage_ = std::make_unique<WebAuthnStorage>();
    // Since there is no session manager, allow accesses by default.
    webauthn_storage_->set_allow_access(true);
    webauthn_storage_->set_sanitized_user(kSanitizedUser);
    webauthn_storage_->SetRootPathForTesting(root_path_);
  }

  ~WebAuthnStorageTest() override {
    EXPECT_TRUE(base::DeletePathRecursively(temp_dir_.GetPath()));
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath root_path_;
  std::unique_ptr<WebAuthnStorage> webauthn_storage_;
};

TEST_F(WebAuthnStorageTest, WriteAndReadRecord) {
  const WebAuthnRecord record{kCredentialId,
                              HexArrayToBlob(kCredentialSecret),
                              kRpId,
                              kUserId,
                              kUserDisplayName,
                              kCreatedTime};

  EXPECT_TRUE(webauthn_storage_->WriteRecord(record));

  webauthn_storage_->Reset();
  webauthn_storage_->set_allow_access(true);
  webauthn_storage_->set_sanitized_user(kSanitizedUser);

  EXPECT_TRUE(webauthn_storage_->LoadRecords());

  base::Optional<WebAuthnRecord> record_loaded =
      webauthn_storage_->GetRecordByCredentialId(kCredentialId);
  EXPECT_TRUE(record_loaded);
  EXPECT_EQ(record.secret, record_loaded->secret);
  EXPECT_EQ(record.rp_id, record_loaded->rp_id);
  EXPECT_EQ(record.user_id, record_loaded->user_id);
  EXPECT_EQ(record.user_display_name, record_loaded->user_display_name);
  EXPECT_EQ(record.timestamp, record_loaded->timestamp);
}

TEST_F(WebAuthnStorageTest, WriteAndReadRecordWithEmptyUserIdAndDisplayName) {
  const WebAuthnRecord record{kCredentialId, HexArrayToBlob(kCredentialSecret),
                              kRpId,
                              std::string(),  // user_id
                              std::string(),  // user_display_name
                              kCreatedTime};

  EXPECT_TRUE(webauthn_storage_->WriteRecord(record));

  webauthn_storage_->Reset();
  webauthn_storage_->set_allow_access(true);
  webauthn_storage_->set_sanitized_user(kSanitizedUser);

  EXPECT_TRUE(webauthn_storage_->LoadRecords());

  base::Optional<WebAuthnRecord> record_loaded =
      webauthn_storage_->GetRecordByCredentialId(kCredentialId);
  EXPECT_TRUE(record_loaded);
  EXPECT_EQ(record.secret, record_loaded->secret);
  EXPECT_EQ(record.rp_id, record_loaded->rp_id);
  EXPECT_TRUE(record_loaded->user_id.empty());
  EXPECT_TRUE(record_loaded->user_display_name.empty());
  EXPECT_EQ(record.timestamp, record_loaded->timestamp);
}

TEST_F(WebAuthnStorageTest, LoadManyRecords) {
  for (int i = 0; i < 30; i++) {
    const WebAuthnRecord record{std::string(kCredentialId) + std::to_string(i),
                                HexArrayToBlob(kCredentialSecret),
                                kRpId,
                                kUserId,
                                kUserDisplayName,
                                kCreatedTime};

    EXPECT_TRUE(webauthn_storage_->WriteRecord(record));
  }

  webauthn_storage_->Reset();
  webauthn_storage_->set_allow_access(true);
  webauthn_storage_->set_sanitized_user(kSanitizedUser);

  EXPECT_TRUE(webauthn_storage_->LoadRecords());
}

}  // namespace
}  // namespace u2f
