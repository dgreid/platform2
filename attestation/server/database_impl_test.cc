// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "attestation/common/mock_crypto_utility.h"
#include "attestation/common/mock_tpm_utility.h"
#include "attestation/server/database_impl.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace {

const char kFakeCredential[] = "1234";

}  // namespace

namespace attestation {

class DatabaseImplTest : public testing::Test, public DatabaseIO {
 public:
  ~DatabaseImplTest() override = default;
  void SetUp() override {
    database_.reset(
        new DatabaseImpl(&mock_crypto_utility_, &mock_tpm_utility_));
    database_->set_io(this);
    InitializeFakeData();
    EXPECT_CALL(mock_tpm_utility_, IsPCR0Valid()).WillRepeatedly(Return(true));
    database_->Initialize();
  }

  // Fake DatabaseIO::Read.
  bool Read(std::string* data) override {
    if (fake_persistent_data_readable_) {
      *data = fake_persistent_data_;
    }
    return fake_persistent_data_readable_;
  }

  // Fake DatabaseIO::Write.
  bool Write(const std::string& data) override {
    if (fake_persistent_data_writable_) {
      fake_persistent_data_ = data;
    }
    return fake_persistent_data_writable_;
  }

  // Initializes fake_persistent_data_ with a default value.
  void InitializeFakeData() {
    AttestationDatabase proto;
    proto.mutable_credentials()->set_endorsement_public_key(kFakeCredential);
    proto.SerializeToString(&fake_persistent_data_);
  }

 protected:
  std::string fake_persistent_data_;
  bool fake_persistent_data_readable_{true};
  bool fake_persistent_data_writable_{true};
  NiceMock<MockCryptoUtility> mock_crypto_utility_;
  NiceMock<MockTpmUtility> mock_tpm_utility_;
  std::unique_ptr<DatabaseImpl> database_;
};

TEST_F(DatabaseImplTest, ReadSuccess) {
  database_->GetMutableProtobuf()->Clear();
  EXPECT_TRUE(database_->Reload());
  EXPECT_EQ(std::string(kFakeCredential),
            database_->GetProtobuf().credentials().endorsement_public_key());
}

TEST_F(DatabaseImplTest, ReadFailure) {
  fake_persistent_data_readable_ = false;
  database_->GetMutableProtobuf()->Clear();
  EXPECT_FALSE(database_->Reload());
  EXPECT_FALSE(database_->GetProtobuf().has_credentials());
}

TEST_F(DatabaseImplTest, DecryptFailure) {
  EXPECT_CALL(mock_crypto_utility_, DecryptData(_, _, _))
      .WillRepeatedly(Return(false));
  database_->GetMutableProtobuf()->Clear();
  EXPECT_FALSE(database_->Reload());
  EXPECT_FALSE(database_->GetProtobuf().has_credentials());
}

TEST_F(DatabaseImplTest, WriteSuccess) {
  database_->GetMutableProtobuf()
      ->mutable_credentials()
      ->set_endorsement_credential("test");
  std::string expected_data;
  database_->GetProtobuf().SerializeToString(&expected_data);
  EXPECT_TRUE(database_->SaveChanges());
  EXPECT_EQ(expected_data, fake_persistent_data_);
}

TEST_F(DatabaseImplTest, WriteFailure) {
  fake_persistent_data_writable_ = false;
  database_->GetMutableProtobuf()
      ->mutable_credentials()
      ->set_endorsement_credential("test");
  EXPECT_FALSE(database_->SaveChanges());
}

TEST_F(DatabaseImplTest, EncryptFailure) {
  EXPECT_CALL(mock_crypto_utility_, EncryptData(_, _, _, _))
      .WillRepeatedly(Return(false));
  database_->GetMutableProtobuf()
      ->mutable_credentials()
      ->set_endorsement_credential("test");
  EXPECT_FALSE(database_->SaveChanges());
}

TEST_F(DatabaseImplTest, IgnoreLegacyEncryptJunk) {
  // Legacy encryption scheme appended a SHA-1 hash before encrypting.
  fake_persistent_data_ += std::string(20, 'A');
  EXPECT_EQ(std::string(kFakeCredential),
            database_->GetProtobuf().credentials().endorsement_public_key());
}

TEST_F(DatabaseImplTest, Reload) {
  AttestationDatabase proto;
  proto.mutable_credentials()->set_endorsement_credential(kFakeCredential);
  proto.SerializeToString(&fake_persistent_data_);
  EXPECT_EQ(std::string(),
            database_->GetProtobuf().credentials().endorsement_credential());
  EXPECT_TRUE(database_->Reload());
  EXPECT_EQ(std::string(kFakeCredential),
            database_->GetProtobuf().credentials().endorsement_credential());
}

}  // namespace attestation
