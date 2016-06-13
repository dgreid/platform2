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
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <chaps/token_manager_client_mock.h>
#include <glib-object.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>
#include <chromeos/constants/cryptohome.h>

#include "cryptohome/crypto.h"
#include "cryptohome/interface.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_attestation.h"
#include "cryptohome/mock_boot_attributes.h"
#include "cryptohome/mock_boot_lockbox.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_dbus_transition.h"
#include "cryptohome/mock_firmware_management_parameters.h"
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
using brillo::SecureBlob;
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
using ::testing::SetArgPointee;
using ::testing::WithArgs;

namespace {

const char kImageDir[] = "test_image_dir";
const char kSaltFile[] = "test_image_dir/salt";
// Keep in sync with service.cc
const int64_t kNotifyDiskSpaceThreshold = 1 << 30;

class FakeEventSourceSink : public cryptohome::CryptohomeEventSourceSink {
 public:
  FakeEventSourceSink() = default;
  ~FakeEventSourceSink() override = default;

  void NotifyEvent(cryptohome::CryptohomeEventBase* result) override {
    if (strcmp(result->GetEventName(),
               cryptohome::kMountTaskResultEventType)) {
      return;
    }
    cryptohome::MountTaskResult* r =
        static_cast<cryptohome::MountTaskResult*>(result);
    completed_tasks_.push_back(*r);
  }

  std::vector<cryptohome::MountTaskResult> completed_tasks_;
};

bool AssignSalt(size_t size, SecureBlob* salt) {
  SecureBlob fake_salt(size, 'S');
  salt->swap(fake_salt);
  return true;
}

}  // namespace

namespace cryptohome {

// Tests that need to do more setup work before calling Service::Initialize can
// use this instead of ServiceTest.
class ServiceTestNotInitialized : public ::testing::Test {
 public:
  ServiceTestNotInitialized() = default;
  ~ServiceTestNotInitialized() override = default;

  void SetUp() override {
    service_.set_crypto(&crypto_);
    service_.set_attestation(&attest_);
    service_.set_homedirs(&homedirs_);
    service_.set_install_attrs(&attrs_);
    service_.set_initialize_tpm(false);
    service_.set_use_tpm(false);
    service_.set_platform(&platform_);
    service_.set_chaps_client(&chaps_client_);
    service_.set_boot_lockbox(&lockbox_);
    service_.set_boot_attributes(&boot_attributes_);
    service_.set_firmware_management_parameters(&fwmp_);
    service_.set_reply_factory(&reply_factory_);
    service_.set_event_source_sink(&event_sink_);
    test_helper_.SetUpSystemSalt();
    ON_CALL(homedirs_, FreeDiskSpace()).WillByDefault(Return(true));
    ON_CALL(homedirs_, Init(_, _, _)).WillByDefault(Return(true));
    ON_CALL(homedirs_, AmountOfFreeDiskSpace()).WillByDefault(
        Return(kNotifyDiskSpaceThreshold));
    ON_CALL(boot_attributes_, Load()).WillByDefault(Return(true));
    // Empty token list by default.  The effect is that there are no attempts
    // to unload tokens unless a test explicitly sets up the token list.
    ON_CALL(chaps_client_, GetTokenList(_, _)).WillByDefault(Return(true));
    // Skip CleanUpStaleMounts by default.
    ON_CALL(platform_, GetMountsBySourcePrefix(_, _))
        .WillByDefault(Return(false));
    // Setup fake salt by default.
    ON_CALL(crypto_, GetOrCreateSalt(_, _, _, _))
        .WillByDefault(WithArgs<1, 3>(Invoke(AssignSalt)));
    // Skip StatefulRecovery by default.
    ON_CALL(platform_, ReadFileToString(EndsWith("decrypt_stateful"), _))
        .WillByDefault(Return(false));
  }

  void SetupMount(const std::string& username) {
    mount_ = new NiceMock<MockMount>();
    service_.set_mount_for_user(username, mount_.get());
  }

  void TearDown() override {
    test_helper_.TearDownSystemSalt();
  }

 protected:
  MakeTests test_helper_;
  NiceMock<MockCrypto> crypto_;
  NiceMock<MockAttestation> attest_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockInstallAttributes> attrs_;
  NiceMock<MockBootLockbox> lockbox_;
  NiceMock<MockBootAttributes> boot_attributes_;
  NiceMock<MockFirmwareManagementParameters> fwmp_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockDBusReplyFactory> reply_factory_;
  NiceMock<chaps::TokenManagerClientMock> chaps_client_;
  FakeEventSourceSink event_sink_;
  scoped_refptr<MockMount> mount_;
  // Declare service_ last so it gets destroyed before all the mocks. This is
  // important because otherwise the background thread may call into mocks that
  // have already been destroyed.
  Service service_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceTestNotInitialized);
};

class ServiceTest : public ServiceTestNotInitialized {
 public:
  ServiceTest() = default;
  ~ServiceTest() override = default;

  void SetUp() override {
    ServiceTestNotInitialized::SetUp();
    ASSERT_TRUE(service_.Initialize());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceTest);
};

TEST_F(ServiceTest, CheckKeySuccessTest) {
  char user[] = "chromeos-user";
  char key[] = "274146c6e8886a843ddfea373e2dc71b";
  SetupMount(user);
  EXPECT_CALL(*mount_, AreSameUser(_))
      .WillOnce(Return(false));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_))
      .WillOnce(Return(true));
  gboolean out = FALSE;
  GError *error = NULL;
  EXPECT_TRUE(service_.CheckKey(user, key, &out, &error));
  EXPECT_TRUE(out);
}

TEST_F(ServiceTest, CheckKeyMountTest) {
  static const char kUser[] = "chromeos-user";
  static const char kKey[] = "274146c6e8886a843ddfea373e2dc71b";
  SetupMount(kUser);
  scoped_ptr<AccountIdentifier> id(new AccountIdentifier);
  scoped_ptr<AuthorizationRequest> auth(new AuthorizationRequest);
  scoped_ptr<CheckKeyRequest> req(new CheckKeyRequest);
  id->set_account_id(kUser);
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

TEST_F(ServiceTest, CheckKeyHomedirsTest) {
  static const char kUser[] = "chromeos-user";
  static const char kKey[] = "274146c6e8886a843ddfea373e2dc71b";
  SetupMount(kUser);
  scoped_ptr<AccountIdentifier> id(new AccountIdentifier);
  scoped_ptr<AuthorizationRequest> auth(new AuthorizationRequest);
  scoped_ptr<CheckKeyRequest> req(new CheckKeyRequest);
  // Expect an error about missing email.
  // |error| will be cleaned up by event_source_
  MockDBusReply* reply = new MockDBusReply();
  std::string* base_reply_ptr = NULL;
  id->set_account_id(kUser);
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

TEST_F(ServiceTestNotInitialized, CheckAsyncTestCredentials) {
  // Setup a real homedirs instance (making this a pseudo-integration test).
  test_helper_.InjectSystemSalt(&platform_, kSaltFile);
  test_helper_.InitTestData(kImageDir, kDefaultUsers, 1);
  TestUser* user = &test_helper_.users[0];
  user->InjectKeyset(&platform_);
  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(user->password,
                                        test_helper_.system_salt, &passkey);
  std::string passkey_string = passkey.to_string();
  Crypto real_crypto(&platform_);
  real_crypto.set_use_tpm(false);
  real_crypto.Init(nullptr);
  HomeDirs real_homedirs;
  real_homedirs.set_crypto(&real_crypto);
  real_homedirs.set_shadow_root(kImageDir);
  real_homedirs.set_platform(&platform_);
  policy::PolicyProvider policy_provider(
      new NiceMock<policy::MockDevicePolicy>);
  real_homedirs.set_policy_provider(&policy_provider);
  // Avoid calling FreeDiskSpace routine.
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(_))
      .WillRepeatedly(Return(kMinFreeSpaceInBytes * 2));
  service_.set_homedirs(&real_homedirs);
  service_.set_crypto(&real_crypto);
  service_.Initialize();

  gboolean out = FALSE;
  GError *error = NULL;
  gint async_id = -1;
  EXPECT_TRUE(service_.AsyncCheckKey(
      const_cast<gchar*>(static_cast<const gchar*>(user->username)),
      const_cast<gchar*>(static_cast<const gchar*>(passkey_string.c_str())),
      &async_id,
      &error));
  EXPECT_NE(-1, async_id);
  for (unsigned int i = 0; i < 64; i++) {
    bool found = false;
    service_.DispatchEvents();
    for (unsigned int j = 0; j < event_sink_.completed_tasks_.size(); j++) {
      if (event_sink_.completed_tasks_[j].sequence_id() == async_id) {
        out = event_sink_.completed_tasks_[j].return_status();
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

TEST_F(ServiceTest, GetPublicMountPassKey) {
  const char kPublicUser1[] = "public_user_1";
  const char kPublicUser2[] = "public_user_2";
  std::string public_user1_passkey;
  service_.GetPublicMountPassKey(kPublicUser1, &public_user1_passkey);
  std::string public_user2_passkey;
  service_.GetPublicMountPassKey(kPublicUser2, &public_user2_passkey);
  // The passkey should be different for different user.
  EXPECT_NE(public_user1_passkey, public_user2_passkey);
  std::string public_user1_passkey2;
  service_.GetPublicMountPassKey(kPublicUser1, &public_user1_passkey2);
  // The passkey should be the same for the same user.
  EXPECT_EQ(public_user1_passkey, public_user1_passkey2);
}

TEST_F(ServiceTest, GetSanitizedUsername) {
  char username[] = "chromeos-user";
  gchar *sanitized = NULL;
  GError *error = NULL;
  EXPECT_TRUE(service_.GetSanitizedUsername(username, &sanitized, &error));
  EXPECT_TRUE(error == NULL);
  ASSERT_TRUE(sanitized);

  const std::string expected(
      brillo::cryptohome::home::SanitizeUserName(username));
  EXPECT_FALSE(expected.empty());

  EXPECT_EQ(expected, sanitized);
  g_free(sanitized);
}

TEST_F(ServiceTestNotInitialized, CheckAutoCleanupCallback) {
  // Checks that AutoCleanupCallback() is called periodically.
  // Service will schedule periodic clean-ups. Wait a bit and make
  // sure that we had at least 3 executed.
  EXPECT_CALL(homedirs_, FreeDiskSpace())
      .Times(::testing::AtLeast(3));
  SetupMount("some-user-to-clean-up");
  EXPECT_CALL(*mount_, UpdateCurrentUserActivityTimestamp(0))
      .Times(::testing::AtLeast(3));

  service_.set_auto_cleanup_period(2);  // 2ms = 500HZ
  service_.set_update_user_activity_period(2);  // 2 x 5ms = 25HZ
  service_.Initialize();
  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
}

TEST_F(ServiceTestNotInitialized, CheckAutoCleanupCallbackFirst) {
  // Checks that AutoCleanupCallback() is called first right after init.
  // Service will schedule first cleanup right after its init.
  EXPECT_CALL(homedirs_, FreeDiskSpace())
      .Times(1);
  service_.set_auto_cleanup_period(1000);  // 1s - long enough
  service_.Initialize();
  // short delay to see the first invocation
  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(10));
}

static gboolean SignalCounter(GSignalInvocationHint *ihint,
              guint n_param_values,
              const GValue *param_values,
              gpointer data) {
  int *count = reinterpret_cast<int *>(data);
  (*count)++;
  return true;
}

TEST_F(ServiceTestNotInitialized, CheckLowDiskCallback) {
  // Checks that LowDiskCallback is called periodically.
  EXPECT_CALL(homedirs_, AmountOfFreeDiskSpace()).Times(::testing::AtLeast(3))
      .WillOnce(Return(kNotifyDiskSpaceThreshold + 1))
      .WillOnce(Return(kNotifyDiskSpaceThreshold - 1))
      .WillRepeatedly(Return(kNotifyDiskSpaceThreshold + 1));
  service_.set_low_disk_notification_period_ms(2);

  guint low_disk_space_signal = g_signal_lookup("low_disk_space",
                                           gobject::cryptohome_get_type());
  if (!low_disk_space_signal) {
    low_disk_space_signal = g_signal_new("low_disk_space",
                                         gobject::cryptohome_get_type(),
                                         G_SIGNAL_RUN_LAST,
                                         0,
                                         NULL,
                                         NULL,
                                         nullptr,
                                         G_TYPE_NONE,
                                         1,
                                         G_TYPE_UINT64);
  }
  int count = 0;
  gulong hook_id = g_signal_add_emission_hook(
      low_disk_space_signal, 0, SignalCounter, &count, NULL);

  service_.Initialize();

  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
  EXPECT_EQ(1, count);
  g_signal_remove_emission_hook(low_disk_space_signal, hook_id);
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

bool StaleShadowMounts(
    const std::string& from_prefix,
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

TEST_F(ServiceTest, CleanUpStale_EmptyMap_NoOpenFiles_ShadowOnly) {
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

TEST_F(ServiceTest, CleanUpStale_EmptyMap_OpenLegacy_ShadowOnly) {
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
    .WillRepeatedly(SetArgPointee<1>(processes));
  EXPECT_CALL(platform_, Unmount(EndsWith("/1"), true, _))
    .Times(2)
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(service_.CleanUpStaleMounts(false));
}

TEST_F(ServiceTestNotInitialized,
       CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly) {
  // Checks that when we have a bunch of stale shadow mounts, some active
  // mounts, and no open filehandles, all inactive mounts are unmounted.

  // ownership handed off to the Service MountMap
  MockMountFactory mount_factory;
  MockMount* mount = new MockMount();
  EXPECT_CALL(mount_factory, New())
    .WillOnce(Return(mount));
  service_.set_mount_factory(&mount_factory);
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .Times(3)
    .WillRepeatedly(Return(false));
  ASSERT_TRUE(service_.Initialize());

  EXPECT_CALL(*mount, Init(&platform_, service_.crypto(), _))
    .WillOnce(Return(true));
  EXPECT_CALL(*mount, MountCryptohome(_, _, _))
    .WillOnce(Return(true));
  EXPECT_CALL(*mount, UpdateCurrentUserActivityTimestamp(_))
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

  EXPECT_CALL(*mount, OwnsMountPoint(_))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(*mount, OwnsMountPoint("/home/user/1"))
    .WillOnce(Return(true));
  EXPECT_CALL(*mount, OwnsMountPoint("/home/root/1"))
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
      .WillRepeatedly(DoAll(SetArgPointee<1>(fake_token_list),
                            Return(true)));

  EXPECT_CALL(chaps_client_,
              UnloadToken(_, base::FilePath("/home/chronos/user/token")))
      .Times(1);

  // Expect that CleanUpStaleMounts() tells us it skipped no mounts.
  EXPECT_FALSE(service_.CleanUpStaleMounts(false));
}

TEST_F(ServiceTest, StoreEnrollmentState) {
  brillo::glib::ScopedArray test_array(g_array_new(FALSE, FALSE, 1));
  std::string data = "123456";
  g_array_append_vals(test_array.get(), data.data(), data.length());

  // Helper strings for setting install attributes.
  static const char true_str[] = "true";
  const brillo::Blob true_value(true_str, true_str + arraysize(true_str));

  static const char false_str[] = "false";
  const brillo::Blob false_value(false_str, false_str + arraysize(false_str));

  gboolean success;
  GError* error = NULL;

  // Set us as non-enterprise enrolled.
  EXPECT_CALL(attrs_, Get("enterprise.owned", _)).WillOnce(
      DoAll(SetArgPointee<1>(false_value), Return(true)));
  service_.DetectEnterpriseOwnership();

  // Should not enterprise-enroll this device.
  EXPECT_TRUE(service_.StoreEnrollmentState(test_array.get(), &success,
                                            &error));
  EXPECT_FALSE(success);

  // Set us as enterprise enrolled.
  EXPECT_CALL(attrs_, Get("enterprise.owned", _)).WillOnce(
      DoAll(SetArgPointee<1>(true_value), Return(true)));
  service_.DetectEnterpriseOwnership();

  std::string encrypted_data = "so_encrypted";

  // Test successful encryption.
  EXPECT_CALL(crypto_, EncryptWithTpm(_, _)).WillOnce(DoAll(
      SetArgPointee<1>(encrypted_data), Return(true)));

  // Should write file as this device is enterprise enrolled.
  EXPECT_CALL(platform_, WriteStringToFileAtomicDurable(
      "/mnt/stateful_partition/unencrypted/preserve/enrollment_state.epb",
      encrypted_data, _)).WillOnce(Return(true));
  EXPECT_TRUE(service_.StoreEnrollmentState(test_array.get(), &success,
                                            &error));
  EXPECT_TRUE(success);

  EXPECT_TRUE(service_.homedirs()->enterprise_owned());
}

TEST_F(ServiceTest, LoadEnrollmentState) {
  gboolean success;
  GError* error = NULL;
  brillo::glib::ScopedArray output;

  // Convert to blob -- this is what we're reading from the file.
  std::string data = "123456";
  const brillo::Blob data_blob(data.c_str(), data.c_str() + data.length());

  SecureBlob decrypted_blob("decrypted");

  // Assume the data is there, we should return the value and success.
  EXPECT_CALL(platform_, ReadFile(
      "/mnt/stateful_partition/unencrypted/preserve/enrollment_state.epb",
      _)).WillOnce(DoAll(SetArgPointee<1>(data_blob), Return(true)));

  EXPECT_CALL(crypto_, DecryptWithTpm(_, _)).WillOnce(DoAll(
      SetArgPointee<1>(decrypted_blob), Return(TRUE)));

  EXPECT_TRUE(service_.LoadEnrollmentState(
      &(brillo::Resetter(&output).lvalue()), &success, &error));
  EXPECT_TRUE(success);

  // Convert output array to a blob for comparison.
  SecureBlob output_blob(output->data, output->data + output->len);
  EXPECT_EQ(decrypted_blob, output_blob);

  // Assume we fail to read the data, we should not return success.
  EXPECT_CALL(platform_, ReadFile(
      "/mnt/stateful_partition/unencrypted/preserve/enrollment_state.epb",
      _)).WillOnce(Return(false));

  EXPECT_TRUE(service_.LoadEnrollmentState(
      &(brillo::Resetter(&output).lvalue()), &success, &error));
  EXPECT_FALSE(success);
}

class ServiceExTest : public ServiceTest {
 public:
  ServiceExTest() = default;
  ~ServiceExTest() override = default;

  void TearDown() override {
    if (g_error_) {
      g_error_free(g_error_);
      g_error_ = NULL;
    }
    ServiceTest::TearDown();
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
  brillo::SecureBlob BlobFromProtobuf(const ProtoBuf& pb) {
    std::string serialized;
    CHECK(pb.SerializeToString(&serialized));
    return brillo::SecureBlob(serialized);
  }

 protected:
  scoped_ptr<AccountIdentifier> id_;
  scoped_ptr<AuthorizationRequest> auth_;
  scoped_ptr<AddKeyRequest> add_req_;
  scoped_ptr<CheckKeyRequest> check_req_;
  scoped_ptr<MountRequest> mount_req_;
  scoped_ptr<RemoveKeyRequest> remove_req_;
  scoped_ptr<ListKeysRequest> list_keys_req_;

  GError* g_error_{nullptr};
  std::string* reply_{nullptr};

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceExTest);
};

TEST_F(ServiceExTest, MountInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ServiceExTest, MountInvalidArgsNoSecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ServiceExTest, MountInvalidArgsEmptySecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("");
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ServiceExTest, MountInvalidArgsCreateWithNoKey) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create();
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("CreateRequest supplied with no keys", g_error_->message);
}

TEST_F(ServiceExTest, MountInvalidArgsCreateWithEmptyKey) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create()->add_keys();
  // TODO(wad) Add remaining missing field tests and NULL tests
  service_.DoMountEx(id_.get(), auth_.get(), mount_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("CreateRequest Keys are not fully specified",
               g_error_->message);
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoSecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoNewKeySet) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  add_req_->clear_key();
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No new key supplied", g_error_->message);
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoKeyFilled) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No new key supplied", g_error_->message);
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoNewKeyLabel) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  // No label
  add_req_->mutable_key()->set_secret("some secret");
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No new key label supplied", g_error_->message);
}

TEST_F(ServiceExTest, CheckKeyInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoCheckKeyEx(id_.get(), auth_.get(), check_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ServiceExTest, CheckKeyInvalidArgsNoSecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  service_.DoCheckKeyEx(id_.get(), auth_.get(), check_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ServiceExTest, CheckKeyInvalidArgsEmptySecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("");
  service_.DoCheckKeyEx(id_.get(), auth_.get(), check_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ServiceExTest, RemoveKeyInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ServiceExTest, RemoveKeyInvalidArgsNoSecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ServiceExTest, RemoveKeyInvalidArgsEmptySecret) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("");
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No key secret supplied", g_error_->message);
}

TEST_F(ServiceExTest, RemoveKeyInvalidArgsEmptyRemoveLabel) {
  SetupErrorReply();
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("some secret");
  remove_req_->mutable_key()->mutable_data();
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No label provided for target key", g_error_->message);
}

TEST_F(ServiceExTest, BootLockboxSignSuccess) {
  SetupReply();
  SecureBlob test_signature("test");
  EXPECT_CALL(lockbox_, Sign(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(test_signature),
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

TEST_F(ServiceExTest, BootLockboxSignBadArgs) {
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

TEST_F(ServiceExTest, BootLockboxSignError) {
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

TEST_F(ServiceExTest, BootLockboxVerifySuccess) {
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

TEST_F(ServiceExTest, BootLockboxVerifyBadArgs) {
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

TEST_F(ServiceExTest, BootLockboxVerifyError) {
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

TEST_F(ServiceExTest, BootLockboxFinalizeSuccess) {
  SetupReply();
  EXPECT_CALL(lockbox_, FinalizeBoot())
      .WillRepeatedly(Return(true));

  FinalizeBootLockboxRequest request;
  service_.DoFinalizeBootLockbox(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
  EXPECT_FALSE(reply.HasExtension(SignBootLockboxReply::reply));
}

TEST_F(ServiceExTest, BootLockboxFinalizeBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoFinalizeBootLockbox(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ServiceExTest, BootLockboxFinalizeError) {
  SetupReply();
  EXPECT_CALL(lockbox_, FinalizeBoot())
      .WillRepeatedly(Return(false));

  FinalizeBootLockboxRequest request;
  service_.DoFinalizeBootLockbox(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_TPM_COMM_ERROR, reply.error());
}

TEST_F(ServiceExTest, GetBootAttributeSuccess) {
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

TEST_F(ServiceExTest, GetBootAttributeBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoGetBootAttribute(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ServiceExTest, GetBootAttributeError) {
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

TEST_F(ServiceExTest, SetBootAttributeSuccess) {
  SetupReply();
  SetBootAttributeRequest request;
  request.set_name("test");
  request.set_value("1234");
  service_.DoSetBootAttribute(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
}

TEST_F(ServiceExTest, SetBootAttributeBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoSetBootAttribute(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ServiceExTest, FlushAndSignBootAttributesSuccess) {
  SetupReply();
  EXPECT_CALL(boot_attributes_, FlushAndSign())
      .WillRepeatedly(Return(true));

  FlushAndSignBootAttributesRequest request;
  service_.DoFlushAndSignBootAttributes(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
}

TEST_F(ServiceExTest, FlushAndSignBootAttributesBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoFlushAndSignBootAttributes(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ServiceExTest, FlushAndSignBootAttributesError) {
  SetupReply();
  EXPECT_CALL(boot_attributes_, FlushAndSign())
      .WillRepeatedly(Return(false));

  FlushAndSignBootAttributesRequest request;
  service_.DoFlushAndSignBootAttributes(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN, reply.error());
}

TEST_F(ServiceExTest, GetLoginStatusSuccess) {
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

TEST_F(ServiceExTest, GetLoginStatusBadArgs) {
  // Try with bad proto data.
  SetupErrorReply();
  service_.DoVerifyBootLockbox(SecureBlob("not_a_protobuf"), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STRNE("", g_error_->message);
}

TEST_F(ServiceExTest, GetKeyDataExNoMatch) {
  SetupReply();
  PrepareArguments();

  EXPECT_CALL(homedirs_, Exists(_))
      .WillRepeatedly(Return(true));

  id_->set_account_id("unittest@example.com");
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

TEST_F(ServiceExTest, GetKeyDataExOneMatch) {
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
      .WillRepeatedly(Invoke(this, &ServiceExTest::GetNiceMockVaultKeyset));

  id_->set_account_id("unittest@example.com");
  service_.DoGetKeyDataEx(id_.get(), auth_.get(), &req, NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());

  GetKeyDataReply sub_reply = reply.GetExtension(GetKeyDataReply::reply);
  ASSERT_EQ(1, sub_reply.key_data_size());
  EXPECT_EQ(std::string(kExpectedLabel), sub_reply.key_data(0).label());
}

TEST_F(ServiceExTest, GetKeyDataInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  GetKeyDataRequest req;
  service_.DoGetKeyDataEx(id_.get(), auth_.get(), &req, NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ServiceExTest, ListKeysInvalidArgsNoEmail) {
  SetupErrorReply();
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoListKeysEx is called directly.
  service_.DoListKeysEx(id_.get(), auth_.get(), list_keys_req_.get(), NULL);
  ASSERT_NE(g_error_, reinterpret_cast<void *>(0));
  EXPECT_STREQ("No email supplied", g_error_->message);
}

TEST_F(ServiceExTest, GetFirmwareManagementParametersSuccess) {
  brillo::SecureBlob hash("its_a_hash");
  SetupReply();

  EXPECT_CALL(fwmp_, Load())
    .WillOnce(Return(true));
  EXPECT_CALL(fwmp_, GetFlags(_))
    .WillRepeatedly(DoAll(SetArgPointee<0>(0x1234), Return(true)));
  EXPECT_CALL(fwmp_, GetDeveloperKeyHash(_))
    .WillRepeatedly(DoAll(SetArgPointee<0>(hash), Return(true)));

  GetFirmwareManagementParametersRequest request;
  service_.DoGetFirmwareManagementParameters(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
  EXPECT_TRUE(reply.HasExtension(GetFirmwareManagementParametersReply::reply));
  EXPECT_EQ(
      reply.GetExtension(GetFirmwareManagementParametersReply::reply).flags(),
      0x1234);
  EXPECT_EQ(reply.GetExtension(
      GetFirmwareManagementParametersReply::reply).developer_key_hash(),
      hash.to_string());
}

TEST_F(ServiceExTest, GetFirmwareManagementParametersError) {
  SetupReply();

  EXPECT_CALL(fwmp_, Load())
    .WillRepeatedly(Return(false));

  GetFirmwareManagementParametersRequest request;
  service_.DoGetFirmwareManagementParameters(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID,
            reply.error());
}

TEST_F(ServiceExTest, SetFirmwareManagementParametersSuccess) {
  brillo::SecureBlob hash("its_a_hash");
  brillo::Blob out_hash;
  SetupReply();

  EXPECT_CALL(fwmp_, Create())
    .WillOnce(Return(true));
  EXPECT_CALL(fwmp_, Store(0x1234, _))
    .WillOnce(DoAll(SaveArgPointee<1>(&out_hash), Return(true)));

  SetFirmwareManagementParametersRequest request;
  request.set_flags(0x1234);
  request.set_developer_key_hash(hash.to_string());
  service_.DoSetFirmwareManagementParameters(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
  EXPECT_EQ(hash, out_hash);
}

TEST_F(ServiceExTest, SetFirmwareManagementParametersNoHash) {
  brillo::Blob hash(0);
  SetupReply();

  EXPECT_CALL(fwmp_, Create())
    .WillOnce(Return(true));
  EXPECT_CALL(fwmp_, Store(0x1234, NULL))
    .WillOnce(Return(true));

  SetFirmwareManagementParametersRequest request;
  request.set_flags(0x1234);
  service_.DoSetFirmwareManagementParameters(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
}

TEST_F(ServiceExTest, SetFirmwareManagementParametersCreateError) {
  brillo::SecureBlob hash("its_a_hash");
  SetupReply();

  EXPECT_CALL(fwmp_, Create())
    .WillOnce(Return(false));

  SetFirmwareManagementParametersRequest request;
  request.set_flags(0x1234);
  request.set_developer_key_hash(hash.to_string());
  service_.DoSetFirmwareManagementParameters(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE,
            reply.error());
}

TEST_F(ServiceExTest, SetFirmwareManagementParametersStoreError) {
  brillo::SecureBlob hash("its_a_hash");
  SetupReply();

  EXPECT_CALL(fwmp_, Create())
    .WillOnce(Return(true));
  EXPECT_CALL(fwmp_, Store(_, _))
    .WillOnce(Return(false));

  SetFirmwareManagementParametersRequest request;
  request.set_flags(0x1234);
  request.set_developer_key_hash(hash.to_string());
  service_.DoSetFirmwareManagementParameters(BlobFromProtobuf(request), NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE,
            reply.error());
}

TEST_F(ServiceExTest, RemoveFirmwareManagementParametersSuccess) {
  SetupReply();

  EXPECT_CALL(fwmp_, Destroy())
    .WillOnce(Return(true));

  RemoveFirmwareManagementParametersRequest request;
  service_.DoRemoveFirmwareManagementParameters(BlobFromProtobuf(request),
                                                NULL);
  BaseReply reply = GetLastReply();
  EXPECT_FALSE(reply.has_error());
}

TEST_F(ServiceExTest, RemoveFirmwareManagementParametersError) {
  SetupReply();

  EXPECT_CALL(fwmp_, Destroy())
    .WillOnce(Return(false));

  RemoveFirmwareManagementParametersRequest request;
  service_.DoRemoveFirmwareManagementParameters(BlobFromProtobuf(request),
                                                NULL);
  BaseReply reply = GetLastReply();
  EXPECT_TRUE(reply.has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE,
            reply.error());
}

}  // namespace cryptohome
