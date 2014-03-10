// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for Service

#include "service.h"

#include <base/at_exit.h>
#include <base/threading/platform_thread.h>
#include <base/file_util.h>
#include <base/time.h>
#include <chaps/token_manager_client_mock.h>
#include <chromeos/cryptohome.h>
#include <chromeos/secure_blob.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>
#include <string>

#include "crypto.h"
#include "make_tests.h"
#include "mock_attestation.h"
#include "mock_crypto.h"
#include "mock_dbus_transition.h"
#include "mock_homedirs.h"
#include "mock_install_attributes.h"
#include "mock_mount.h"
#include "mock_mount_factory.h"
#include "mock_platform.h"
#include "mock_tpm.h"
#include "username_passkey.h"

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
  EXPECT_CALL(homedirs, Init())
      .WillOnce(Return(true));
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
  service.Initialize();
  gboolean out = FALSE;
  GError *error = NULL;

  char user[] = "chromeos-user";
  char key[] = "274146c6e8886a843ddfea373e2dc71b";
  EXPECT_TRUE(service.CheckKey(user, key, &out, &error));
  EXPECT_TRUE(out);
}

TEST_F(ServiceInterfaceTest, CheckAsyncTestCredentials) {
  NiceMock<MockTpm> tpm;
  NiceMock<MockPlatform> platform;

  test_helper_.InjectSystemSalt(&platform, kSaltFile);
  test_helper_.InitTestData(kImageDir, kDefaultUsers, kDefaultUserCount);
  TestUser* user = &test_helper_.users[7];
  user->InjectKeyset(&platform);

  HomeDirs homedirs;
  homedirs.crypto()->set_tpm(&tpm);
  homedirs.crypto()->set_use_tpm(false);
  homedirs.crypto()->set_platform(&platform);
  homedirs.set_shadow_root(kImageDir);
  homedirs.set_platform(&platform);
  homedirs.set_policy_provider(new policy::PolicyProvider(
      new NiceMock<policy::MockDevicePolicy>));

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
  Service service;
  service.set_homedirs(&homedirs);
  NiceMock<MockInstallAttributes> attrs;
  service.set_install_attrs(&attrs);
  service.set_initialize_tpm(false);

  // Service will schedule periodic clean-ups. Wait a bit and make
  // sure that we had at least 3 executed.
  EXPECT_CALL(homedirs, Init())
      .WillOnce(Return(true));
  EXPECT_CALL(homedirs, FreeDiskSpace())
      .Times(::testing::AtLeast(3));

  scoped_refptr<MockMount> mount = new MockMount();
  EXPECT_CALL(*mount, UpdateCurrentUserActivityTimestamp(0))
      .Times(::testing::AtLeast(3));
  service.set_mount_for_user("some-user-to-clean-up", mount.get());

  service.set_auto_cleanup_period(2);  // 2ms = 500HZ
  service.set_update_user_activity_period(2);  // 2 x 5ms = 25HZ
  NiceMock<MockAttestation> attest;
  service.set_attestation(&attest);
  NiceMock<chaps::TokenManagerClientMock> chaps;
  service.set_chaps_client(&chaps);
  service.Initialize();
  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
}

TEST(Standalone, CheckAutoCleanupCallbackFirst) {
  // Checks that AutoCleanupCallback() is called first right after init.
  MockHomeDirs homedirs;
  Service service;
  service.set_homedirs(&homedirs);
  NiceMock<MockInstallAttributes> attrs;
  service.set_install_attrs(&attrs);
  service.set_initialize_tpm(false);
  NiceMock<MockAttestation> attest;
  service.set_attestation(&attest);

  // Service will schedule first cleanup right after its init.
  EXPECT_CALL(homedirs, Init())
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

  EXPECT_CALL(homedirs_, Init())
    .WillOnce(Return(true));

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .Times(3)
    .WillRepeatedly(Return(false));

  ASSERT_TRUE(service_.Initialize());

  EXPECT_CALL(*m, Init())
    .WillOnce(Return(true));

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .Times(3)
    .WillRepeatedly(Return(false));

  gint error_code = 0;
  gboolean result = FALSE;
  ASSERT_TRUE(service_.Mount("foo@bar.net", "key", true, false,
                             &error_code, &result, NULL));

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
              UnloadToken(_, FilePath("/home/chronos/user/token")))
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
  EXPECT_CALL(crypto, EncryptWithTpm(_,_)).WillOnce(DoAll(
      SetArgumentPointee<1>(encrypted_data), Return(true)));

  // Should write file as this device is enterprise enrolled.
  EXPECT_CALL(platform, WriteStringToFile(
      "/mnt/stateful_partition/unencrypted/preserve/enrollment_state.epb",
      encrypted_data)).WillOnce(Return(true));
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
  GArray* output = NULL;

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

  EXPECT_TRUE(service.LoadEnrollmentState(&output, &success, &error));
  EXPECT_TRUE(success);

  // Convert output array to a blob for comparison.
  SecureBlob output_blob(output->data, output->len);
  EXPECT_EQ(decrypted_blob, output_blob);

  // Assume we fail to read the data, we should not return success.
  EXPECT_CALL(platform, ReadFile(
      "/mnt/stateful_partition/unencrypted/preserve/enrollment_state.epb",
      _)).WillOnce(Return(false));

  EXPECT_TRUE(service.LoadEnrollmentState(&output, &success, &error));
  EXPECT_FALSE(success);
}

class MountExTest : public ::testing::Test {
 public:
  MountExTest() { }
  virtual ~MountExTest() { }

  void SetUp() {
    service_.set_attestation(&attest_);
    service_.set_homedirs(&homedirs_);
    service_.set_install_attrs(&attrs_);
    service_.set_initialize_tpm(false);
    service_.set_use_tpm(false);
    service_.set_platform(&platform_);
    service_.set_chaps_client(&chaps_client_);
    service_.set_reply_factory(&reply_factory_);
    // Empty token list by default.  The effect is that there are no attempts
    // to unload tokens unless a test explicitly sets up the token list.
    EXPECT_CALL(chaps_client_, GetTokenList(_, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, ReadFileToString(EndsWith("decrypt_stateful"), _))
        .WillRepeatedly(Return(false));
  }

  void TearDown() { }

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

 protected:
  NiceMock<MockAttestation> attest_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockInstallAttributes> attrs_;
  MockDBusReplyFactory reply_factory_;

  MockPlatform platform_;
  chaps::TokenManagerClientMock chaps_client_;
  Service service_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MountExTest);
};


TEST_F(MountExTest, InvalidArgsChecks) {
  // Fast path through Initialize()
  EXPECT_CALL(homedirs_, Init())
    .WillOnce(Return(true));
  // Skip the CleanUpStaleMounts bit.
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .WillRepeatedly(Return(false));
  ASSERT_TRUE(service_.Initialize());

  scoped_ptr<AccountIdentifier> id(new AccountIdentifier);
  scoped_ptr<AuthorizationRequest> auth(new AuthorizationRequest);
  scoped_ptr<MountRequest> req(new MountRequest);
  // Expect an error about missing email.
  GError* call_err = NULL;
  // |error| will be cleaned up by event_source_
  MockDBusErrorReply *error = new MockDBusErrorReply();
  EXPECT_CALL(reply_factory_, NewErrorReply(NULL, _))
    .WillOnce(DoAll(SaveArg<1>(&call_err), Return(error)));
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoMountEx(id.get(), auth.get(), req.get(), NULL);
  ASSERT_NE(call_err, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", call_err->message);
  g_error_free(call_err);
  call_err = NULL;

  error = new MockDBusErrorReply();
  EXPECT_CALL(reply_factory_, NewErrorReply(NULL, _))
    .WillOnce(DoAll(SaveArg<1>(&call_err), Return(error)));
  id->set_email("foo@gmail.com");
  service_.DoMountEx(id.get(), auth.get(), req.get(), NULL);
  ASSERT_NE(call_err, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", call_err->message);
  g_error_free(call_err);
  call_err = NULL;

  error = new MockDBusErrorReply();
  EXPECT_CALL(reply_factory_, NewErrorReply(NULL, _))
    .WillOnce(DoAll(SaveArg<1>(&call_err), Return(error)));
  id->set_email("foo@gmail.com");
  auth->mutable_key()->set_secret("");
  service_.DoMountEx(id.get(), auth.get(), req.get(), NULL);
  ASSERT_NE(call_err, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", call_err->message);
  g_error_free(call_err);
  call_err = NULL;

  error = new MockDBusErrorReply();
  EXPECT_CALL(reply_factory_, NewErrorReply(NULL, _))
    .WillOnce(DoAll(SaveArg<1>(&call_err), Return(error)));
  id->set_email("foo@gmail.com");
  auth->mutable_key()->set_secret("blerg");
  req->mutable_create();
  service_.DoMountEx(id.get(), auth.get(), req.get(), NULL);
  ASSERT_NE(call_err, reinterpret_cast<void *>(0));
  EXPECT_STREQ("CreateRequest supplied with no keys", call_err->message);
  g_error_free(call_err);
  call_err = NULL;

  error = new MockDBusErrorReply();
  EXPECT_CALL(reply_factory_, NewErrorReply(NULL, _))
    .WillOnce(DoAll(SaveArg<1>(&call_err), Return(error)));
  id->set_email("foo@gmail.com");
  auth->mutable_key()->set_secret("blerg");
  // Empty key
  // TODO(wad) Add remaining missing field tests and NULL tests
  req->mutable_create()->add_keys();
  service_.DoMountEx(id.get(), auth.get(), req.get(), NULL);
  ASSERT_NE(call_err, reinterpret_cast<void *>(0));
  EXPECT_STREQ("CreateRequest Keys are not fully specified",
               call_err->message);
  g_error_free(call_err);
  call_err = NULL;
}

class AddKeyExTest : public ::testing::Test {
 public:
  AddKeyExTest() { }
  virtual ~AddKeyExTest() { }

  void SetUp() {
    // Ensure service is cleanly mocked/faked.
    service_.set_attestation(&attest_);
    service_.set_homedirs(&homedirs_);
    service_.set_install_attrs(&attrs_);
    service_.set_initialize_tpm(false);
    service_.set_use_tpm(false);
    service_.set_platform(&platform_);
    service_.set_chaps_client(&chaps_client_);
    service_.set_reply_factory(&reply_factory_);
    service_.set_crypto(&crypto_);
    // Empty token list by default.  The effect is that there are no attempts
    // to unload tokens unless a test explicitly sets up the token list.
    EXPECT_CALL(chaps_client_, GetTokenList(_, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, ReadFileToString(EndsWith("decrypt_stateful"), _))
        .WillRepeatedly(Return(false));
    g_error_ = NULL;

    // Fast path through Initialize()
    EXPECT_CALL(homedirs_, Init())
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

  void SetupErrorReply() {
    g_error_ = NULL;
    // |error| will be cleaned up by event_source_
    MockDBusErrorReply *error = new MockDBusErrorReply();
    EXPECT_CALL(reply_factory_, NewErrorReply(NULL, _))
      .WillOnce(DoAll(SaveArg<1>(&g_error_), Return(error)));
  }

  void PrepareArguments() {
    id_.reset(new AccountIdentifier);
    auth_.reset(new AuthorizationRequest);
    req_.reset(new AddKeyRequest);
  }

 protected:
  NiceMock<MockAttestation> attest_;
  NiceMock<MockInstallAttributes> attrs_;
  NiceMock<MockCrypto> crypto_;
  NiceMock<MockHomeDirs> homedirs_;
  MockDBusReplyFactory reply_factory_;

  scoped_ptr<AccountIdentifier> id_;
  scoped_ptr<AuthorizationRequest> auth_;
  scoped_ptr<AddKeyRequest> req_;

  GError* g_error_;

  MockPlatform platform_;
  chaps::TokenManagerClientMock chaps_client_;
  Service service_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AddKeyExTest);
};


TEST_F(AddKeyExTest, InvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoAddKeyEx(id_.get(), auth_.get(), req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(AddKeyExTest, InvalidArgsNoSecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  service_.DoAddKeyEx(id_.get(), auth_.get(), req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(AddKeyExTest, InvalidArgsNoNewKeySet) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  req_->clear_key();
  service_.DoAddKeyEx(id_.get(), auth_.get(), req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No new key supplied", g_error_->message);
}

TEST_F(AddKeyExTest, InvalidArgsNoKeyFilled) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  req_->mutable_key();
  service_.DoAddKeyEx(id_.get(), auth_.get(), req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No new key supplied", g_error_->message);
}

TEST_F(AddKeyExTest, InvalidArgsNoNewKeyLabel) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_email("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  req_->mutable_key();
  // No label
  req_->mutable_key()->set_secret("some secret");
  service_.DoAddKeyEx(id_.get(), auth_.get(), req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No new key label supplied", g_error_->message);
}

}  // namespace cryptohome
