// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for Service

#include "cryptohome/service.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <base/files/file_util.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <chaps/token_manager_client_mock.h>
#include <chromeos/cryptohome.h>
#include <chromeos/secure_blob.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>

#include "cryptohome/crypto.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_attestation.h"
#include "cryptohome/mock_boot_attributes.h"
#include "cryptohome/mock_boot_lockbox.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_dbus_transition.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_mount.h"
#include "cryptohome/mock_mount_factory.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/username_passkey.h"

using base::PlatformThread;
using chromeos::SecureBlob;

namespace cryptohome {
using ::testing::_;
using ::testing::DoAll;
using ::testing::EndsWith;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;
using ::testing::SetArgPointee;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::SetArgumentPointee;

const char kImageDir[] = "test_image_dir";
const char kSaltFile[] = "test_image_dir/salt";
class ServiceInterfaceTest : public ::testing::Test {
 public:
  ServiceInterfaceTest() { }
  virtual ~ServiceInterfaceTest() { }

  void SetUp() {
    test_helper_.SetUpSystemSalt();
  }
  void TearDown() {
    test_helper_.TearDownSystemSalt();
  }

 protected:
  MakeTests test_helper_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceInterfaceTest);
};

class ServiceSubclass : public Service {
 public:
  ServiceSubclass()
    : Service(),
      completed_tasks_() { }
  virtual ~ServiceSubclass() { }

  virtual void NotifyEvent(CryptohomeEventBase* result) {
    if (strcmp(result->GetEventName(), kMountTaskResultEventType))
      return;
    MountTaskResult* r = static_cast<MountTaskResult*>(result);
    completed_tasks_.push_back(*r);
  }

  virtual void Dispatch() {
    DispatchEvents();
  }

  std::vector<MountTaskResult> completed_tasks_;
};

TEST_F(ServiceInterfaceTest, CheckKeySuccessTest) {
  MockHomeDirs homedirs;
  scoped_refptr<MockMount> mount = new MockMount();
  EXPECT_CALL(homedirs, FreeDiskSpace())
      .WillOnce(Return(true));
  EXPECT_CALL(*mount, AreSameUser(_))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs, AreCredentialsValid(_))
      .WillOnce(Return(true));

  Service service;
  service.set_homedirs(&homedirs);
  service.set_mount_for_user("chromeos-user", mount.get());
  NiceMock<MockInstallAttributes> attrs;
  service.set_install_attrs(&attrs);
  NiceMock<MockAttestation> attest;
  service.set_attestation(&attest);
  NiceMock<chaps::TokenManagerClientMock> chaps;
  NiceMock<MockPlatform> platform;
  service.set_platform(&platform);
  service.set_chaps_client(&chaps);
  service.set_initialize_tpm(false);
  EXPECT_CALL(homedirs, Init(&platform, service.crypto(), _))
    .WillOnce(Return(true));

  service.Initialize();
  gboolean out = FALSE;
  GError *error = NULL;

  char user[] = "chromeos-user";
  char key[] = "274146c6e8886a843ddfea373e2dc71b";
  EXPECT_TRUE(service.CheckKey(user, key, &out, &error));
  EXPECT_TRUE(out);
}

class CheckKeyExInterfaceTest : public ::testing::Test {
 public:
  CheckKeyExInterfaceTest() { }
  virtual ~CheckKeyExInterfaceTest() { }

  void SetUp() {
    mount_ = new MockMount();
    EXPECT_CALL(homedirs_, FreeDiskSpace())
        .WillOnce(Return(true));

    service_.set_reply_factory(&reply_factory_);
    service_.set_homedirs(&homedirs_);
    service_.set_mount_for_user("chromeos-user", mount_.get());
    service_.set_install_attrs(&attrs_);
    service_.set_attestation(&attest_);
    service_.set_platform(&platform_);
    service_.set_chaps_client(&chaps_);
    service_.set_initialize_tpm(false);
    EXPECT_CALL(homedirs_, Init(&platform_, service_.crypto(), _))
        .WillOnce(Return(true));
    service_.Initialize();
  }
  void TearDown() {
  }

 protected:
  MockHomeDirs homedirs_;
  MockDBusReplyFactory reply_factory_;
  NiceMock<MockInstallAttributes> attrs_;
  NiceMock<MockAttestation> attest_;
  NiceMock<chaps::TokenManagerClientMock> chaps_;
  NiceMock<MockPlatform> platform_;
  scoped_refptr<MockMount> mount_;
  Service service_;

 private:
  DISALLOW_COPY_AND_ASSIGN(CheckKeyExInterfaceTest);
};
TEST_F(CheckKeyExInterfaceTest, CheckKeyExMountTest) {
  static const char kUser[] = "chromeos-user";
  static const char kKey[] = "274146c6e8886a843ddfea373e2dc71b";
  scoped_ptr<AccountIdentifier> id(new AccountIdentifier);
  scoped_ptr<AuthorizationRequest> auth(new AuthorizationRequest);
  scoped_ptr<CheckKeyRequest> req(new CheckKeyRequest);
  id->set_email(kUser);
  auth->mutable_key()->set_secret(kKey);

  // event_source_ will delete reply on cleanup.
  std::string* base_reply_ptr = NULL;
  MockDBusReply* reply = new MockDBusReply();
  EXPECT_CALL(reply_factory_, NewReply(NULL, _))
    .WillOnce(DoAll(SaveArg<1>(&base_reply_ptr), Return(reply)));

  EXPECT_CALL(*mount_, AreSameUser(_))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount_, AreValid(_))
       .WillOnce(Return(true));
  // Run will never be called because we aren't running the event loop.
  service_.DoCheckKeyEx(id.get(), auth.get(), req.get(), NULL);

  // Expect an empty reply as success.
  BaseReply expected_reply;
  std::string expected_reply_str;
  expected_reply.SerializeToString(&expected_reply_str);
  ASSERT_TRUE(base_reply_ptr);
  EXPECT_EQ(expected_reply_str, *base_reply_ptr);
  delete base_reply_ptr;
  base_reply_ptr = NULL;

  // Rinse and repeat but fail.
  EXPECT_CALL(*mount_, AreSameUser(_))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount_, AreValid(_))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, Exists(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_))
      .WillOnce(Return(false));

  // event_source_ will delete reply on cleanup.
  reply = new MockDBusReply();
  EXPECT_CALL(reply_factory_, NewReply(NULL, _))
    .WillOnce(DoAll(SaveArg<1>(&base_reply_ptr), Return(reply)));

  service_.DoCheckKeyEx(id.get(), auth.get(), req.get(), NULL);

  // Expect an empty reply as success.
  expected_reply.Clear();
  expected_reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  expected_reply.SerializeToString(&expected_reply_str);
  ASSERT_TRUE(base_reply_ptr);
  EXPECT_EQ(expected_reply_str, *base_reply_ptr);
  delete base_reply_ptr;
  base_reply_ptr = NULL;
}

TEST_F(CheckKeyExInterfaceTest, CheckKeyExHomedirsTest) {
  static const char kUser[] = "chromeos-user";
  static const char kKey[] = "274146c6e8886a843ddfea373e2dc71b";
  scoped_ptr<AccountIdentifier> id(new AccountIdentifier);
  scoped_ptr<AuthorizationRequest> auth(new AuthorizationRequest);
  scoped_ptr<CheckKeyRequest> req(new CheckKeyRequest);
  // Expect an error about missing email.
  // |error| will be cleaned up by event_source_
  MockDBusReply* reply = new MockDBusReply();
  std::string* base_reply_ptr = NULL;
  id->set_email(kUser);
  auth->mutable_key()->set_secret(kKey);

  EXPECT_CALL(*mount_, AreSameUser(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(homedirs_, Exists(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_))
      .WillOnce(Return(true));

  // Run will never be called because we aren't running the event loop.
  EXPECT_CALL(reply_factory_, NewReply(NULL, _))
    .WillOnce(DoAll(SaveArg<1>(&base_reply_ptr), Return(reply)));
  service_.DoCheckKeyEx(id.get(), auth.get(), req.get(), NULL);

  // Expect an empty reply as success.
  BaseReply expected_reply;
  std::string expected_reply_str;
  expected_reply.SerializeToString(&expected_reply_str);
  ASSERT_TRUE(base_reply_ptr);
  EXPECT_EQ(expected_reply_str, *base_reply_ptr);
  delete base_reply_ptr;
  base_reply_ptr = NULL;

  // Ensure failure
  EXPECT_CALL(homedirs_, AreCredentialsValid(_))
      .WillOnce(Return(false));

  // event_source_ will delete reply on cleanup.
  reply = new MockDBusReply();
  EXPECT_CALL(reply_factory_, NewReply(NULL, _))
    .WillOnce(DoAll(SaveArg<1>(&base_reply_ptr), Return(reply)));

  service_.DoCheckKeyEx(id.get(), auth.get(), req.get(), NULL);

  // Expect an empty reply as success.
  expected_reply.Clear();
  expected_reply.set_error(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED);
  expected_reply.SerializeToString(&expected_reply_str);
  ASSERT_TRUE(base_reply_ptr);
  EXPECT_EQ(expected_reply_str, *base_reply_ptr);
  delete base_reply_ptr;
  base_reply_ptr = NULL;
}

TEST_F(ServiceInterfaceTest, CheckAsyncTestCredentials) {
  NiceMock<MockTpm> tpm;
  NiceMock<MockPlatform> platform;

  test_helper_.InjectSystemSalt(&platform, kSaltFile);
  test_helper_.InitTestData(kImageDir, kDefaultUsers, kDefaultUserCount);
  TestUser* user = &test_helper_.users[7];
  user->InjectKeyset(&platform);

  HomeDirs homedirs;
  homedirs.set_shadow_root(kImageDir);
  homedirs.set_platform(&platform);
  scoped_ptr<policy::PolicyProvider> policy_provider(
      new policy::PolicyProvider(new NiceMock<policy::MockDevicePolicy>));
  homedirs.set_policy_provider(policy_provider.get());

  ServiceSubclass service;
  service.set_platform(&platform);
  service.set_homedirs(&homedirs);
  service.crypto()->set_platform(&platform);
  NiceMock<MockInstallAttributes> attrs;
  service.set_install_attrs(&attrs);
  service.set_initialize_tpm(false);
  NiceMock<MockAttestation> attest;
  service.set_attestation(&attest);
  NiceMock<chaps::TokenManagerClientMock> chaps;
  service.set_chaps_client(&chaps);
  service.Initialize();

  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(user->password,
                                        test_helper_.system_salt, &passkey);
  std::string passkey_string(static_cast<const char*>(passkey.const_data()),
                             passkey.size());

  gboolean out = FALSE;
  GError *error = NULL;
  gint async_id = -1;
  EXPECT_TRUE(service.AsyncCheckKey(
      const_cast<gchar*>(static_cast<const gchar*>(user->username)),
      const_cast<gchar*>(static_cast<const gchar*>(passkey_string.c_str())),
      &async_id,
      &error));
  EXPECT_NE(-1, async_id);
  for (unsigned int i = 0; i < 64; i++) {
    bool found = false;
    service.Dispatch();
    for (unsigned int j = 0; j < service.completed_tasks_.size(); j++) {
      if (service.completed_tasks_[j].sequence_id() == async_id) {
        out = service.completed_tasks_[j].return_status();
        found = true;
      }
    }
    if (found) {
      break;
    }
    PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
  }
  EXPECT_TRUE(out);
}

TEST_F(ServiceInterfaceTest, GetPublicMountPassKey) {
  NiceMock<MockPlatform> platform;

  const char kPublicMountSaltPath[] = "/var/lib/public_mount_salt";
  chromeos::Blob public_mount_salt(CRYPTOHOME_DEFAULT_SALT_LENGTH, 'P');
  EXPECT_CALL(platform, FileExists(kPublicMountSaltPath))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform, GetFileSize(kPublicMountSaltPath, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(public_mount_salt.size()),
                          Return(true)));
  EXPECT_CALL(platform, ReadFile(kPublicMountSaltPath, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(public_mount_salt),
                          Return(true)));

  MockHomeDirs homedirs;
  ServiceSubclass service;
  service.set_platform(&platform);
  service.set_homedirs(&homedirs);
  service.crypto()->set_platform(&platform);
  NiceMock<MockInstallAttributes> attrs;
  service.set_install_attrs(&attrs);
  service.set_initialize_tpm(false);
  NiceMock<MockAttestation> attest;
  service.set_attestation(&attest);
  NiceMock<chaps::TokenManagerClientMock> chaps;
  service.set_chaps_client(&chaps);
  service.Initialize();

  const char kPublicUser1[] = "public_user_1";
  const char kPublicUser2[] = "public_user_2";

  std::string public_user1_passkey;
  service.GetPublicMountPassKey(kPublicUser1, &public_user1_passkey);

  std::string public_user2_passkey;
  service.GetPublicMountPassKey(kPublicUser2, &public_user2_passkey);
  // The passkey should be different for different user.
  EXPECT_NE(public_user1_passkey, public_user2_passkey);

  std::string public_user1_passkey2;
  service.GetPublicMountPassKey(kPublicUser1, &public_user1_passkey2);
  // The passkey should be the same for the same user.
  EXPECT_EQ(public_user1_passkey, public_user1_passkey2);
}

TEST_F(ServiceInterfaceTest, GetSanitizedUsername) {
  Service service;
  char username[] = "chromeos-user";
  gchar *sanitized = NULL;
  GError *error = NULL;
  EXPECT_TRUE(service.GetSanitizedUsername(username, &sanitized, &error));
  EXPECT_TRUE(error == NULL);
  ASSERT_TRUE(sanitized);

  const std::string expected(
      chromeos::cryptohome::home::SanitizeUserName(username));
  EXPECT_FALSE(expected.empty());

  EXPECT_EQ(expected, sanitized);
  g_free(sanitized);
}

TEST(Standalone, CheckAutoCleanupCallback) {
  // Checks that AutoCleanupCallback() is called periodically.
  MockHomeDirs homedirs;
  NiceMock<MockPlatform> platform;
  NiceMock<MockInstallAttributes> attrs;
  NiceMock<MockTpm> tpm;
  NiceMock<MockAttestation> attest;
  NiceMock<chaps::TokenManagerClientMock> chaps;
  NiceMock<MockBootAttributes> boot_attributes;
  Service service;
  service.set_homedirs(&homedirs);
  service.set_platform(&platform);
  service.set_install_attrs(&attrs);
  service.set_initialize_tpm(false);
  service.set_use_tpm(false);
  service.set_tpm(&tpm);
  service.set_boot_attributes(&boot_attributes);
  service.set_attestation(&attest);
  service.set_chaps_client(&chaps);

  // Service will schedule periodic clean-ups. Wait a bit and make
  // sure that we had at least 3 executed.
  EXPECT_CALL(homedirs, Init(&platform, service.crypto(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(homedirs, FreeDiskSpace())
      .Times(::testing::AtLeast(3));

  scoped_refptr<MockMount> mount = new MockMount();
  EXPECT_CALL(*mount, UpdateCurrentUserActivityTimestamp(0))
      .Times(::testing::AtLeast(3));
  service.set_mount_for_user("some-user-to-clean-up", mount.get());

  service.set_auto_cleanup_period(2);  // 2ms = 500HZ
  service.set_update_user_activity_period(2);  // 2 x 5ms = 25HZ
  service.Initialize();
  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
}

TEST(Standalone, CheckAutoCleanupCallbackFirst) {
  // Checks that AutoCleanupCallback() is called first right after init.
  MockHomeDirs homedirs;
  NiceMock<MockInstallAttributes> attrs;
  NiceMock<MockTpm> tpm;
  NiceMock<MockAttestation> attest;
  NiceMock<MockPlatform> platform;
  NiceMock<MockBootAttributes> boot_attributes;
  NiceMock<chaps::TokenManagerClientMock> chaps;
  Service service;
  service.set_homedirs(&homedirs);
  service.set_install_attrs(&attrs);
  service.set_initialize_tpm(false);
  service.set_use_tpm(false);
  service.set_tpm(&tpm);
  service.set_attestation(&attest);
  service.set_platform(&platform);
  service.set_boot_attributes(&boot_attributes);
  service.set_chaps_client(&chaps);

  // Service will schedule first cleanup right after its init.
  EXPECT_CALL(homedirs, Init(&platform, service.crypto(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(homedirs, FreeDiskSpace())
      .Times(1);
  service.set_auto_cleanup_period(1000);  // 1s - long enough
  service.Initialize();
  // short delay to see the first invocation
  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(10));
}

struct Mounts {
  const char* src;
  const char* dst;
};

const struct Mounts kShadowMounts[] = {
  { "/home/.shadow/a", "/home/user/0" },
  { "/home/.shadow/a", "/home/root/0" },
  { "/home/.shadow/b", "/home/user/1" },
  { "/home/.shadow/a", "/home/chronos/user" },
  { "/home/.shadow/b", "/home/root/1" },
};
const int kShadowMountsCount = 5;

bool StaleShadowMounts(const std::string& from_prefix,
                 std::multimap<const std::string, const std::string>* mounts) {
  LOG(INFO) << "StaleShadowMounts(" << from_prefix << "): called";
  if (from_prefix == "/home/.shadow/") {
    if (!mounts)
      return true;
    const struct Mounts* m = &kShadowMounts[0];
    for (int i = 0; i < kShadowMountsCount; ++i, ++m) {
      mounts->insert(
          std::pair<const std::string, const std::string>(m->src, m->dst));
      LOG(INFO) << "Inserting " << m->src << ":" << m->dst;
    }
    return true;
  }
  return false;
}

class CleanUpStaleTest : public ::testing::Test {
 public:
  CleanUpStaleTest() { }
  virtual ~CleanUpStaleTest() { }

  void SetUp() {
    service_.set_homedirs(&homedirs_);
    service_.set_install_attrs(&attrs_);
    service_.set_initialize_tpm(false);
    service_.set_platform(&platform_);
    service_.set_chaps_client(&chaps_client_);
    // Empty token list by default.  The effect is that there are no attempts
    // to unload tokens unless a test explicitly sets up the token list.
    EXPECT_CALL(chaps_client_, GetTokenList(_, _))
        .WillRepeatedly(Return(true));
  }

  void TearDown() { }

 protected:
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockInstallAttributes> attrs_;
  MockPlatform platform_;
  chaps::TokenManagerClientMock chaps_client_;
  Service service_;

 private:
  DISALLOW_COPY_AND_ASSIGN(CleanUpStaleTest);
};

TEST_F(CleanUpStaleTest, EmptyMap_NoOpenFiles_ShadowOnly) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .Times(3)
    .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
    .Times(kShadowMountsCount);
  EXPECT_CALL(platform_, Unmount(_, true, _))
    .Times(kShadowMountsCount)
    .WillRepeatedly(Return(true));
  EXPECT_FALSE(service_.CleanUpStaleMounts(false));
}

TEST_F(CleanUpStaleTest, EmptyMap_OpenLegacy_ShadowOnly) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and some open filehandles to the legacy homedir, all mounts without
  // filehandles are unmounted.
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .Times(3)
    .WillRepeatedly(Invoke(StaleShadowMounts));
  std::vector<ProcessInformation> processes(1);
  processes[0].set_process_id(1);
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
    .Times(kShadowMountsCount - 1);
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(
      "/home/chronos/user", _))
    .Times(1)
    .WillRepeatedly(SetArgumentPointee<1>(processes));
  EXPECT_CALL(platform_, Unmount(EndsWith("/1"), true, _))
    .Times(2)
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(service_.CleanUpStaleMounts(false));
}

TEST_F(CleanUpStaleTest, FilledMap_NoOpenFiles_ShadowOnly) {
  // Checks that when we have a bunch of stale shadow mounts, some active
  // mounts, and no open filehandles, all inactive mounts are unmounted.

  // ownership handed off to the Service MountMap
  MockMountFactory f;
  MockMount *m = new MockMount();

  EXPECT_CALL(f, New())
    .WillOnce(Return(m));

  service_.set_mount_factory(&f);

  EXPECT_CALL(homedirs_, Init(&platform_, service_.crypto(), _))
    .WillOnce(Return(true));

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .Times(3)
    .WillRepeatedly(Return(false));

  ASSERT_TRUE(service_.Initialize());

  EXPECT_CALL(*m, Init(&platform_, service_.crypto(), _))
    .WillOnce(Return(true));
  EXPECT_CALL(*m, MountCryptohome(_, _, _))
    .WillOnce(Return(true));
  EXPECT_CALL(*m, UpdateCurrentUserActivityTimestamp(_))
    .WillOnce(Return(true));

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .Times(3)
    .WillRepeatedly(Return(false));

  gint error_code = 0;
  gboolean result = FALSE;
  ASSERT_TRUE(service_.Mount("foo@bar.net", "key", true, false,
                             &error_code, &result, NULL));
  ASSERT_EQ(TRUE, result);
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .Times(3)
    .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
    .Times(kShadowMountsCount);

  EXPECT_CALL(*m, OwnsMountPoint(_))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(*m, OwnsMountPoint("/home/user/1"))
    .WillOnce(Return(true));
  EXPECT_CALL(*m, OwnsMountPoint("/home/root/1"))
    .WillOnce(Return(true));

  EXPECT_CALL(platform_, Unmount(EndsWith("/0"), true, _))
    .Times(2)
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount("/home/chronos/user", true, _))
    .WillOnce(Return(true));

  std::vector<std::string> fake_token_list;
  fake_token_list.push_back("/home/chronos/user/token");
  fake_token_list.push_back("/home/user/1/token");
  fake_token_list.push_back("/home/root/1/token");
  EXPECT_CALL(chaps_client_, GetTokenList(_, _))
      .WillRepeatedly(DoAll(SetArgumentPointee<1>(fake_token_list),
                            Return(true)));

  EXPECT_CALL(chaps_client_,
              UnloadToken(_, base::FilePath("/home/chronos/user/token")))
      .Times(1);

  // Expect that CleanUpStaleMounts() tells us it skipped no mounts.
  EXPECT_FALSE(service_.CleanUpStaleMounts(false));
}

TEST(Standalone, StoreEnrollmentState) {
  NiceMock<MockInstallAttributes> attrs;
  MockPlatform platform;
  MockCrypto crypto;
  Service service;
  service.set_crypto(&crypto);
  service.set_install_attrs(&attrs);
  service.set_platform(&platform);

  chromeos::glib::ScopedArray test_array(g_array_new(FALSE, FALSE, 1));
  std::string data = "123456";
  g_array_append_vals(test_array.get(), data.data(), data.length());

  // Helper strings for setting install attributes.
  static const char true_str[] = "true";
  const chromeos::Blob true_value(true_str, true_str + arraysize(true_str));

  static const char false_str[] = "false";
  const chromeos::Blob false_value(false_str, false_str + arraysize(false_str));

  gboolean success;
  GError* error = NULL;

  // Set us as non-enterprise enrolled.
  EXPECT_CALL(attrs, Get("enterprise.owned", _)).WillOnce(
      DoAll(SetArgumentPointee<1>(false_value), Return(true)));
  service.DetectEnterpriseOwnership();

  // Should not enterprise-enroll this device.
  EXPECT_TRUE(service.StoreEnrollmentState(test_array.get(), &success, &error));
  EXPECT_FALSE(success);

  // Set us as enterprise enrolled.
  EXPECT_CALL(attrs, Get("enterprise.owned", _)).WillOnce(
      DoAll(SetArgumentPointee<1>(true_value), Return(true)));
  service.DetectEnterpriseOwnership();

  std::string encrypted_data = "so_encrypted";

  // Test successful encryption.
  EXPECT_CALL(crypto, EncryptWithTpm(_, _)).WillOnce(DoAll(
      SetArgumentPointee<1>(encrypted_data), Return(true)));

  // Should write file as this device is enterprise enrolled.
  EXPECT_CALL(platform, WriteStringToFileAtomicDurable(
      "/mnt/stateful_partition/unencrypted/preserve/enrollment_state.epb",
      encrypted_data, _)).WillOnce(Return(true));
  EXPECT_TRUE(service.StoreEnrollmentState(test_array.get(), &success, &error));
  EXPECT_TRUE(success);

  EXPECT_TRUE(service.homedirs()->enterprise_owned());
}

TEST(Standalone, LoadEnrollmentState) {
  MockPlatform platform;
  MockCrypto crypto;
  Service service;
  service.set_crypto(&crypto);
  service.set_platform(&platform);

  gboolean success;
  GError* error = NULL;
  chromeos::glib::ScopedArray output;

  // Convert to blob -- this is what we're reading from the file.
  std::string data = "123456";
  const chromeos::Blob data_blob(data.c_str(), data.c_str() + data.length());
  std::string decrypted_data = "decrypted";
  SecureBlob decrypted_blob(decrypted_data.data(), decrypted_data.size());

  // Assume the data is there, we should return the value and success.
  EXPECT_CALL(platform, ReadFile(
      "/mnt/stateful_partition/unencrypted/preserve/enrollment_state.epb",
      _)).WillOnce(DoAll(SetArgumentPointee<1>(data_blob), Return(true)));

  EXPECT_CALL(crypto, DecryptWithTpm(_, _)).WillOnce(DoAll(
      SetArgumentPointee<1>(decrypted_blob), Return(TRUE)));

  EXPECT_TRUE(service.LoadEnrollmentState(
      &(chromeos::Resetter(&output).lvalue()), &success, &error));
  EXPECT_TRUE(success);

  // Convert output array to a blob for comparison.
  SecureBlob output_blob(output->data, output->len);
  EXPECT_EQ(decrypted_blob, output_blob);

  // Assume we fail to read the data, we should not return success.
  EXPECT_CALL(platform, ReadFile(
      "/mnt/stateful_partition/unencrypted/preserve/enrollment_state.epb",
      _)).WillOnce(Return(false));

  EXPECT_TRUE(service.LoadEnrollmentState(
      &(chromeos::Resetter(&output).lvalue()), &success, &error));
  EXPECT_FALSE(success);
}

class ExTest : public ::testing::Test {
 public:
  ExTest() { }
  virtual ~ExTest() { }

  void SetUp() {
    service_.set_attestation(&attest_);
    service_.set_homedirs(&homedirs_);
    service_.set_install_attrs(&attrs_);
    service_.set_initialize_tpm(false);
    service_.set_use_tpm(false);
    service_.set_platform(&platform_);
    service_.set_chaps_client(&chaps_client_);
    service_.set_boot_lockbox(&lockbox_);
    service_.set_boot_attributes(&boot_attributes_);
    service_.set_reply_factory(&reply_factory_);
    // Empty token list by default.  The effect is that there are no attempts
    // to unload tokens unless a test explicitly sets up the token list.
    EXPECT_CALL(chaps_client_, GetTokenList(_, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, ReadFileToString(EndsWith("decrypt_stateful"), _))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(boot_attributes_, Load())
        .WillRepeatedly(Return(true));

    g_error_ = NULL;
    // Fast path through Initialize()
    EXPECT_CALL(homedirs_, Init(&platform_, service_.crypto(), _))
        .WillOnce(Return(true));
    // Skip the CleanUpStaleMounts bit.
    EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
        .WillRepeatedly(Return(false));
    ASSERT_TRUE(service_.Initialize());
  }

  void TearDown() {
    if (g_error_) {
      g_error_free(g_error_);
      g_error_ = NULL;
    }
  }

  void SetupErrorReply() {
    g_error_ = NULL;
    // |error| will be cleaned up by event_source_
    MockDBusErrorReply *error = new MockDBusErrorReply();
    EXPECT_CALL(reply_factory_, NewErrorReply(NULL, _))
        .WillOnce(DoAll(SaveArg<1>(&g_error_), Return(error)));
  }

  void SetupReply() {
    EXPECT_CALL(reply_factory_, NewReply(NULL, _))
        .WillOnce(DoAll(SaveArg<1>(&reply_), Return(new MockDBusReply())));
  }

  BaseReply GetLastReply() {
    BaseReply reply;
    CHECK(reply_);
    CHECK(reply.ParseFromString(*reply_));
    delete reply_;
    reply_ = NULL;
    return reply;
  }

  void PrepareArguments() {
    id_.reset(new AccountIdentifier);
    auth_.reset(new AuthorizationRequest);
    add_req_.reset(new AddKeyRequest);
    check_req_.reset(new CheckKeyRequest);
    mount_req_.reset(new MountRequest);
    remove_req_.reset(new RemoveKeyRequest);
    list_keys_req_.reset(new ListKeysRequest);
  }

  template<class ProtoBuf>
  GArray* GArrayFromProtoBuf(const ProtoBuf& pb) {
    guint len = pb.ByteSize();
    GArray* ary = g_array_sized_new(FALSE, FALSE, 1, len);
    g_array_set_size(ary, len);
    if (!pb.SerializeToArray(ary->data, len)) {
      printf("Failed to serialize protocol buffer.\n");
      return NULL;
    }
    return ary;
  }

  VaultKeyset* GetNiceMockVaultKeyset(const Credentials& credentials) const {
    scoped_ptr<VaultKeyset> mvk(new NiceMock<MockVaultKeyset>);
    *(mvk->mutable_serialized()->mutable_key_data()) = credentials.key_data();
    return mvk.release();
  }

  template<class ProtoBuf>
  chromeos::SecureBlob BlobFromProtobuf(const ProtoBuf& pb) {
    std::string serialized;
    CHECK(pb.SerializeToString(&serialized));
    return chromeos::SecureBlob(serialized);
  }

 protected:
  NiceMock<MockAttestation> attest_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockInstallAttributes> attrs_;
  NiceMock<MockBootLockbox> lockbox_;
  NiceMock<MockBootAttributes> boot_attributes_;
  MockDBusReplyFactory reply_factory_;

  scoped_ptr<AccountIdentifier> id_;
  scoped_ptr<AuthorizationRequest> auth_;
  scoped_ptr<AddKeyRequest> add_req_;
  scoped_ptr<CheckKeyRequest> check_req_;
  scoped_ptr<MountRequest> mount_req_;
  scoped_ptr<RemoveKeyRequest> remove_req_;
  scoped_ptr<ListKeysRequest> list_keys_req_;

  GError* g_error_;
  std::string* reply_;
  MockPlatform platform_;
  chaps::TokenManagerClientMock chaps_client_;
  Service service_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ExTest);
};

TEST_F(ExTest, MountInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  // TODO(wad) Look at using the ServiceSubclass.
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ExTest, MountInvalidArgsNoSecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ExTest, MountInvalidArgsEmptySecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("");
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ExTest, MountInvalidArgsCreateWithNoKey) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create();
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("CreateRequest supplied with no keys", g_error_->message);
}

TEST_F(ExTest, MountInvalidArgsCreateWithEmptyKey) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create()->add_keys();
  // TODO(wad) Add remaining missing field tests and NULL tests
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("CreateRequest Keys are not fully specified",
               g_error_->message);
}

TEST_F(ExTest, AddKeyInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ExTest, AddKeyInvalidArgsNoSecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ExTest, AddKeyInvalidArgsNoNewKeySet) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  add_req_->clear_key();
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No new key supplied", g_error_->message);
}

TEST_F(ExTest, AddKeyInvalidArgsNoKeyFilled) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No new key supplied", g_error_->message);
}

TEST_F(ExTest, AddKeyInvalidArgsNoNewKeyLabel) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  // No label
  add_req_->mutable_key()->set_secret("some secret");
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No new key label supplied", g_error_->message);
}

TEST_F(ExTest, CheckKeyInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoCheckKeyEx(id_.get(), auth_.get(), check_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ExTest, CheckKeyInvalidArgsNoSecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  service_.DoCheckKeyEx(id_.get(), auth_.get(), check_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ExTest, CheckKeyInvalidArgsEmptySecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("");
  service_.DoCheckKeyEx(id_.get(), auth_.get(), check_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ExTest, RemoveKeyInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ExTest, RemoveKeyInvalidArgsNoSecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ExTest, RemoveKeyInvalidArgsEmptySecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("");
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ExTest, RemoveKeyInvalidArgsEmptyRemoveLabel) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("some secret");
  remove_req_->mutable_key()->mutable_data();
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No label provided for target key", g_error_->message);
}

TEST_F(ExTest, BootLockboxSignSuccess) {
  SetupReply();
  SecureBlob test_signature("test");
  EXPECT_CALL(lockbox_, Sign(_, _))
      .WillRepeatedly(DoAll(SetArgumentPointee<1>(test_signature),
                            Return(true)));

  SignBootLockboxRequest request;
  request.set_data("test_data");
  service_.DoSignBootLockbox(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
  EXPECT_TRUE(reply.HasExtension(SignBootLockboxReply::reply));
  EXPECT_EQ("test",
            reply.GetExtension(SignBootLockboxReply::reply).signature());
}

TEST_F(ExTest, BootLockboxSignBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoSignBootLockbox(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
  // Try with |data| not set.
  SetupErrorReply();
  SignBootLockboxRequest request;
  service_.DoSignBootLockbox(BlobFromProtobuf(request), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ExTest, BootLockboxSignError) {
  SetupReply();
  EXPECT_CALL(lockbox_, Sign(_, _))
      .WillRepeatedly(Return(false));

  SignBootLockboxRequest request;
  request.set_data("test_data");
  service_.DoSignBootLockbox(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN, reply.error());
  EXPECT_FALSE(reply.HasExtension(SignBootLockboxReply::reply));
}

TEST_F(ExTest, BootLockboxVerifySuccess) {
  SetupReply();
  EXPECT_CALL(lockbox_, Verify(_, _))
      .WillRepeatedly(Return(true));

  VerifyBootLockboxRequest request;
  request.set_data("test_data");
  request.set_signature("test_signature");
  service_.DoVerifyBootLockbox(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
  EXPECT_FALSE(reply.HasExtension(SignBootLockboxReply::reply));
}

TEST_F(ExTest, BootLockboxVerifyBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoVerifyBootLockbox(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
  // Try with |signature| not set.
  SetupErrorReply();
  VerifyBootLockboxRequest request;
  request.set_data("test_data");
  service_.DoVerifyBootLockbox(BlobFromProtobuf(request), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
  // Try with |data| not set.
  SetupErrorReply();
  VerifyBootLockboxRequest request2;
  request2.set_signature("test_data");
  service_.DoVerifyBootLockbox(BlobFromProtobuf(request2), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ExTest, BootLockboxVerifyError) {
  SetupReply();
  EXPECT_CALL(lockbox_, Verify(_, _))
      .WillRepeatedly(Return(false));

  VerifyBootLockboxRequest request;
  request.set_data("test_data");
  request.set_signature("test_signature");
  service_.DoVerifyBootLockbox(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID, reply.error());
}

TEST_F(ExTest, BootLockboxFinalizeSuccess) {
  SetupReply();
  EXPECT_CALL(lockbox_, FinalizeBoot())
      .WillRepeatedly(Return(true));

  FinalizeBootLockboxRequest request;
  service_.DoFinalizeBootLockbox(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
  EXPECT_FALSE(reply.HasExtension(SignBootLockboxReply::reply));
}

TEST_F(ExTest, BootLockboxFinalizeBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoFinalizeBootLockbox(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ExTest, BootLockboxFinalizeError) {
  SetupReply();
  EXPECT_CALL(lockbox_, FinalizeBoot())
      .WillRepeatedly(Return(false));

  FinalizeBootLockboxRequest request;
  service_.DoFinalizeBootLockbox(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_TPM_COMM_ERROR, reply.error());
}

TEST_F(ExTest, GetBootAttributeSuccess) {
  SetupReply();
  EXPECT_CALL(boot_attributes_, Get(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>("1234"), Return(true)));

  GetBootAttributeRequest request;
  request.set_name("test");
  service_.DoGetBootAttribute(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
  EXPECT_TRUE(reply.HasExtension(GetBootAttributeReply::reply));
  EXPECT_EQ("1234",
            reply.GetExtension(GetBootAttributeReply::reply).value());
}

TEST_F(ExTest, GetBootAttributeBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoGetBootAttribute(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ExTest, GetBootAttributeError) {
  SetupReply();
  EXPECT_CALL(boot_attributes_, Get(_, _))
      .WillRepeatedly(Return(false));

  GetBootAttributeRequest request;
  request.set_name("test");
  service_.DoGetBootAttribute(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND, reply.error());
}

TEST_F(ExTest, SetBootAttributeSuccess) {
  SetupReply();
  SetBootAttributeRequest request;
  request.set_name("test");
  request.set_value("1234");
  service_.DoSetBootAttribute(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
}

TEST_F(ExTest, SetBootAttributeBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoSetBootAttribute(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ExTest, FlushAndSignBootAttributesSuccess) {
  SetupReply();
  EXPECT_CALL(boot_attributes_, FlushAndSign())
      .WillRepeatedly(Return(true));

  FlushAndSignBootAttributesRequest request;
  service_.DoFlushAndSignBootAttributes(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
}

TEST_F(ExTest, FlushAndSignBootAttributesBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoFlushAndSignBootAttributes(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ExTest, FlushAndSignBootAttributesError) {
  SetupReply();
  EXPECT_CALL(boot_attributes_, FlushAndSign())
      .WillRepeatedly(Return(false));

  FlushAndSignBootAttributesRequest request;
  service_.DoFlushAndSignBootAttributes(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN, reply.error());
}

TEST_F(ExTest, GetLoginStatusSuccess) {
  SetupReply();
  EXPECT_CALL(homedirs_, GetPlainOwner(_))
    .WillOnce(Return(true));
  EXPECT_CALL(lockbox_, IsFinalized())
    .WillOnce(Return(false));

  GetLoginStatusRequest request;
  service_.DoGetLoginStatus(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
  EXPECT_TRUE(reply.HasExtension(GetLoginStatusReply::reply));
  EXPECT_TRUE(
      reply.GetExtension(GetLoginStatusReply::reply).owner_user_exists());
  EXPECT_FALSE(
      reply.GetExtension(GetLoginStatusReply::reply).boot_lockbox_finalized());
}

TEST_F(ExTest, GetLoginStatusBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoVerifyBootLockbox(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ExTest, GetKeyDataExNoMatch) {
  SetupReply();
  PrepareArguments();

  EXPECT_CALL(homedirs_, Exists(_))
      .WillRepeatedly(Return(true));

  id_->set_email("unittest@example.com");
  GetKeyDataRequest req;
  req.mutable_key()->mutable_data()->set_label("non-existent label");
  // Ensure there are no matches.
  EXPECT_CALL(homedirs_, GetVaultKeyset(_))
      .Times(1)
      .WillRepeatedly(Return(static_cast<VaultKeyset*>(NULL)));
  service_.DoGetKeyDataEx(id_.get(), auth_.get(), &req, NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
  GetKeyDataReply sub_reply = reply.GetExtension(GetKeyDataReply::reply);
  EXPECT_EQ(0, sub_reply.key_data_size());
}

TEST_F(ExTest, GetKeyDataExOneMatch) {
  // Request the single key by label.
  SetupReply();
  PrepareArguments();

  static const char *kExpectedLabel = "find-me";
  GetKeyDataRequest req;
  req.mutable_key()->mutable_data()->set_label(kExpectedLabel);

  EXPECT_CALL(homedirs_, Exists(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, GetVaultKeyset(_))
      .Times(1)
      .WillRepeatedly(Invoke(this, &ExTest::GetNiceMockVaultKeyset));

  id_->set_email("unittest@example.com");
  service_.DoGetKeyDataEx(id_.get(), auth_.get(), &req, NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());

  GetKeyDataReply sub_reply = reply.GetExtension(GetKeyDataReply::reply);
  ASSERT_EQ(1, sub_reply.key_data_size());
  EXPECT_EQ(std::string(kExpectedLabel), sub_reply.key_data(0).label());
}

TEST_F(ExTest, GetKeyDataInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  GetKeyDataRequest req;
  service_.DoGetKeyDataEx(id_.get(), auth_.get(), &req, NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ExTest, ListKeysInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoListKeysEx is called directly.
  service_.DoListKeysEx(id_.get(), auth_.get(), list_keys_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

}  // namespace cryptohome
