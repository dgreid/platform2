// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/slot_manager_impl.h"

#include <map>
#include <memory>
#include <string>

#include <base/bind.h>
#include <base/callback.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/sha.h>

#include "chaps/chaps_factory_mock.h"
#include "chaps/chaps_utility.h"
#include "chaps/isolate.h"
#include "chaps/object_importer_mock.h"
#include "chaps/object_pool_mock.h"
#include "chaps/object_store_mock.h"
#include "chaps/session_mock.h"
#include "chaps/tpm_utility_mock.h"

using base::FilePath;
using brillo::SecureBlob;
using std::string;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;

namespace chaps {

namespace {

const char kAuthData[] = "000000";
const char kNewAuthData[] = "111111";
const char kDefaultPubExp[] = {1, 0, 1};
const int kDefaultPubExpSize = 3;
const char kTokenLabel[] = "test_label";

SecureBlob MakeBlob(const char* auth_data_str) {
  return Sha1(SecureBlob(auth_data_str));
}

// Creates and sets default expectations on a ObjectPoolMock instance. Returns
// a pointer to the new object.
ObjectPool* CreateObjectPoolMock() {
  ObjectPoolMock* object_pool = new ObjectPoolMock();
  EXPECT_CALL(*object_pool, GetInternalBlob(kEncryptedAuthKey, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(string("auth_key_blob")), Return(true)));
  EXPECT_CALL(*object_pool, GetInternalBlob(kEncryptedMasterKey, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(string("encrypted_master_key")),
                            Return(true)));
  EXPECT_CALL(*object_pool, GetInternalBlob(kImportedTracker, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(string()), Return(false)));
  EXPECT_CALL(*object_pool, GetInternalBlob(kAuthDataHash, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(string("\x01\xCE")), Return(true)));
  EXPECT_CALL(*object_pool,
              SetInternalBlob(kEncryptedAuthKey, string("auth_key_blob")))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*object_pool,
              SetInternalBlob(kEncryptedAuthKey, string("new_auth_key_blob")))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*object_pool, SetInternalBlob(kEncryptedMasterKey,
                                            string("encrypted_master_key")))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*object_pool, SetInternalBlob(kImportedTracker, string()))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*object_pool, SetInternalBlob(kAuthDataHash, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*object_pool, SetEncryptionKey(_)).WillRepeatedly(Return(true));
  return object_pool;
}

// Sets default expectations on a TPMUtilityMock.
void ConfigureTPMUtility(TPMUtilityMock* tpm) {
  EXPECT_CALL(*tpm, Init()).WillRepeatedly(Return(true));
  EXPECT_CALL(*tpm, UnloadKeysForSlot(_)).Times(AnyNumber());
  EXPECT_CALL(
      *tpm, Authenticate(_, Sha1(MakeBlob(kAuthData)), string("auth_key_blob"),
                         string("encrypted_master_key"), _))
      .WillRepeatedly(
          DoAll(SetArgPointee<4>(MakeBlob("master_key")), Return(true)));
  EXPECT_CALL(*tpm, ChangeAuthData(_, Sha1(MakeBlob(kAuthData)),
                                   Sha1(MakeBlob(kNewAuthData)),
                                   string("auth_key_blob"), _))
      .WillRepeatedly(
          DoAll(SetArgPointee<4>(string("new_auth_key_blob")), Return(true)));
  EXPECT_CALL(*tpm, GenerateRandom(_, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(string("master_key")), Return(true)));
  string exponent(kDefaultPubExp, kDefaultPubExpSize);
  EXPECT_CALL(*tpm,
              GenerateRSAKey(1, 2048, exponent, MakeBlob(kAuthData), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<4>(string("auth_key_blob")),
                            SetArgPointee<5>(1), Return(true)));
  EXPECT_CALL(*tpm, Bind(1, string("master_key"), _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(string("encrypted_master_key")),
                            Return(true)));
  EXPECT_CALL(*tpm, IsSRKReady()).WillRepeatedly(Return(true));
  EXPECT_CALL(*tpm, IsTPMAvailable()).WillRepeatedly(Return(true));
}

// Creates and returns a mock Session instance.
Session* CreateNewSession() {
  return new SessionMock();
}

}  // namespace

// A test fixture for an initialized SlotManagerImpl instance.
class TestSlotManager : public ::testing::Test {
 public:
  TestSlotManager() {
    EXPECT_CALL(factory_, CreateSession(_, _, _, _, _))
        .WillRepeatedly(InvokeWithoutArgs(CreateNewSession));
    ObjectStore* null_store = NULL;
    EXPECT_CALL(factory_, CreateObjectStore(_))
        .WillRepeatedly(Return(null_store));
    ObjectImporter* null_importer = NULL;
    EXPECT_CALL(factory_, CreateObjectImporter(_, _, _))
        .WillRepeatedly(Return(null_importer));
    ic_ = IsolateCredentialManager::GetDefaultIsolateCredential();
  }
  void SetUp() {
    EXPECT_CALL(factory_, CreateObjectPool(_, _, _))
        .WillRepeatedly(InvokeWithoutArgs(CreateObjectPoolMock));
    ConfigureTPMUtility(&tpm_);
    slot_manager_.reset(new SlotManagerImpl(&factory_, &tpm_, false, nullptr));
    ASSERT_TRUE(slot_manager_->Init());
  }
  void TearDown() {
    // Destroy the slot manager before its dependencies.
    slot_manager_.reset();
  }
#if GTEST_IS_THREADSAFE
  int InsertToken() {
    int slot_id = 0;
    slot_manager_->LoadToken(ic_, FilePath("/var/lib/chaps"),
                             MakeBlob(kAuthData), kTokenLabel, &slot_id);
    return slot_id;
  }
#endif

 protected:
  ChapsFactoryMock factory_;
  TPMUtilityMock tpm_;
  std::unique_ptr<SlotManagerImpl> slot_manager_;
  SecureBlob ic_;
};

typedef TestSlotManager TestSlotManager_DeathTest;
TEST(DeathTest, InvalidInit) {
  ChapsFactoryMock factory;
  EXPECT_DEATH_IF_SUPPORTED(new SlotManagerImpl(&factory, NULL, false, nullptr),
                            "Check failed");
  TPMUtilityMock tpm;
  EXPECT_DEATH_IF_SUPPORTED(new SlotManagerImpl(NULL, &tpm, false, nullptr),
                            "Check failed");
}

TEST_F(TestSlotManager_DeathTest, InvalidArgs) {
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->IsTokenPresent(ic_, 3),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->GetSlotInfo(ic_, 0, NULL),
                            "Check failed");
  CK_SLOT_INFO slot_info;
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->GetSlotInfo(ic_, 3, &slot_info),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->GetTokenInfo(ic_, 0, NULL),
                            "Check failed");
  CK_TOKEN_INFO token_info;
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->GetTokenInfo(ic_, 3, &token_info),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->GetMechanismInfo(ic_, 3),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->OpenSession(ic_, 3, false),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->CloseAllSessions(ic_, 3),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->GetSession(ic_, 0, NULL),
                            "Check failed");
}

TEST_F(TestSlotManager_DeathTest, OutOfMemorySession) {
  Session* null_session = NULL;
  EXPECT_CALL(factory_, CreateSession(_, _, _, _, _))
      .WillRepeatedly(Return(null_session));
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->OpenSession(ic_, 0, false),
                            "Check failed");
}

TEST_F(TestSlotManager_DeathTest, NoToken) {
  CK_TOKEN_INFO token_info;
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->GetTokenInfo(ic_, 1, &token_info),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->GetMechanismInfo(ic_, 1),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->OpenSession(ic_, 1, false),
                            "Check failed");
}

TEST_F(TestSlotManager, DefaultSlotSetup) {
  EXPECT_EQ(2, slot_manager_->GetSlotCount());
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, 0));
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, 1));
}

#if GTEST_IS_THREADSAFE

TEST(DeathTest, OutOfMemoryInit) {
  TPMUtilityMock tpm;
  ConfigureTPMUtility(&tpm);
  ChapsFactoryMock factory;
  ObjectPool* null_pool = NULL;
  EXPECT_CALL(factory, CreateObjectPool(_, _, _))
      .WillRepeatedly(Return(null_pool));
  ObjectStore* null_store = NULL;
  EXPECT_CALL(factory, CreateObjectStore(_)).WillRepeatedly(Return(null_store));
  ObjectImporter* null_importer = NULL;
  EXPECT_CALL(factory, CreateObjectImporter(_, _, _))
      .WillRepeatedly(Return(null_importer));
  SlotManagerImpl sm(&factory, &tpm, false, nullptr);
  ASSERT_TRUE(sm.Init());
  int slot_id;
  EXPECT_DEATH_IF_SUPPORTED(
      sm.LoadToken(IsolateCredentialManager::GetDefaultIsolateCredential(),
                   FilePath("/var/lib/chaps"), MakeBlob(kAuthData), kTokenLabel,
                   &slot_id),
      "Check failed");
}

TEST_F(TestSlotManager, QueryInfo) {
  InsertToken();
  CK_SLOT_INFO slot_info;
  memset(&slot_info, 0xEE, sizeof(slot_info));
  slot_manager_->GetSlotInfo(ic_, 0, &slot_info);
  // Check if all bytes have been set by the call.
  EXPECT_EQ(NULL, memchr(&slot_info, 0xEE, sizeof(slot_info)));
  CK_TOKEN_INFO token_info;
  memset(&token_info, 0xEE, sizeof(token_info));
  slot_manager_->GetTokenInfo(ic_, 0, &token_info);
  EXPECT_EQ(NULL, memchr(&token_info, 0xEE, sizeof(token_info)));
  string expected_label(kTokenLabel);
  expected_label.resize(base::size(token_info.label), ' ');
  string actual_label(reinterpret_cast<char*>(token_info.label),
                      base::size(token_info.label));
  EXPECT_EQ(expected_label, actual_label);
  const MechanismMap* mechanisms = slot_manager_->GetMechanismInfo(ic_, 0);
  ASSERT_TRUE(mechanisms != NULL);
  // Sanity check - we don't want to be strict on the mechanism list.
  EXPECT_TRUE(mechanisms->end() != mechanisms->find(CKM_RSA_PKCS));
  EXPECT_TRUE(mechanisms->end() != mechanisms->find(CKM_AES_CBC));
}

TEST_F(TestSlotManager, TestSessions) {
  InsertToken();
  int id1 = slot_manager_->OpenSession(ic_, 0, false);
  int id2 = slot_manager_->OpenSession(ic_, 0, true);
  EXPECT_NE(id1, id2);
  Session* s1 = NULL;
  EXPECT_TRUE(slot_manager_->GetSession(ic_, id1, &s1));
  EXPECT_TRUE(s1 != NULL);
  Session* s2 = NULL;
  EXPECT_TRUE(slot_manager_->GetSession(ic_, id2, &s2));
  EXPECT_TRUE(s2 != NULL);
  EXPECT_NE(s1, s2);
  EXPECT_TRUE(slot_manager_->CloseSession(ic_, id1));
  EXPECT_FALSE(slot_manager_->CloseSession(ic_, id1));
  slot_manager_->CloseAllSessions(ic_, 0);
  EXPECT_FALSE(slot_manager_->CloseSession(ic_, id2));
}

TEST_F(TestSlotManager, TestLoadTokenEvents) {
  InsertToken();
  int slot_id;
  EXPECT_TRUE(slot_manager_->LoadToken(
      ic_, FilePath("some_path"), MakeBlob(kAuthData), kTokenLabel, &slot_id));
  EXPECT_TRUE(slot_manager_->IsTokenPresent(ic_, 1));
  // Load token with an existing path - should not result in a new slot.
  int slot_id2;
  EXPECT_TRUE(slot_manager_->LoadToken(
      ic_, FilePath("some_path"), MakeBlob(kAuthData), kTokenLabel, &slot_id2));
  EXPECT_EQ(slot_id, slot_id2);
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, FilePath("another_path"),
                                       MakeBlob(kAuthData), kTokenLabel,
                                       &slot_id));
  EXPECT_TRUE(slot_manager_->IsTokenPresent(ic_, 2));
  slot_manager_->ChangeTokenAuthData(FilePath("some_path"), MakeBlob(kAuthData),
                                     MakeBlob(kNewAuthData));
  slot_manager_->ChangeTokenAuthData(FilePath("yet_another_path"),
                                     MakeBlob(kAuthData),
                                     MakeBlob(kNewAuthData));
  // Logout with an unknown path.
  slot_manager_->UnloadToken(ic_, FilePath("still_yet_another_path"));
  slot_manager_->UnloadToken(ic_, FilePath("some_path"));
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, 1));
  EXPECT_TRUE(slot_manager_->IsTokenPresent(ic_, 2));
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, FilePath("one_more_path"),
                                       MakeBlob(kAuthData), kTokenLabel,
                                       &slot_id));
  EXPECT_TRUE(slot_manager_->IsTokenPresent(ic_, 1));
  slot_manager_->UnloadToken(ic_, FilePath("another_path"));
}

TEST_F(TestSlotManager, ManyLoadToken) {
  InsertToken();
  for (int i = 0; i < 100; ++i) {
    string path = base::StringPrintf("test%d", i);
    int slot_id = 0;
    slot_manager_->LoadToken(ic_, FilePath(path), MakeBlob(kAuthData),
                             kTokenLabel, &slot_id);
    slot_manager_->ChangeTokenAuthData(FilePath(path), MakeBlob(kAuthData),
                                       MakeBlob(kNewAuthData));
    slot_manager_->ChangeTokenAuthData(
        FilePath(path + "_"), MakeBlob(kAuthData), MakeBlob(kNewAuthData));
  }
  for (int i = 0; i < 100; ++i) {
    string path = base::StringPrintf("test%d", i);
    slot_manager_->UnloadToken(ic_, FilePath(path));
  }
}

TEST_F(TestSlotManager, TestDefaultIsolate) {
  // Check default isolate is there by default.
  SecureBlob defaultIsolate =
      IsolateCredentialManager::GetDefaultIsolateCredential();
  bool new_isolate = true;
  EXPECT_TRUE(slot_manager_->OpenIsolate(&defaultIsolate, &new_isolate));
  EXPECT_FALSE(new_isolate);
  EXPECT_EQ(IsolateCredentialManager::GetDefaultIsolateCredential(),
            defaultIsolate);
}

TEST_F(TestSlotManager, TestOpenIsolate) {
  EXPECT_CALL(tpm_, GenerateRandom(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(string("567890")), Return(true)));

  // Check that trying to open an invalid isolate creates new isolate.
  SecureBlob isolate("invalid");
  bool new_isolate_created = false;
  EXPECT_TRUE(slot_manager_->OpenIsolate(&isolate, &new_isolate_created));
  EXPECT_TRUE(new_isolate_created);
  EXPECT_EQ(SecureBlob(string("567890")), isolate);

  // Check opening an existing isolate.
  EXPECT_TRUE(slot_manager_->OpenIsolate(&isolate, &new_isolate_created));
  EXPECT_FALSE(new_isolate_created);
  EXPECT_EQ(SecureBlob(string("567890")), isolate);
}

TEST_F(TestSlotManager, TestCloseIsolate) {
  EXPECT_CALL(tpm_, GenerateRandom(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(string("abcdef")), Return(true)))
      .WillOnce(DoAll(SetArgPointee<1>(string("ghijkl")), Return(true)));

  SecureBlob isolate;
  bool new_isolate_created;
  EXPECT_TRUE(slot_manager_->OpenIsolate(&isolate, &new_isolate_created));
  EXPECT_TRUE(new_isolate_created);
  EXPECT_EQ(SecureBlob(string("abcdef")), isolate);
  EXPECT_TRUE(slot_manager_->OpenIsolate(&isolate, &new_isolate_created));
  EXPECT_FALSE(new_isolate_created);
  EXPECT_EQ(SecureBlob(string("abcdef")), isolate);
  slot_manager_->CloseIsolate(isolate);
  slot_manager_->CloseIsolate(isolate);
  // Final logout, isolate should now be destroyed.
  EXPECT_TRUE(slot_manager_->OpenIsolate(&isolate, &new_isolate_created));
  EXPECT_TRUE(new_isolate_created);
  EXPECT_EQ(SecureBlob(string("ghijkl")), isolate);
}

TEST_F(TestSlotManager, TestCloseIsolateUnloadToken) {
  SecureBlob isolate;
  bool new_isolate_created;
  EXPECT_TRUE(slot_manager_->OpenIsolate(&isolate, &new_isolate_created));
  EXPECT_TRUE(new_isolate_created);
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(isolate, 0));
  int slot_id;
  EXPECT_TRUE(slot_manager_->LoadToken(isolate, FilePath("some_path"),
                                       MakeBlob(kAuthData), kTokenLabel,
                                       &slot_id));
  EXPECT_TRUE(slot_manager_->IsTokenPresent(isolate, 0));
  // Token should be unloaded by CloseIsolate call.
  slot_manager_->CloseIsolate(isolate);
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(isolate, 0));
}

TEST_F(TestSlotManager_DeathTest, TestIsolateTokens) {
  CK_SLOT_INFO slot_info;
  CK_TOKEN_INFO token_info;
  Session* session;
  SecureBlob new_isolate_0, new_isolate_1;
  SecureBlob defaultIsolate =
      IsolateCredentialManager::GetDefaultIsolateCredential();

  // Ensure different credentials are created for each isolate.
  EXPECT_CALL(tpm_, GenerateRandom(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(string("123456")), Return(true)))
      .WillOnce(DoAll(SetArgPointee<1>(string("567890")), Return(true)));

  bool new_isolate_created;
  int slot_id;
  ASSERT_TRUE(slot_manager_->OpenIsolate(&new_isolate_0, &new_isolate_created));
  ASSERT_TRUE(new_isolate_created);
  ASSERT_TRUE(slot_manager_->LoadToken(new_isolate_0, FilePath("new_isolate"),
                                       MakeBlob(kAuthData), kTokenLabel,
                                       &slot_id));

  ASSERT_TRUE(slot_manager_->OpenIsolate(&new_isolate_1, &new_isolate_created));
  ASSERT_TRUE(new_isolate_created);
  ASSERT_TRUE(
      slot_manager_->LoadToken(new_isolate_1, FilePath("another_new_isolate"),
                               MakeBlob(kAuthData), kTokenLabel, &slot_id));
  // Ensure tokens are only accessible with the valid isolate cred.
  ASSERT_TRUE(slot_manager_->IsTokenAccessible(new_isolate_0, 0));
  ASSERT_TRUE(slot_manager_->IsTokenAccessible(new_isolate_1, 1));
  ASSERT_FALSE(slot_manager_->IsTokenAccessible(new_isolate_1, 0));
  ASSERT_FALSE(slot_manager_->IsTokenAccessible(new_isolate_0, 1));
  ASSERT_FALSE(slot_manager_->IsTokenAccessible(defaultIsolate, 0));
  ASSERT_FALSE(slot_manager_->IsTokenAccessible(defaultIsolate, 1));

  // Check all public methods perform isolate checks.
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->IsTokenPresent(new_isolate_0, 1),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(
      slot_manager_->GetSlotInfo(new_isolate_0, 1, &slot_info), "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(
      slot_manager_->GetTokenInfo(new_isolate_0, 1, &token_info),
      "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->GetMechanismInfo(new_isolate_0, 1),
                            "Check failed");
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->OpenSession(new_isolate_0, 1, false),
                            "Check failed");
  int slot_1_session = slot_manager_->OpenSession(new_isolate_1, 1, false);
  EXPECT_TRUE(
      slot_manager_->GetSession(new_isolate_1, slot_1_session, &session));
  EXPECT_FALSE(
      slot_manager_->GetSession(new_isolate_0, slot_1_session, &session));
  EXPECT_FALSE(slot_manager_->CloseSession(new_isolate_0, slot_1_session));
  EXPECT_DEATH_IF_SUPPORTED(slot_manager_->CloseAllSessions(new_isolate_0, 1),
                            "Check failed");
}

TEST_F(TestSlotManager, SRKNotReady) {
  EXPECT_CALL(tpm_, IsSRKReady()).WillRepeatedly(Return(false));
  slot_manager_.reset(new SlotManagerImpl(&factory_, &tpm_, false, nullptr));
  ASSERT_TRUE(slot_manager_->Init());

  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, 0));
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, 1));
  int slot_id = 0;
  EXPECT_FALSE(slot_manager_->LoadToken(
      ic_, FilePath("test_token"), MakeBlob(kAuthData), kTokenLabel, &slot_id));
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, 0));
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, 1));
}

TEST_F(TestSlotManager, DelayedSRKInit) {
  EXPECT_CALL(tpm_, IsSRKReady()).WillRepeatedly(Return(false));
  slot_manager_.reset(new SlotManagerImpl(&factory_, &tpm_, false, nullptr));
  ASSERT_TRUE(slot_manager_->Init());

  EXPECT_CALL(tpm_, IsSRKReady()).WillRepeatedly(Return(true));
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(
      ic_, FilePath("test_token"), MakeBlob(kAuthData), kTokenLabel, &slot_id));
}
#endif

class SoftwareOnlyTest : public TestSlotManager {
 public:
  SoftwareOnlyTest()
      : kTestTokenPath("sw_test_token"),
        set_encryption_key_num_calls_(0),
        delete_all_num_calls_(0),
        pool_write_result_(true) {}
  ~SoftwareOnlyTest() override {}

  void SetUp() override {
    // Use our own ObjectPoolFactory.
    EXPECT_CALL(factory_, CreateObjectPool(_, _, _))
        .WillRepeatedly(
            InvokeWithoutArgs(this, &SoftwareOnlyTest::ObjectPoolFactory));
    EXPECT_CALL(no_tpm_, IsTPMAvailable()).WillRepeatedly(Return(false));
    slot_manager_.reset(
        new SlotManagerImpl(&factory_, &no_tpm_, false, nullptr));
    ASSERT_TRUE(slot_manager_->Init());
  }

  void TearDown() override {
    // Destroy the slot manager before its dependencies.
    slot_manager_.reset();
  }

  ObjectPoolMock* ObjectPoolFactory() {
    // Redirect internal blob stuff to fake methods.
    ObjectPoolMock* object_pool = new ObjectPoolMock();
    EXPECT_CALL(*object_pool, GetInternalBlob(_, _))
        .WillRepeatedly(Invoke(this, &SoftwareOnlyTest::FakeGetInternalBlob));
    EXPECT_CALL(*object_pool, SetInternalBlob(_, _))
        .WillRepeatedly(Invoke(this, &SoftwareOnlyTest::FakeSetInternalBlob));
    EXPECT_CALL(*object_pool, SetEncryptionKey(_))
        .WillRepeatedly(Invoke(this, &SoftwareOnlyTest::FakeSetEncryptionKey));
    EXPECT_CALL(*object_pool, DeleteAll())
        .WillRepeatedly(Invoke(this, &SoftwareOnlyTest::FakeDeleteAll));
    return object_pool;
  }

  void InitializeObjectPoolBlobs() {
    // The easiest way is to load / unload a token and let the SlotManager do
    // the crypto.
    pool_blobs_.clear();
    int slot_id = 0;
    ASSERT_TRUE(slot_manager_->LoadToken(
        ic_, kTestTokenPath, MakeBlob(kAuthData), kTokenLabel, &slot_id));
    slot_manager_->UnloadToken(ic_, kTestTokenPath);
    set_encryption_key_num_calls_ = 0;
    delete_all_num_calls_ = 0;
  }

  bool FakeGetInternalBlob(int blob_id, std::string* blob) {
    std::map<int, string>::iterator iter = pool_blobs_.find(blob_id);
    if (iter == pool_blobs_.end())
      return false;
    *blob = iter->second;
    return true;
  }

  bool FakeSetInternalBlob(int blob_id, const std::string& blob) {
    if (pool_write_result_) {
      pool_blobs_[blob_id] = blob;
    }
    return pool_write_result_;
  }

  bool FakeSetEncryptionKey(const brillo::SecureBlob& key) {
    set_encryption_key_num_calls_++;
    return pool_write_result_;
  }

  ObjectPool::Result FakeDeleteAll() {
    delete_all_num_calls_++;
    return pool_write_result_ ? ObjectPool::Result::Success
                              : ObjectPool::Result::Failure;
  }

 protected:
  const FilePath kTestTokenPath;
  // Strict so that we get an error if this gets called.
  StrictMock<TPMUtilityMock> no_tpm_;
  std::map<int, string> pool_blobs_;
  int set_encryption_key_num_calls_;
  int delete_all_num_calls_;
  bool pool_write_result_;
};

TEST_F(SoftwareOnlyTest, CreateNew) {
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob(kAuthData),
                                       kTokenLabel, &slot_id));
  EXPECT_TRUE(slot_manager_->IsTokenAccessible(ic_, slot_id));
  // Check that an encryption key gets set for a load.
  EXPECT_EQ(1, set_encryption_key_num_calls_);
  // Check that there was no attempt to destroy a previous token.
  EXPECT_EQ(0, delete_all_num_calls_);
}

TEST_F(SoftwareOnlyTest, TestOpenIsolate) {
  // Check that trying to open an invalid isolate creates new isolate.
  SecureBlob isolate("invalid");
  bool new_isolate_created = false;
  EXPECT_TRUE(slot_manager_->OpenIsolate(&isolate, &new_isolate_created));
  EXPECT_TRUE(new_isolate_created);

  // Check opening an existing isolate.
  EXPECT_TRUE(slot_manager_->OpenIsolate(&isolate, &new_isolate_created));
  EXPECT_FALSE(new_isolate_created);
}

TEST_F(SoftwareOnlyTest, LoadExisting) {
  InitializeObjectPoolBlobs();
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob(kAuthData),
                                       kTokenLabel, &slot_id));
  EXPECT_TRUE(slot_manager_->IsTokenAccessible(ic_, slot_id));
  EXPECT_EQ(1, set_encryption_key_num_calls_);
  EXPECT_EQ(0, delete_all_num_calls_);
}

TEST_F(SoftwareOnlyTest, BadAuth) {
  InitializeObjectPoolBlobs();
  // We expect the token to be successfully recreated with the new auth value.
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob("bad"),
                                       kTokenLabel, &slot_id));
  EXPECT_TRUE(slot_manager_->IsTokenAccessible(ic_, slot_id));
  EXPECT_EQ(1, set_encryption_key_num_calls_);
  EXPECT_EQ(1, delete_all_num_calls_);
}

TEST_F(SoftwareOnlyTest, CorruptMasterKey) {
  InitializeObjectPoolBlobs();
  pool_blobs_[kEncryptedMasterKey] = "bad";
  // We expect the token to be successfully recreated.
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob(kAuthData),
                                       kTokenLabel, &slot_id));
  EXPECT_TRUE(slot_manager_->IsTokenAccessible(ic_, slot_id));
  EXPECT_EQ(1, set_encryption_key_num_calls_);
  EXPECT_EQ(1, delete_all_num_calls_);
}

TEST_F(SoftwareOnlyTest, CreateNewWriteFailure) {
  pool_write_result_ = false;
  int slot_id = 0;
  EXPECT_FALSE(slot_manager_->LoadToken(
      ic_, kTestTokenPath, MakeBlob(kAuthData), kTokenLabel, &slot_id));
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, slot_id));
}

TEST_F(SoftwareOnlyTest, LoadExistingWriteFailure) {
  InitializeObjectPoolBlobs();
  pool_write_result_ = false;
  int slot_id = 0;
  EXPECT_FALSE(slot_manager_->LoadToken(
      ic_, kTestTokenPath, MakeBlob(kAuthData), kTokenLabel, &slot_id));
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, slot_id));
  EXPECT_EQ(1, set_encryption_key_num_calls_);
}

TEST_F(SoftwareOnlyTest, Unload) {
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob(kAuthData),
                                       kTokenLabel, &slot_id));
  EXPECT_TRUE(slot_manager_->IsTokenAccessible(ic_, slot_id));
  slot_manager_->UnloadToken(ic_, kTestTokenPath);
  EXPECT_FALSE(slot_manager_->IsTokenAccessible(ic_, slot_id));
}

TEST_F(SoftwareOnlyTest, ChangeAuth) {
  InitializeObjectPoolBlobs();
  slot_manager_->ChangeTokenAuthData(kTestTokenPath, MakeBlob(kAuthData),
                                     MakeBlob("new"));
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob("new"),
                                       kTokenLabel, &slot_id));
  EXPECT_EQ(0, delete_all_num_calls_);
}

TEST_F(SoftwareOnlyTest, ChangeAuthWhileLoaded) {
  InitializeObjectPoolBlobs();
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob(kAuthData),
                                       kTokenLabel, &slot_id));
  slot_manager_->ChangeTokenAuthData(kTestTokenPath, MakeBlob(kAuthData),
                                     MakeBlob("new"));
  slot_manager_->UnloadToken(ic_, kTestTokenPath);
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob("new"),
                                       kTokenLabel, &slot_id));
  EXPECT_EQ(0, delete_all_num_calls_);
}

TEST_F(SoftwareOnlyTest, ChangeAuthBeforeInit) {
  slot_manager_->ChangeTokenAuthData(kTestTokenPath, MakeBlob(kAuthData),
                                     MakeBlob("new"));
  // At this point we expect the token to still not exist.
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob("new"),
                                       kTokenLabel, &slot_id));
  EXPECT_EQ(0, delete_all_num_calls_);
}

TEST_F(SoftwareOnlyTest, ChangeAuthWithBadOldAuth) {
  InitializeObjectPoolBlobs();
  slot_manager_->ChangeTokenAuthData(kTestTokenPath, MakeBlob("bad"),
                                     MakeBlob("new"));
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob(kAuthData),
                                       kTokenLabel, &slot_id));
  EXPECT_EQ(0, delete_all_num_calls_);
}

TEST_F(SoftwareOnlyTest, ChangeAuthWithCorruptMasterKey) {
  InitializeObjectPoolBlobs();
  pool_blobs_[kEncryptedMasterKey] = "bad";
  slot_manager_->ChangeTokenAuthData(kTestTokenPath, MakeBlob(kAuthData),
                                     MakeBlob("new"));
  EXPECT_EQ("bad", pool_blobs_[kEncryptedMasterKey]);
}

TEST_F(SoftwareOnlyTest, ChangeAuthWithWriteErrors) {
  InitializeObjectPoolBlobs();
  pool_write_result_ = false;
  slot_manager_->ChangeTokenAuthData(kTestTokenPath, MakeBlob(kAuthData),
                                     MakeBlob("new"));
  pool_write_result_ = true;
  int slot_id = 0;
  EXPECT_TRUE(slot_manager_->LoadToken(ic_, kTestTokenPath, MakeBlob(kAuthData),
                                       kTokenLabel, &slot_id));
  EXPECT_EQ(0, delete_all_num_calls_);
}

}  // namespace chaps
