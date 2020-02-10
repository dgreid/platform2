// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymaster/context/chaps_client.h"

#include <vector>

#include <brillo/secure_blob.h>
#include <chaps/attributes.h>
#include <chaps/chaps_proxy_mock.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/keymaster/context/context_adaptor.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace arc {
namespace keymaster {
namespace context {

namespace {

// Arbitrary non-zero CK_SESSION_HANDLE.
constexpr uint64_t kSessionId = 9;
// Arbitrary single-element list.
const std::vector<uint64_t> kObjectList = {7};
const std::vector<uint64_t> kEmptyObjectList = {};

// Arbitrary 32 byte key.
const std::vector<uint8_t> kKeyBlob(32, 99);

}  // anonymous namespace

// Fixture for chaps client tests.
class ChapsClientTest : public ::testing::Test {
 public:
  ChapsClientTest()
      : chaps_mock_(/* is_initialized */ true),
        context_adaptor_(scoped_refptr<::dbus::Bus>()),
        chaps_client_(context_adaptor_.GetWeakPtr()) {}

  uint32_t FakeGetAttributeValue(const brillo::SecureBlob& isolate_credential,
                                 uint64_t session_id,
                                 uint64_t object_handle,
                                 const std::vector<uint8_t>& attributes_in,
                                 std::vector<uint8_t>* attributes_out) {
    chaps::Attributes parsed;
    parsed.Parse(attributes_in);
    parsed.attributes()[0].ulValueLen = kKeyBlob.size();
    if (parsed.attributes()[0].pValue) {
      memcpy(parsed.attributes()[0].pValue, kKeyBlob.data(), kKeyBlob.size());
    }
    parsed.Serialize(attributes_out);
    return CKR_OK;
  }

 protected:
  void SetUp() override {
    context_adaptor_.set_slot_for_tests(1);

    ON_CALL(chaps_mock_, OpenSession(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(kSessionId), Return(CKR_OK)));
    ON_CALL(chaps_mock_, CloseSession(_, _)).WillByDefault(Return(CKR_OK));
    ON_CALL(chaps_mock_, FindObjectsInit(_, _, _))
        .WillByDefault(Return(CKR_OK));
    ON_CALL(chaps_mock_, FindObjects(_, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<3>(kObjectList), Return(CKR_OK)));
    ON_CALL(chaps_mock_, FindObjectsFinal(_, _)).WillByDefault(Return(CKR_OK));
    ON_CALL(chaps_mock_, GetAttributeValue(_, _, _, _, _))
        .WillByDefault(Invoke(/* obj_ptr */ this,
                              &ChapsClientTest::FakeGetAttributeValue));
  }

  ::testing::NiceMock<::chaps::ChapsProxyMock> chaps_mock_;
  ContextAdaptor context_adaptor_;
  ChapsClient chaps_client_;
};

TEST_F(ChapsClientTest, UsesSlotFromAdaptor) {
  // Setup a fake slot in the cache.
  uint64_t slot = 42;
  context_adaptor_.set_slot_for_tests(slot);

  // Expect chaps client will use the given slot.
  EXPECT_CALL(chaps_mock_, OpenSession(_, Eq(slot), _, _));

  // Call an operation that triggers slot usage.
  chaps_client_.session_handle();
}

TEST_F(ChapsClientTest, ExportExistingEncryptionKey) {
  // An existing key is prepared in fixture setup, expect no generation happens.
  EXPECT_CALL(chaps_mock_, GenerateKey(_, _, _, _, _, _)).Times(0);

  // Call export key.
  base::Optional<brillo::SecureBlob> encryption_key =
      chaps_client_.ExportOrGenerateEncryptionKey();

  // Verify output.
  ASSERT_TRUE(encryption_key.has_value());
  std::vector<uint8_t> key(encryption_key->begin(), encryption_key->end());
  EXPECT_EQ(kKeyBlob, key);
}

TEST_F(ChapsClientTest, ExportGeneratedEncryptionKey) {
  // Expect no existing key is found and generation is called.
  EXPECT_CALL(chaps_mock_, FindObjects(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kEmptyObjectList), Return(CKR_OK)));
  EXPECT_CALL(chaps_mock_, GenerateKey(_, _, _, _, _, _))
      .WillOnce(Return(CKR_OK));

  // Call export key.
  base::Optional<brillo::SecureBlob> encryption_key =
      chaps_client_.ExportOrGenerateEncryptionKey();

  // Verify output.
  ASSERT_TRUE(encryption_key.has_value());
  std::vector<uint8_t> key(encryption_key->begin(), encryption_key->end());
  EXPECT_EQ(kKeyBlob, key);
}

TEST_F(ChapsClientTest, CachesExportedEncryptionKey) {
  // Expect chaps is queried only once.
  EXPECT_CALL(chaps_mock_, FindObjects(_, _, _, _)).Times(1);

  // Call export key.
  base::Optional<brillo::SecureBlob> encryption_key =
      chaps_client_.ExportOrGenerateEncryptionKey();

  // Verify exported key is cached in adaptor
  ASSERT_TRUE(context_adaptor_.encryption_key().has_value());
  std::vector<uint8_t> key(context_adaptor_.encryption_key()->begin(),
                           context_adaptor_.encryption_key()->end());
  EXPECT_EQ(kKeyBlob, key);

  // Verify exporting key again won't trigger more FindObject calls.
  for (int i = 0; i < 10; ++i)
    chaps_client_.ExportOrGenerateEncryptionKey();
}

TEST_F(ChapsClientTest, ReturnsCachedEncryptionKey) {
  // Prepare the adaptor cache with a key.
  brillo::SecureBlob in_key(kKeyBlob.begin(), kKeyBlob.end());
  context_adaptor_.set_encryption_key(in_key);

  // Expect chaps is never asked to find nor generate a key.
  EXPECT_CALL(chaps_mock_, FindObjects(_, _, _, _)).Times(0);
  EXPECT_CALL(chaps_mock_, GenerateKey(_, _, _, _, _, _)).Times(0);

  // Call export key.
  base::Optional<brillo::SecureBlob> encryption_key =
      chaps_client_.ExportOrGenerateEncryptionKey();

  // Verify exported key is what we prepared in adaptor cache.
  ASSERT_TRUE(encryption_key.has_value());
  std::vector<uint8_t> key(encryption_key->begin(), encryption_key->end());
  EXPECT_EQ(kKeyBlob, key);
}

TEST_F(ChapsClientTest, FindKeyHandlesInvalidSession) {
  // Expect a retry if FindObjects returns CKR_SESSION_HANDLE_INVALID.
  EXPECT_CALL(chaps_mock_, FindObjects(_, _, _, _))
      .WillOnce(Return(CKR_SESSION_HANDLE_INVALID))
      .WillOnce(Return(CKR_SESSION_HANDLE_INVALID))
      .WillOnce(DoAll(SetArgPointee<3>(kObjectList), Return(CKR_OK)));

  // Call export key.
  base::Optional<brillo::SecureBlob> encryption_key =
      chaps_client_.ExportOrGenerateEncryptionKey();

  // Verify key is exported successfully.
  ASSERT_TRUE(encryption_key.has_value());
  std::vector<uint8_t> key(encryption_key->begin(), encryption_key->end());
  EXPECT_EQ(kKeyBlob, key);
}

TEST_F(ChapsClientTest, GenerateKeyHandlesInvalidSession) {
  // Expect a retry if GenerateKey returns CKR_SESSION_HANDLE_INVALID.
  EXPECT_CALL(chaps_mock_, FindObjects(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<3>(kEmptyObjectList), Return(CKR_OK)));
  EXPECT_CALL(chaps_mock_, GenerateKey(_, _, _, _, _, _))
      .WillOnce(Return(CKR_SESSION_HANDLE_INVALID))
      .WillOnce(Return(CKR_SESSION_HANDLE_INVALID))
      .WillOnce(Return(CKR_OK));

  // Call export key.
  base::Optional<brillo::SecureBlob> encryption_key =
      chaps_client_.ExportOrGenerateEncryptionKey();

  // Verify output.
  ASSERT_TRUE(encryption_key.has_value());
  std::vector<uint8_t> key(encryption_key->begin(), encryption_key->end());
  EXPECT_EQ(kKeyBlob, key);
}

TEST_F(ChapsClientTest, GetAttributeHandlesInvalidSession) {
  // Expect a retry if GetAttribute returns CKR_SESSION_HANDLE_INVALID.
  EXPECT_CALL(chaps_mock_, GetAttributeValue(_, _, _, _, _))
      .WillOnce(Return(CKR_SESSION_HANDLE_INVALID))
      .WillOnce(Return(CKR_SESSION_HANDLE_INVALID))
      .WillRepeatedly(
          Invoke(/* obj_ptr */ this, &ChapsClientTest::FakeGetAttributeValue));

  // Call export key.
  base::Optional<brillo::SecureBlob> encryption_key =
      chaps_client_.ExportOrGenerateEncryptionKey();

  // Verify output.
  ASSERT_TRUE(encryption_key.has_value());
  std::vector<uint8_t> key(encryption_key->begin(), encryption_key->end());
  EXPECT_EQ(kKeyBlob, key);
}

}  // namespace context
}  // namespace keymaster
}  // namespace arc
