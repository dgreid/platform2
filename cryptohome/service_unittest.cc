// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Unit tests for Service and ServiceMonolithic

#include "cryptohome/service_distributed.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/stl_util.h>
#include <base/strings/sys_string_conversions.h>
#include <base/synchronization/lock.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <chaps/token_manager_client_mock.h>
#include <chromeos/constants/cryptohome.h>
#include <glib-object.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>

#include "cryptohome/bootlockbox/mock_boot_attributes.h"
#include "cryptohome/bootlockbox/mock_boot_lockbox.h"
#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/challenge_credentials/mock_challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/disk_cleanup.h"
#include "cryptohome/glib_transition.h"
#include "cryptohome/interface.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_arc_disk_quota.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_disk_cleanup.h"
#include "cryptohome/mock_fingerprint_manager.h"
#include "cryptohome/mock_firmware_management_parameters.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_key_challenge_service.h"
#include "cryptohome/mock_key_challenge_service_factory.h"
#include "cryptohome/mock_mount.h"
#include "cryptohome/mock_mount_factory.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_tpm_init.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/protobuf_test_utils.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/user_session.h"

using base::FilePath;
using base::PlatformThread;
using brillo::SecureBlob;
using ::testing::_;
using ::testing::AnyOf;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::EndsWith;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::WithArgs;

namespace cryptohome {

namespace {

constexpr char kImageDir[] = "test_image_dir";
constexpr char kSaltFile[] = "test_image_dir/salt";

class FakeEventSourceSink : public CryptohomeEventSourceSink {
 public:
  FakeEventSourceSink() = default;
  ~FakeEventSourceSink() override = default;

  void NotifyEvent(CryptohomeEventBase* event) override {
    const std::string event_name = event->GetEventName();
    if (event_name == kDBusBlobReplyEventType) {
      EXPECT_FALSE(reply_);
      EXPECT_FALSE(error_reply_);
      auto* dbus_reply = static_cast<const DBusBlobReply*>(event);
      BaseReply parsed_reply;
      EXPECT_TRUE(parsed_reply.ParseFromString(dbus_reply->reply()));
      reply_ = std::make_unique<BaseReply>(parsed_reply);
    } else if (event_name == kDBusErrorReplyEventType) {
      EXPECT_FALSE(reply_);
      EXPECT_FALSE(error_reply_);
      auto* dbus_error_reply = static_cast<const DBusErrorReply*>(event);
      error_reply_ =
          std::make_unique<std::string>(dbus_error_reply->error().message);
    } else if (event_name == kClosureEventType) {
      ClosureEvent* closure_event = static_cast<ClosureEvent*>(event);
      closure_event->Run();
    }
  }

  const BaseReply* reply() const { return reply_.get(); }

  const std::string* error_reply() const { return error_reply_.get(); }

  void ClearReplies() {
    reply_.reset();
    error_reply_.reset();
  }

 private:
  std::unique_ptr<BaseReply> reply_;
  std::unique_ptr<std::string> error_reply_;

  DISALLOW_COPY_AND_ASSIGN(FakeEventSourceSink);
};

bool AssignSalt(size_t size, SecureBlob* salt) {
  SecureBlob fake_salt(size, 'S');
  salt->swap(fake_salt);
  return true;
}

bool ProtosAreEqual(const google::protobuf::MessageLite& lhs,
                    const google::protobuf::MessageLite& rhs) {
  std::string serialized_lhs, serialized_rhs;
  return lhs.SerializeToString(&serialized_lhs) &&
         rhs.SerializeToString(&serialized_rhs) &&
         serialized_lhs == serialized_rhs;
}

bool GetInstallAttributesIsReady(Service* service) {
  gboolean result = false;
  GError* error_result = nullptr;
  EXPECT_TRUE(service->InstallAttributesIsReady(&result, &error_result));
  return result;
}

bool GetInstallAttributesIsInvalid(Service* service) {
  gboolean result = false;
  GError* error_result = nullptr;
  EXPECT_TRUE(service->InstallAttributesIsInvalid(&result, &error_result));
  return result;
}

bool GetInstallAttributesIsFirstInstall(Service* service) {
  gboolean result = false;
  GError* error_result = nullptr;
  EXPECT_TRUE(service->InstallAttributesIsFirstInstall(&result, &error_result));
  return result;
}

}  // namespace

// We use this subclass to bypass those objects that are lack of proper
// mechanism in |ServiceDistributed|.
class ServiceDistributedNoRealDBus : public ServiceDistributed {
 public:
  ServiceDistributedNoRealDBus() {
    // We don't use |fingerprint_manager_|, so we just let |this| takes the
    // ownership of the mock; by doing this, we can bypass the construction of
    // real |FingerprintManager|.
    fingerprint_manager_ = std::make_unique<NiceMock<MockFingerprintManager>>();
  }
  ~ServiceDistributedNoRealDBus() = default;

 protected:
  // The signal doesn't work in unittets.
  void ConnectOwnershipTakenSignal() override {}
};

// Tests that need to do more setup work before calling Service::Initialize can
// use this instead of ServiceTest.
class ServiceTestNotInitialized : public ::testing::Test {
 public:
  ServiceTestNotInitialized() = default;
  ~ServiceTestNotInitialized() override = default;

  void SetUp() override {
    service_.set_crypto(&crypto_);
    service_.set_homedirs(&homedirs_);
    service_.set_install_attrs(&attrs_);
    service_.set_initialize_tpm(false);
    service_.set_use_tpm(false);
    service_.set_platform(&platform_);
    service_.set_chaps_client(&chaps_client_);
    service_.set_boot_lockbox(&lockbox_);
    service_.set_boot_attributes(&boot_attributes_);
    service_.set_firmware_management_parameters(&fwmp_);
    service_.set_event_source_sink(&event_sink_);
    service_.set_arc_disk_quota(&arc_disk_quota_);
    service_.set_challenge_credentials_helper(&challenge_credentials_helper_);
    service_.set_key_challenge_service_factory(&key_challenge_service_factory_);
    test_helper_.SetUpSystemSalt();
    homedirs_.set_crypto(&crypto_);
    homedirs_.set_platform(&platform_);
    tpm_init_.set_tpm(&tpm_);
    ON_CALL(homedirs_, shadow_root()).WillByDefault(ReturnRef(kShadowRoot));
    ON_CALL(homedirs_, disk_cleanup()).WillByDefault(Return(&cleanup_));
    ON_CALL(homedirs_, Init(_, _, _)).WillByDefault(Return(true));
    // Return valid values for the amount of free space.
    ON_CALL(cleanup_, AmountOfFreeDiskSpace())
        .WillByDefault(Return(kFreeSpaceThresholdToTriggerCleanup));
    ON_CALL(cleanup_, GetFreeDiskSpaceState(_))
        .WillByDefault(Return(DiskCleanup::FreeSpaceState::kNeedNormalCleanup));
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
    ON_CALL(platform_,
            ReadFileToString(
                Property(&FilePath::value, EndsWith("decrypt_stateful")), _))
        .WillByDefault(Return(false));

    ON_CALL(arc_disk_quota_, Initialize()).WillByDefault(Return());
  }

  void SetupMount(const std::string& username) {
    brillo::SecureBlob salt;
    AssignSalt(CRYPTOHOME_DEFAULT_SALT_LENGTH, &salt);
    mount_ = new NiceMock<MockMount>();
    session_ = new UserSession(salt, mount_);
    service_.set_session_for_user(username, session_.get());
  }

  void TearDown() override { test_helper_.TearDownSystemSalt(); }

 protected:
  void DispatchEvents() { service_.DispatchEventsForTesting(); }

  const BaseReply* reply() const { return event_sink_.reply(); }

  bool ReplyIsEmpty() const {
    EXPECT_TRUE(reply());
    return reply() && ProtosAreEqual(BaseReply(), *reply());
  }

  const std::string* error_reply() const { return event_sink_.error_reply(); }

  void ClearReplies() { event_sink_.ClearReplies(); }

  MakeTests test_helper_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockTpmInit> tpm_init_;
  NiceMock<MockCrypto> crypto_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockDiskCleanup> cleanup_;
  NiceMock<MockInstallAttributes> attrs_;
  NiceMock<MockBootLockbox> lockbox_;
  NiceMock<MockBootAttributes> boot_attributes_;
  NiceMock<MockFirmwareManagementParameters> fwmp_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockArcDiskQuota> arc_disk_quota_;
  NiceMock<chaps::TokenManagerClientMock> chaps_client_;
  NiceMock<MockChallengeCredentialsHelper> challenge_credentials_helper_;
  NiceMock<MockKeyChallengeServiceFactory> key_challenge_service_factory_;
  FakeEventSourceSink event_sink_;
  scoped_refptr<MockMount> mount_;
  scoped_refptr<UserSession> session_;
  base::FilePath kShadowRoot = base::FilePath("/home/.shadow");
  // Declare service_ last so it gets destroyed before all the mocks. This is
  // important because otherwise the background thread may call into mocks that
  // have already been destroyed.
  ServiceDistributedNoRealDBus service_;

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

TEST_F(ServiceTestNotInitialized, CheckAsyncTestCredentials) {
  // Setup a real homedirs instance (making this a pseudo-integration test).
  test_helper_.InjectSystemSalt(&platform_, FilePath(kSaltFile));
  test_helper_.InitTestData(FilePath(kImageDir), kDefaultUsers, 1,
                            false /* force_ecryptfs */);
  TestUser* user = &test_helper_.users[0];
  user->InjectKeyset(&platform_);
  // Inject the dirs because InitTestData uses its own platform
  // mock for some reason...
  user->InjectUserPaths(&platform_, 1000, 1000, 1001, 0, false);
  EXPECT_CALL(platform_, DirectoryExists(user->base_path))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(user->password,
                                        test_helper_.system_salt, &passkey);
  std::string passkey_string = passkey.to_string();
  Crypto real_crypto(&platform_);
  real_crypto.set_use_tpm(false);
  real_crypto.Init(nullptr);
  HomeDirs real_homedirs;
  real_homedirs.set_crypto(&real_crypto);
  real_homedirs.set_shadow_root(FilePath(kImageDir));
  real_homedirs.set_platform(&platform_);
  policy::PolicyProvider policy_provider(
      std::unique_ptr<NiceMock<policy::MockDevicePolicy>>(
          new NiceMock<policy::MockDevicePolicy>));
  real_homedirs.set_policy_provider(&policy_provider);
  real_homedirs.set_disk_cleanup(&cleanup_);
  service_.set_homedirs(&real_homedirs);
  service_.set_crypto(&real_crypto);
  service_.Initialize();

  auto id = std::make_unique<AccountIdentifier>();
  id->set_account_id(user->username);
  auto auth = std::make_unique<AuthorizationRequest>();
  auth->mutable_key()->set_secret(passkey_string.c_str());
  auto req = std::make_unique<CheckKeyRequest>();

  // Run will never be called because we aren't running the event loop.
  service_.DoCheckKeyEx(std::move(id), std::move(auth), std::move(req), NULL);

  // Expect an empty reply as success.
  DispatchEvents();
  EXPECT_TRUE(ReplyIsEmpty());

  // Reset pointers from local variables that will be destroyed before
  // |service_|.
  service_.set_homedirs(&homedirs_);
  service_.set_crypto(&crypto_);
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
  gchar* sanitized = NULL;
  GError* error = NULL;
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
  // Checks that DoAutoCleanup() is called periodically.
  // Service will schedule periodic clean-ups.
  SetupMount("some-user-to-clean-up");

  // Check that UpdateCurrentUserActivityTimestamp happens daily.
  EXPECT_CALL(*mount_, UpdateCurrentUserActivityTimestamp(0, _))
      .Times(AtLeast(1));

  // These are shared between Mount and Platform threads, guarded by the lock.
  int free_disk_space_count = 0;
  base::Time current_time;
  base::Lock lock;

  // These will be invoked from the mount thread.
  EXPECT_CALL(cleanup_, FreeDiskSpace()).WillRepeatedly(Invoke([&]() {
    base::AutoLock scoped_lock(lock);
    ++free_disk_space_count;
  }));
  EXPECT_CALL(platform_, GetCurrentTime()).WillRepeatedly(Invoke([&]() {
    base::AutoLock scoped_lock(lock);
    return current_time;
  }));

  const int period_ms = 1;

  // This will cause the low disk space callback to be called every ms
  service_.set_low_disk_notification_period_ms(period_ms);
  service_.Initialize();

  // Make sure that we have at least 48 FreeDiskSpace calls executed.
  // (48 hourly callbacks == two days,
  // at least 1 UpdateCurrentUserActivityTimestamp)
  for (int count = 0; count < 48;) {
    {
      base::AutoLock scoped_lock(lock);
      // Advance platform time. Let each period_ms = 30 minutes.
      current_time += base::TimeDelta::FromMinutes(30);
      count = free_disk_space_count;
    }
    PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(period_ms));
  }

  // Currently low disk space callback runs every 1 ms. If that test callback
  // runs before we finish test teardown but after platform_ object is cleared,
  // then we'll get error. Therefore, we need to set test callback interval
  // back to 1 minute, so we will not have any race condition.
  service_.set_low_disk_notification_period_ms(60 * 1000);
  // Wait for the change to take effect.
  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(period_ms * 5));

  // Cleanup invocable lambdas so they don't capture this test variables anymore
  Mock::VerifyAndClear(&platform_);
  Mock::VerifyAndClear(&cleanup_);
}

TEST_F(ServiceTestNotInitialized, CheckAutoCleanupCallbackFirst) {
  // Checks that DoAutoCleanup() is called first right after init.
  // Service will schedule first cleanup right after its init.
  EXPECT_CALL(cleanup_, FreeDiskSpace()).Times(1);
  EXPECT_CALL(cleanup_, AmountOfFreeDiskSpace())
      .WillRepeatedly(Return(kFreeSpaceThresholdToTriggerCleanup + 1));
  EXPECT_CALL(cleanup_, GetFreeDiskSpaceState(_))
      .WillRepeatedly(Return(DiskCleanup::FreeSpaceState::kAboveThreshold));
  service_.set_low_disk_notification_period_ms(1000);  // 1s - long enough
  service_.Initialize();
  // short delay to see the first invocation
  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(10));
}

static gboolean SignalCounter(GSignalInvocationHint* ihint,
                              guint n_param_values,
                              const GValue* param_values,
                              gpointer data) {
  int* count = reinterpret_cast<int*>(data);
  (*count)++;
  return true;
}

TEST_F(ServiceTestNotInitialized, CheckLowDiskCallback) {
  // Checks that LowDiskCallback is called periodically.
  EXPECT_CALL(cleanup_, AmountOfFreeDiskSpace())
      .Times(AtLeast(5))
      .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup + 1))
      .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup + 1))
      .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup + 1))
      .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup - 1))
      .WillRepeatedly(Return(kFreeSpaceThresholdToTriggerCleanup + 1));
  EXPECT_CALL(cleanup_, GetFreeDiskSpaceState(_))
      .Times(AtLeast(5))
      .WillOnce(Return(DiskCleanup::FreeSpaceState::kAboveThreshold))
      .WillOnce(Return(DiskCleanup::FreeSpaceState::kAboveThreshold))
      .WillOnce(Return(DiskCleanup::FreeSpaceState::kAboveThreshold))
      .WillOnce(Return(DiskCleanup::FreeSpaceState::kNeedNormalCleanup))
      .WillRepeatedly(Return(DiskCleanup::FreeSpaceState::kAboveThreshold));
  // Checks that DoAutoCleanup is called second time ahead of schedule
  // if disk space goes below threshold and recovers back to normal.
  EXPECT_CALL(cleanup_, FreeDiskSpace()).Times(2);

  service_.set_low_disk_notification_period_ms(2);

  guint low_disk_space_signal =
      g_signal_lookup("low_disk_space", gobject::cryptohome_get_type());
  if (!low_disk_space_signal) {
    low_disk_space_signal = g_signal_new(
        "low_disk_space", gobject::cryptohome_get_type(), G_SIGNAL_RUN_LAST, 0,
        nullptr, nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_UINT64);
  }
  int count_signals = 0;
  gulong hook_id = g_signal_add_emission_hook(
      low_disk_space_signal, 0, SignalCounter, &count_signals, nullptr);

  service_.Initialize();

  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
  EXPECT_EQ(1, count_signals);
  g_signal_remove_emission_hook(low_disk_space_signal, hook_id);
}

TEST_F(ServiceTestNotInitialized, CheckLowDiskCallbackFreeDiskSpaceOnce) {
  EXPECT_CALL(cleanup_, AmountOfFreeDiskSpace())
      .Times(AtLeast(5))
      .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup + 1))
      .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup + 1))
      .WillOnce(Return(kFreeSpaceThresholdToTriggerCleanup + 1))
      .WillRepeatedly(Return(kFreeSpaceThresholdToTriggerCleanup - 1));
  EXPECT_CALL(cleanup_, GetFreeDiskSpaceState(_))
      .Times(AtLeast(5))
      .WillOnce(Return(DiskCleanup::FreeSpaceState::kAboveThreshold))
      .WillOnce(Return(DiskCleanup::FreeSpaceState::kAboveThreshold))
      .WillOnce(Return(DiskCleanup::FreeSpaceState::kAboveThreshold))
      .WillRepeatedly(Return(DiskCleanup::FreeSpaceState::kNeedNormalCleanup));
  // Checks that DoAutoCleanup is called second time ahead of schedule
  // if disk space goes below threshold and stays below forever.
  EXPECT_CALL(cleanup_, FreeDiskSpace()).Times(2);

  service_.set_low_disk_notification_period_ms(2);

  service_.Initialize();

  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
}

TEST_F(ServiceTestNotInitialized, UploadAlertsCallback) {
  NiceMock<MockTpm> tpm;
  NiceMock<MockTpmInit> tpm_init;
  tpm_init.set_tpm(&tpm);

  service_.set_use_tpm(true);
  service_.set_tpm(&tpm);
  service_.set_tpm_init(&tpm_init);
  service_.set_initialize_tpm(true);

  // Checks that LowDiskCallback is called periodically.
  EXPECT_CALL(tpm, GetAlertsData(_)).Times(::testing::AtLeast(1));

  service_.Initialize();

  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(100));
  // TODO(anatol): check that alerts are written to /var/lib/metrics/uma-events
}

TEST_F(ServiceTest, NoDeadlocksInInitializeTpmComplete) {
  char user[] = "chromeos-user";

  // OwnershipCallback needs tpm_init_.
  service_.set_tpm_init(&tpm_init_);

  SetupMount(user);

  // Put a task on mount_thread_ that starts before InitializeTpmComplete
  // and finishes after it exits. Verify it doesn't wait for
  // InitializeTpmComplete forever.
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  base::WaitableEvent event_stop(
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED);
  bool finished = false;
  service_.mount_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          [](bool* finished, base::WaitableEvent* event,
             base::WaitableEvent* event_stop) {
            event->Signal();  // Signal "Ready to start"
            // Wait up to 2s for InitializeTpmComplete to finish
            *finished = event_stop->TimedWait(base::TimeDelta::FromSeconds(2));
            event->Signal();  // Signal "Result ready"
          },
          &finished, &event, &event_stop));

  event.Wait();  // Wait for "Ready to start"
  service_.OwnershipCallback(true, true);
  event_stop.Signal();
  event.Wait();  // Wait for "Result ready"
  ASSERT_TRUE(finished);
}

struct Mounts {
  const FilePath src;
  const FilePath dst;
};

const std::vector<Mounts> kShadowMounts = {
    {FilePath("/home/.shadow/a"), FilePath("/home/root/0")},
    {FilePath("/home/.shadow/a"), FilePath("/home/user/0")},
    {FilePath("/home/.shadow/a"), FilePath("/home/chronos/user")},
    {FilePath("/home/.shadow/a/Downloads"),
     FilePath("/home/chronos/user/MyFiles/Downloads")},
    {FilePath("/home/.shadow/a/server/run"),
     FilePath("/daemon-store/server/a")},
    {FilePath("/home/.shadow/b"), FilePath("/home/root/1")},
    {FilePath("/home/.shadow/b"), FilePath("/home/user/1")},
    {FilePath("/home/.shadow/b/Downloads"),
     FilePath("/home/chronos/u-b/MyFiles/Downloads")},
    {FilePath("/home/.shadow/b/Downloads"),
     FilePath("/home/user/b/MyFiles/Downloads")},
    {FilePath("/home/.shadow/b/server/run"),
     FilePath("/daemon-store/server/b")},
};

// Ephemeral mounts must be at the beginning.
const std::vector<Mounts> kLoopDevMounts = {
    {FilePath("/dev/loop7"), FilePath("/run/cryptohome/ephemeral_mount/1")},
    {FilePath("/dev/loop7"), FilePath("/home/user/0")},
    {FilePath("/dev/loop7"), FilePath("/home/root/0")},
    {FilePath("/dev/loop7"), FilePath("/home/chronos/u-1")},
    {FilePath("/dev/loop7"), FilePath("/home/chronos/user")},
    {FilePath("/dev/loop1"), FilePath("/opt/google/containers")},
    {FilePath("/dev/loop2"), FilePath("/home/root/1")},
    {FilePath("/dev/loop2"), FilePath("/home/user/1")},
};

// The number of mount for loop7.
const int kEphemeralMountsCount = 5;

const std::vector<Platform::LoopDevice> kLoopDevices = {
    {FilePath("/mnt/stateful_partition/encrypted.block"),
     FilePath("/dev/loop0")},
    {FilePath("/run/cryptohome/ephemeral_data/1"), FilePath("/dev/loop7")},
};

const std::vector<FilePath> kSparseFiles = {
    FilePath("/run/cryptohome/ephemeral_data/2"),
    FilePath("/run/cryptohome/ephemeral_data/1"),
};

bool StaleShadowMounts(const FilePath& from_prefix,
                       std::multimap<const FilePath, const FilePath>* mounts) {
  int i = 0;

  for (const auto& m : kShadowMounts) {
    if (m.src.value().find(from_prefix.value()) == 0) {
      i++;
      if (mounts)
        mounts->insert(std::make_pair(m.src, m.dst));
    }
  }
  return i > 0;
}

bool LoopDeviceMounts(std::multimap<const FilePath, const FilePath>* mounts) {
  if (!mounts)
    return false;
  for (const auto& m : kLoopDevMounts)
    mounts->insert(std::make_pair(m.src, m.dst));
  return true;
}

bool EnumerateSparseFiles(const base::FilePath& path,
                          bool is_recursive,
                          std::vector<base::FilePath>* ent_list) {
  if (path != FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir))
    return false;
  ent_list->insert(ent_list->begin(), kSparseFiles.begin(), kSparseFiles.end());
  return true;
}

TEST_F(ServiceTest, CleanUpStale_NoOpenFiles_Ephemeral) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted, loop device is
  // detached and sparse file is deleted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(homedirs_.shadow_root(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(kLoopDevices));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kEphemeralMountsCount);

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(platform_, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(platform_, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[0], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[1], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kLoopDevMounts[0].dst, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(service_.CleanUpStaleMounts(false));
}

TEST_F(ServiceTest, CleanUpStale_OpenLegacy_Ephemeral) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, everything is kept.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(homedirs_.shadow_root(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(kLoopDevices));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kEphemeralMountsCount - 1);
  std::vector<ProcessInformation> processes(1);
  std::vector<std::string> cmd_line;
  processes[0].set_process_id(1);
  processes[0].set_cmd_line(&cmd_line);
  EXPECT_CALL(platform_,
              GetProcessesWithOpenFiles(FilePath("/home/chronos/user"), _))
      .Times(1)
      .WillRepeatedly(SetArgPointee<1>(processes));

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(FilePath("/dev/loop7"), _))
      .WillOnce(Return(false));

  EXPECT_CALL(platform_, Unmount(_, _, _)).Times(0);
  EXPECT_TRUE(service_.CleanUpStaleMounts(false));
}

TEST_F(ServiceTest, CleanUpStale_OpenLegacy_Ephemeral_Forced) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, but cleanup is forced,
  // all mounts are unmounted, loop device is detached and file is deleted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(homedirs_.shadow_root(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(kLoopDevices));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _)).Times(0);

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(platform_, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(platform_, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[0], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[1], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kLoopDevMounts[0].dst, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(service_.CleanUpStaleMounts(true));
}

TEST_F(ServiceTest, CleanUpStale_EmptyMap_NoOpenFiles_ShadowOnly) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .WillOnce(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kShadowMounts.size());
  EXPECT_CALL(platform_, Unmount(_, true, _))
      .Times(kShadowMounts.size())
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(service_.CleanUpStaleMounts(false));
}

TEST_F(ServiceTest, CleanUpStale_EmptyMap_NoOpenFiles_ShadowOnly_Forced) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted and we attempt
  // to clear the encryption key for fscrypt/ecryptfs mounts.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
    .WillOnce(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
    .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, Unmount(_, true, _))
    .Times(kShadowMounts.size())
    .WillRepeatedly(Return(true));

  // Expect the cleanup to clear user keys.
  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
  EXPECT_CALL(platform_, InvalidateDirCryptoKey(_, _))
    .Times(kShadowMounts.size())
    .WillRepeatedly(Return(true));

  EXPECT_FALSE(service_.CleanUpStaleMounts(true));
}

TEST_F(ServiceTest, CleanUpStale_EmptyMap_OpenLegacy_ShadowOnly) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and some open filehandles to the legacy homedir, all mounts without
  // filehandles are unmounted.

  // Called by CleanUpStaleMounts and each time a directory is excluded.
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .Times(4)
      .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  std::vector<ProcessInformation> processes(1);
  std::vector<std::string> cmd_line(1, "test");
  processes[0].set_process_id(1);
  processes[0].set_cmd_line(&cmd_line);
  // In addition to /home/chronos/user mount point, /home/.shadow/a/Downloads
  // is not considered anymore, as it is under /home/.shadow/a.
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kShadowMounts.size() - 3);
  EXPECT_CALL(platform_,
              GetProcessesWithOpenFiles(FilePath("/home/chronos/user"), _))
      .Times(1)
      .WillRepeatedly(SetArgPointee<1>(processes));
  // Given /home/chronos/user is still used, a is still used, so only
  // b mounts should be removed.
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value,
                       AnyOf(EndsWith("/1"), EndsWith("b/MyFiles/Downloads"))),
              true, _))
      .Times(4)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(FilePath("/daemon-store/server/b"), true, _))
      .WillOnce(Return(true));
  EXPECT_TRUE(service_.CleanUpStaleMounts(false));
}

TEST_F(ServiceTestNotInitialized,
       CleanUpStale_FilledMap_NoOpenFiles_ShadowOnly) {
  // Checks that when we have a bunch of stale shadow mounts, some active
  // mounts, and no open filehandles, all inactive mounts are unmounted.

  // ownership handed off to the Service MountMap
  MockMountFactory mount_factory;
  MockMount* mount = new MockMount();
  EXPECT_CALL(mount_factory, New()).WillOnce(Return(mount));
  service_.set_mount_factory(&mount_factory);
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  ASSERT_TRUE(service_.Initialize());

  EXPECT_CALL(lockbox_, FinalizeBoot());
  EXPECT_CALL(*mount, Init(&platform_, service_.crypto(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount, MountCryptohome(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(*mount, UpdateCurrentUserActivityTimestamp(_, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));

  gint error_code = 0;
  gboolean result = FALSE;
  ASSERT_TRUE(service_.Mount("foo@bar.net", "key", true, false, &error_code,
                             &result, nullptr));
  ASSERT_EQ(TRUE, result);
  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .Times(4)
      .WillRepeatedly(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  // Only 5 look ups: user/1 and root/1 are owned, children of these
  // directories are excluded.
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _)).Times(5);

  EXPECT_CALL(*mount, OwnsMountPoint(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(*mount, OwnsMountPoint(FilePath("/home/user/1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount, OwnsMountPoint(FilePath("/home/root/1")))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_,
              Unmount(Property(&FilePath::value, EndsWith("/0")), true, _))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Unmount(Property(&FilePath::value,
                                          EndsWith("user/MyFiles/Downloads")),
                                 true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Unmount(FilePath("/daemon-store/server/a"), true, _))
      .WillOnce(Return(true));

  std::vector<std::string> fake_token_list;
  fake_token_list.push_back("/home/chronos/user/token");
  fake_token_list.push_back("/home/user/1/token");
  fake_token_list.push_back("/home/root/1/token");
  EXPECT_CALL(chaps_client_, GetTokenList(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(fake_token_list), Return(true)));

  EXPECT_CALL(chaps_client_,
              UnloadToken(_, FilePath("/home/chronos/user/token")))
      .Times(1);

  // Expect that CleanUpStaleMounts() tells us it skipped mounts since 1 is
  // still logged in.
  EXPECT_TRUE(service_.CleanUpStaleMounts(false));
}

class ServiceExTest : public ServiceTest {
 public:
  ServiceExTest() { platform_.GetFake()->SetStandardUsersAndGroups(); }
  ~ServiceExTest() override = default;

  VaultKeyset* GetNiceMockVaultKeyset(const std::string& obfuscated_username,
                                      const std::string& key_label) const {
    std::unique_ptr<VaultKeyset> mvk(new NiceMock<MockVaultKeyset>);
    mvk->mutable_serialized()->mutable_key_data()->set_label(key_label);
    return mvk.release();
  }

 protected:
  void PrepareArguments() {
    id_.reset(new AccountIdentifier);
    auth_.reset(new AuthorizationRequest);
    add_req_.reset(new AddKeyRequest);
    check_req_.reset(new CheckKeyRequest);
    migrate_req_.reset(new MigrateKeyRequest);
    mount_req_.reset(new MountRequest);
    remove_req_.reset(new RemoveKeyRequest);
    list_keys_req_.reset(new ListKeysRequest);
  }

  template <class ProtoBuf>
  brillo::Blob BlobFromProtobuf(const ProtoBuf& pb) {
    std::string serialized;
    CHECK(pb.SerializeToString(&serialized));
    return brillo::BlobFromString(serialized);
  }

  template <class ProtoBuf>
  brillo::SecureBlob SecureBlobFromProtobuf(const ProtoBuf& pb) {
    std::string serialized;
    CHECK(pb.SerializeToString(&serialized));
    return brillo::SecureBlob(serialized);
  }

  std::unique_ptr<AccountIdentifier> id_;
  std::unique_ptr<AuthorizationRequest> auth_;
  std::unique_ptr<AddKeyRequest> add_req_;
  std::unique_ptr<CheckKeyRequest> check_req_;
  std::unique_ptr<MigrateKeyRequest> migrate_req_;
  std::unique_ptr<MountRequest> mount_req_;
  std::unique_ptr<RemoveKeyRequest> remove_req_;
  std::unique_ptr<ListKeysRequest> list_keys_req_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ServiceExTest);
};

TEST_F(ServiceExTest, AddDataRestoreKeyInvalidArgsNoEmail) {
  PrepareArguments();
  service_.DoAddDataRestoreKey(id_.get(), auth_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No email supplied", *error_reply());
}

TEST_F(ServiceExTest, AddDataRestoreKeyInvalidArgsNoSecret) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  service_.DoAddDataRestoreKey(id_.get(), auth_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No key secret supplied", *error_reply());
}

TEST_F(ServiceExTest, AddDataRestoreKeyAccountNotExist) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(false));
  service_.DoAddDataRestoreKey(id_.get(), auth_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND, reply()->error());
}

TEST_F(ServiceExTest, AddDataRestoreKeyAccountExistAddFail) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AddKeyset(_, _, _, _, _))
      .WillRepeatedly(Return(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE));
  service_.DoAddDataRestoreKey(id_.get(), auth_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE, reply()->error());
}

TEST_F(ServiceExTest, AddDataRestoreKeyAccountExistAddSuccess) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AddKeyset(_, _, _, _, _))
      .WillRepeatedly(Return(CRYPTOHOME_ERROR_NOT_SET));
  service_.DoAddDataRestoreKey(id_.get(), auth_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
  // Since the adding is success, the reply should contain raw bytes
  // of data restore key, whose length is 32 bytes.
  EXPECT_TRUE(reply()->HasExtension(AddDataRestoreKeyReply::reply));
  EXPECT_EQ(32, reply()
                    ->GetExtension(AddDataRestoreKeyReply::reply)
                    .data_restore_key()
                    .length());
}

TEST_F(ServiceExTest, MassRemoveKeysInvalidArgsNoEmail) {
  PrepareArguments();
  std::unique_ptr<MassRemoveKeysRequest> mrk_req(new MassRemoveKeysRequest);
  service_.DoMassRemoveKeys(id_.get(), auth_.get(), mrk_req.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No email supplied", *error_reply());
}

TEST_F(ServiceExTest, MassRemoveKeysInvalidArgsNoSecret) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  std::unique_ptr<MassRemoveKeysRequest> mrk_req(new MassRemoveKeysRequest);
  service_.DoMassRemoveKeys(id_.get(), auth_.get(), mrk_req.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No key secret supplied", *error_reply());
}

TEST_F(ServiceExTest, MassRemoveKeysAccountNotExist) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  std::unique_ptr<MassRemoveKeysRequest> mrk_req(new MassRemoveKeysRequest);
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(false));
  service_.DoMassRemoveKeys(id_.get(), auth_.get(), mrk_req.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND, reply()->error());
}

TEST_F(ServiceExTest, MassRemoveKeysAuthFailed) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillRepeatedly(Return(false));
  std::unique_ptr<MassRemoveKeysRequest> mrk_req(new MassRemoveKeysRequest);
  service_.DoMassRemoveKeys(id_.get(), auth_.get(), mrk_req.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED, reply()->error());
}

TEST_F(ServiceExTest, MassRemoveKeysGetLabelsFailed) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, GetVaultKeysetLabels(_, _))
      .WillRepeatedly(Return(false));
  std::unique_ptr<MassRemoveKeysRequest> mrk_req(new MassRemoveKeysRequest);
  service_.DoMassRemoveKeys(id_.get(), auth_.get(), mrk_req.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_KEY_NOT_FOUND, reply()->error());
}

TEST_F(ServiceExTest, MassRemoveKeysForceSuccess) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, GetVaultKeysetLabels(_, _))
      .WillRepeatedly(Return(true));
  std::unique_ptr<MassRemoveKeysRequest> mrk_req(new MassRemoveKeysRequest);
  service_.DoMassRemoveKeys(id_.get(), auth_.get(), mrk_req.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
}

TEST_F(ServiceExTest, MountInvalidArgsNoEmail) {
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoMountEx(std::move(id_), std::move(auth_), std::move(mount_req_),
                     NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No email supplied", *error_reply());
}

TEST_F(ServiceExTest, MountInvalidArgsNoSecret) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  service_.DoMountEx(std::move(id_), std::move(auth_), std::move(mount_req_),
                     NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No key secret supplied", *error_reply());
}

TEST_F(ServiceExTest, MountInvalidArgsEmptySecret) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("");
  service_.DoMountEx(std::move(id_), std::move(auth_), std::move(mount_req_),
                     NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No key secret supplied", *error_reply());
}

TEST_F(ServiceExTest, MountInvalidArgsCreateWithNoKey) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create();
  service_.DoMountEx(std::move(id_), std::move(auth_), std::move(mount_req_),
                     NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("CreateRequest supplied with no keys", *error_reply());
}

TEST_F(ServiceExTest, MountInvalidArgsCreateWithEmptyKey) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create()->add_keys();
  // TODO(wad) Add remaining missing field tests and NULL tests
  service_.DoMountEx(std::move(id_), std::move(auth_), std::move(mount_req_),
                     NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("CreateRequest Keys are not fully specified", *error_reply());
}

TEST_F(ServiceExTest, MountPublicWithExistingMounts) {
  constexpr char kUser[] = "chromeos-user";
  PrepareArguments();
  SetupMount("foo@gmail.com");

  id_->set_account_id(kUser);
  mount_req_->set_public_mount(true);
  EXPECT_CALL(lockbox_, FinalizeBoot());
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  service_.DoMountEx(std::move(id_), std::move(auth_), std::move(mount_req_),
                     NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY, reply()->error());
}

TEST_F(ServiceExTest, MountPublicUsesPublicMountPasskey) {
  constexpr char kUser[] = "chromeos-user";
  PrepareArguments();

  id_->set_account_id(kUser);
  mount_req_->set_public_mount(true);
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(testing::InvokeWithoutArgs([&]() {
    SetupMount(kUser);
    EXPECT_CALL(*mount_, MountCryptohome(_, _, _))
        .WillOnce(testing::Invoke([](const Credentials& credentials,
                                     const Mount::MountArgs& mount_args,
                                     MountError* error) {
          // Tests that the passkey is filled when public_mount is set.
          EXPECT_FALSE(credentials.passkey().empty());
          return true;
        }));
    return true;
  }));
  service_.DoMountEx(std::move(id_), std::move(auth_), std::move(mount_req_),
                     NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoEmail) {
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No email supplied", *error_reply());
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoSecret) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No key secret supplied", *error_reply());
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoNewKeySet) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  add_req_->clear_key();
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No new key supplied", *error_reply());
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoKeyFilled) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No new key supplied", *error_reply());
}

TEST_F(ServiceExTest, AddKeyInvalidArgsNoNewKeyLabel) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  // No label
  add_req_->mutable_key()->set_secret("some secret");
  service_.DoAddKeyEx(id_.get(), auth_.get(), add_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No new key label supplied", *error_reply());
}

TEST_F(ServiceExTest, CheckKeySuccessTest) {
  constexpr char kUser[] = "chromeos-user";
  constexpr char kKey[] = "274146c6e8886a843ddfea373e2dc71b";

  PrepareArguments();
  SetupMount(kUser);

  id_->set_account_id(kUser);
  auth_->mutable_key()->set_secret(kKey);
  CheckKeyRequest req;

  Credentials credentials("another", brillo::SecureBlob(kKey));
  session_->SetCredentials(credentials, 0);

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillOnce(Return(true));
  service_.DoCheckKeyEx(std::move(id_), std::move(auth_), std::move(check_req_),
                        nullptr);

  // Expect an empty reply as success.
  DispatchEvents();
  EXPECT_TRUE(ReplyIsEmpty());
}

TEST_F(ServiceExTest, CheckKeyMountTest) {
  constexpr char kUser[] = "chromeos-user";
  constexpr char kKey[] = "274146c6e8886a843ddfea373e2dc71b";

  PrepareArguments();
  SetupMount(kUser);

  id_->set_account_id(kUser);
  auth_->mutable_key()->set_secret(kKey);

  Credentials credentials(kUser, brillo::SecureBlob(kKey));
  session_->SetCredentials(credentials, 0);

  service_.DoCheckKeyEx(std::make_unique<AccountIdentifier>(*id_),
                        std::make_unique<AuthorizationRequest>(*auth_),
                        std::make_unique<CheckKeyRequest>(*check_req_),
                        nullptr);

  // Expect an empty reply as success.
  DispatchEvents();
  EXPECT_TRUE(ReplyIsEmpty());
  Mock::VerifyAndClearExpectations(mount_.get());

  Credentials credentials2(kUser, brillo::SecureBlob("another"));
  session_->SetCredentials(credentials2, 0);

  // Rinse and repeat but fail.
  ClearReplies();
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillOnce(Return(false));
  service_.DoCheckKeyEx(std::make_unique<AccountIdentifier>(*id_),
                        std::make_unique<AuthorizationRequest>(*auth_),
                        std::make_unique<CheckKeyRequest>(*check_req_),
                        nullptr);

  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED, reply()->error());
}

class ChallengeResponseServiceExTest : public ServiceExTest {
 public:
  static constexpr const char* kUser = "chromeos-user";
  static constexpr const char* kKeyLabel = "key";
  static constexpr const char* kKeyDelegateDBusService = "key-delegate-service";
  static constexpr const char* kSpkiDer = "fake-spki";
  static constexpr ChallengeSignatureAlgorithm kAlgorithm =
      CHALLENGE_RSASSA_PKCS1_V1_5_SHA256;
  static constexpr const char* kPasskey = "passkey";

  // GMock actions that perform reply to ChallengeCredentialsHelper operations:

  struct ReplyToVerifyKey {
    void operator()(const std::string& account_id,
                    const KeyData& key_data,
                    std::unique_ptr<KeyChallengeService> key_challenge_service,
                    ChallengeCredentialsHelper::VerifyKeyCallback callback) {
      std::move(callback).Run(is_key_valid);
    }

    bool is_key_valid = false;
  };

  struct ReplyToDecrypt {
    void operator()(const std::string& account_id,
                    const KeyData& key_data,
                    const SerializedVaultKeyset_SignatureChallengeInfo&
                        keyset_challenge_info,
                    std::unique_ptr<KeyChallengeService> key_challenge_service,
                    ChallengeCredentialsHelper::DecryptCallback callback) {
      std::unique_ptr<Credentials> credentials_to_pass;
      if (credentials)
        credentials_to_pass = std::make_unique<Credentials>(*credentials);
      std::move(callback).Run(std::move(credentials_to_pass));
    }

    base::Optional<Credentials> credentials;
  };

  ChallengeResponseServiceExTest() {
    key_data_.set_label(kKeyLabel);
    key_data_.set_type(KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
    ChallengePublicKeyInfo* const key_public_info =
        key_data_.add_challenge_response_key();
    key_public_info->set_public_key_spki_der(kSpkiDer);
    key_public_info->add_signature_algorithm(kAlgorithm);

    PrepareArguments();
    id_->set_account_id(kUser);
    *auth_->mutable_key()->mutable_data() = key_data_;
    auth_->mutable_key_delegate()->set_dbus_service_name(
        kKeyDelegateDBusService);

    ON_CALL(key_challenge_service_factory_,
            New(/*bus=*/_, kKeyDelegateDBusService))
        .WillByDefault(InvokeWithoutArgs(
            []() { return std::make_unique<MockKeyChallengeService>(); }));
  }

  void SetUpActiveUserSession() {
    EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(homedirs_, GetVaultKeyset(_, kKeyLabel))
        .WillRepeatedly(Invoke(this, &ServiceExTest::GetNiceMockVaultKeyset));

    SetupMount(kUser);

    Credentials credentials(kUser, brillo::SecureBlob(kPasskey));
    credentials.set_key_data(key_data_);
    session_->SetCredentials(credentials, 0);
  }

 protected:
  KeyData key_data_;
};

// Tests the CheckKeyEx lightweight check scenario for challenge-response
// credentials, where the credentials are verified without going through full
// decryption.
TEST_F(ChallengeResponseServiceExTest, LightweightCheckKey) {
  SetUpActiveUserSession();

  // Simulate a successful key verification.
  EXPECT_CALL(challenge_credentials_helper_,
              VerifyKey(kUser, ProtobufEquals(key_data_), _, _))
      .WillOnce(ReplyToVerifyKey{/*is_key_valid=*/true});

  service_.DoCheckKeyEx(std::make_unique<AccountIdentifier>(*id_),
                        std::make_unique<AuthorizationRequest>(*auth_),
                        std::make_unique<CheckKeyRequest>(*check_req_),
                        nullptr);

  // Expect an empty reply as success.
  DispatchEvents();
  EXPECT_TRUE(ReplyIsEmpty());
}

// Tests the CheckKeyEx full check scenario for challenge-response credentials,
// with falling back from the failed lightweight check.
TEST_F(ChallengeResponseServiceExTest, FallbackLightweightCheckKey) {
  SetUpActiveUserSession();

  // Simulate a failure in the lightweight check and a successful decryption.
  EXPECT_CALL(challenge_credentials_helper_,
              VerifyKey(kUser, ProtobufEquals(key_data_), _, _))
      .WillOnce(ReplyToVerifyKey{/*is_key_valid=*/false});
  EXPECT_CALL(challenge_credentials_helper_,
              Decrypt(kUser, ProtobufEquals(key_data_), _, _, _))
      .WillOnce(ReplyToDecrypt{Credentials(kUser, SecureBlob(kPasskey))});

  service_.DoCheckKeyEx(std::make_unique<AccountIdentifier>(*id_),
                        std::make_unique<AuthorizationRequest>(*auth_),
                        std::make_unique<CheckKeyRequest>(*check_req_),
                        nullptr);

  // Expect an empty reply as success.
  DispatchEvents();
  EXPECT_TRUE(ReplyIsEmpty());
}

MATCHER_P(CredentialsEqual, credentials, "") {
  const Credentials& expected_creds = credentials;
  return expected_creds.username() == arg.username() &&
         expected_creds.passkey() == arg.passkey();
}

TEST_F(ServiceExTest, MigrateKeyTest) {
  constexpr char kUser[] = "chromeos-user";
  constexpr char kOldKey[] = "274146c6e8886a843ddfea373e2dc71b";
  constexpr char kNewKey[] = "274146c6e8886a843ddfea373e2dc71c";

  PrepareArguments();
  SetupMount(kUser);

  id_->set_account_id(kUser);
  auth_->mutable_key()->set_secret(kOldKey);
  migrate_req_->set_secret(kNewKey);

  Credentials credentials(kUser, SecureBlob(kNewKey));

  EXPECT_CALL(homedirs_, Migrate(CredentialsEqual(testing::ByRef(credentials)),
                                 SecureBlob(kOldKey), _))
      .WillRepeatedly(Return(true));
  service_.DoMigrateKeyEx(id_.get(), auth_.get(), migrate_req_.get(), nullptr);

  // Expect an empty reply as success.
  DispatchEvents();
  EXPECT_TRUE(ReplyIsEmpty());
  Mock::VerifyAndClearExpectations(mount_.get());
}

TEST_F(ServiceExTest, CheckKeyHomedirsTest) {
  constexpr char kUser[] = "chromeos-user";
  constexpr char kKey[] = "274146c6e8886a843ddfea373e2dc71b";

  PrepareArguments();
  SetupMount(kUser);

  id_->set_account_id(kUser);
  auth_->mutable_key()->set_secret(kKey);

  Credentials credentials("another", brillo::SecureBlob(kKey));
  session_->SetCredentials(credentials, 0);

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillOnce(Return(true));
  service_.DoCheckKeyEx(std::make_unique<AccountIdentifier>(*id_),
                        std::make_unique<AuthorizationRequest>(*auth_),
                        std::make_unique<CheckKeyRequest>(*check_req_),
                        nullptr);

  // Expect an empty reply as success.
  DispatchEvents();
  EXPECT_TRUE(ReplyIsEmpty());
  Mock::VerifyAndClearExpectations(&homedirs_);

  // Ensure failure
  ClearReplies();
  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, AreCredentialsValid(_)).WillOnce(Return(false));
  service_.DoCheckKeyEx(std::make_unique<AccountIdentifier>(*id_),
                        std::make_unique<AuthorizationRequest>(*auth_),
                        std::make_unique<CheckKeyRequest>(*check_req_),
                        nullptr);

  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED, reply()->error());
}

TEST_F(ServiceExTest, CheckKeyInvalidArgsNoEmail) {
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoCheckKeyEx(std::move(id_), std::move(auth_), std::move(check_req_),
                        NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No email supplied", *error_reply());
}

TEST_F(ServiceExTest, CheckKeyInvalidArgsNoSecret) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  service_.DoCheckKeyEx(std::move(id_), std::move(auth_), std::move(check_req_),
                        NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No key secret supplied", *error_reply());
}

TEST_F(ServiceExTest, CheckKeyInvalidArgsEmptySecret) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("");
  service_.DoCheckKeyEx(std::move(id_), std::move(auth_), std::move(check_req_),
                        NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No key secret supplied", *error_reply());
}

TEST_F(ServiceExTest, RemoveKeyInvalidArgsNoEmail) {
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoMountEx is called directly.
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No email supplied", *error_reply());
}

TEST_F(ServiceExTest, RemoveKeyInvalidArgsNoSecret) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No key secret supplied", *error_reply());
}

TEST_F(ServiceExTest, RemoveKeyInvalidArgsEmptySecret) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("");
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No key secret supplied", *error_reply());
}

TEST_F(ServiceExTest, RemoveKeyInvalidArgsEmptyRemoveLabel) {
  PrepareArguments();
  id_->set_account_id("foo@gmail.com");
  auth_->mutable_key()->set_secret("some secret");
  remove_req_->mutable_key()->mutable_data();
  service_.DoRemoveKeyEx(id_.get(), auth_.get(), remove_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No label provided for target key", *error_reply());
}

TEST_F(ServiceExTest, BootLockboxSignSuccess) {
  SecureBlob test_signature("test");
  EXPECT_CALL(lockbox_, Sign(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(test_signature), Return(true)));

  SignBootLockboxRequest request;
  request.set_data("test_data");
  service_.DoSignBootLockbox(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
  EXPECT_TRUE(reply()->HasExtension(SignBootLockboxReply::reply));
  EXPECT_EQ("test",
            reply()->GetExtension(SignBootLockboxReply::reply).signature());
}

TEST_F(ServiceExTest, BootLockboxSignBadArgs) {
  // Try with bad proto data.
  service_.DoSignBootLockbox(brillo::BlobFromString("not_a_protobuf"), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
  // Try with |data| not set.
  ClearReplies();
  SignBootLockboxRequest request;
  service_.DoSignBootLockbox(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
}

TEST_F(ServiceExTest, BootLockboxSignError) {
  EXPECT_CALL(lockbox_, Sign(_, _)).WillRepeatedly(Return(false));

  SignBootLockboxRequest request;
  request.set_data("test_data");
  service_.DoSignBootLockbox(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN, reply()->error());
  EXPECT_FALSE(reply()->HasExtension(SignBootLockboxReply::reply));
}

TEST_F(ServiceExTest, BootLockboxVerifySuccess) {
  EXPECT_CALL(lockbox_, Verify(_, _)).WillRepeatedly(Return(true));

  VerifyBootLockboxRequest request;
  request.set_data("test_data");
  request.set_signature("test_signature");
  service_.DoVerifyBootLockbox(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
  EXPECT_FALSE(reply()->HasExtension(SignBootLockboxReply::reply));
}

TEST_F(ServiceExTest, BootLockboxVerifyBadArgs) {
  // Try with bad proto data.
  service_.DoVerifyBootLockbox(brillo::BlobFromString("not_a_protobuf"), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
  // Try with |signature| not set.
  ClearReplies();
  VerifyBootLockboxRequest request;
  request.set_data("test_data");
  service_.DoVerifyBootLockbox(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
  // Try with |data| not set.
  ClearReplies();
  VerifyBootLockboxRequest request2;
  request2.set_signature("test_data");
  service_.DoVerifyBootLockbox(BlobFromProtobuf(request2), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
}

TEST_F(ServiceExTest, BootLockboxVerifyError) {
  EXPECT_CALL(lockbox_, Verify(_, _)).WillRepeatedly(Return(false));

  VerifyBootLockboxRequest request;
  request.set_data("test_data");
  request.set_signature("test_signature");
  service_.DoVerifyBootLockbox(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID, reply()->error());
}

TEST_F(ServiceExTest, BootLockboxFinalizeSuccess) {
  EXPECT_CALL(lockbox_, FinalizeBoot()).WillRepeatedly(Return(true));

  FinalizeBootLockboxRequest request;
  service_.DoFinalizeBootLockbox(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
  EXPECT_FALSE(reply()->HasExtension(SignBootLockboxReply::reply));
}

TEST_F(ServiceExTest, BootLockboxFinalizeBadArgs) {
  // Try with bad proto data.
  service_.DoFinalizeBootLockbox(brillo::BlobFromString("not_a_protobuf"),
                                 NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
}

TEST_F(ServiceExTest, BootLockboxFinalizeError) {
  EXPECT_CALL(lockbox_, FinalizeBoot()).WillRepeatedly(Return(false));

  FinalizeBootLockboxRequest request;
  service_.DoFinalizeBootLockbox(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_TPM_COMM_ERROR, reply()->error());
}

TEST_F(ServiceExTest, GetBootAttributeSuccess) {
  EXPECT_CALL(boot_attributes_, Get(_, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>("1234"), Return(true)));

  GetBootAttributeRequest request;
  request.set_name("test");
  service_.DoGetBootAttribute(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
  EXPECT_TRUE(reply()->HasExtension(GetBootAttributeReply::reply));
  EXPECT_EQ("1234",
            reply()->GetExtension(GetBootAttributeReply::reply).value());
}

TEST_F(ServiceExTest, GetBootAttributeBadArgs) {
  // Try with bad proto data.
  service_.DoGetBootAttribute(brillo::BlobFromString("not_a_protobuf"), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
}

TEST_F(ServiceExTest, GetBootAttributeError) {
  EXPECT_CALL(boot_attributes_, Get(_, _)).WillRepeatedly(Return(false));

  GetBootAttributeRequest request;
  request.set_name("test");
  service_.DoGetBootAttribute(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND, reply()->error());
}

TEST_F(ServiceExTest, SetBootAttributeSuccess) {
  SetBootAttributeRequest request;
  request.set_name("test");
  request.set_value("1234");
  service_.DoSetBootAttribute(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
}

TEST_F(ServiceExTest, SetBootAttributeBadArgs) {
  // Try with bad proto data.
  service_.DoSetBootAttribute(brillo::BlobFromString("not_a_protobuf"), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
}

TEST_F(ServiceExTest, FlushAndSignBootAttributesSuccess) {
  EXPECT_CALL(boot_attributes_, FlushAndSign()).WillRepeatedly(Return(true));

  FlushAndSignBootAttributesRequest request;
  service_.DoFlushAndSignBootAttributes(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
}

TEST_F(ServiceExTest, FlushAndSignBootAttributesBadArgs) {
  // Try with bad proto data.
  service_.DoFlushAndSignBootAttributes(
      brillo::BlobFromString("not_a_protobuf"), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
}

TEST_F(ServiceExTest, FlushAndSignBootAttributesError) {
  EXPECT_CALL(boot_attributes_, FlushAndSign()).WillRepeatedly(Return(false));

  FlushAndSignBootAttributesRequest request;
  service_.DoFlushAndSignBootAttributes(BlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN, reply()->error());
}

TEST_F(ServiceExTest, GetLoginStatusSuccess) {
  EXPECT_CALL(homedirs_, GetPlainOwner(_)).WillOnce(Return(true));
  EXPECT_CALL(lockbox_, IsFinalized()).WillOnce(Return(false));

  GetLoginStatusRequest request;
  service_.DoGetLoginStatus(SecureBlobFromProtobuf(request), NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
  EXPECT_TRUE(reply()->HasExtension(GetLoginStatusReply::reply));
  EXPECT_TRUE(
      reply()->GetExtension(GetLoginStatusReply::reply).owner_user_exists());
  EXPECT_FALSE(reply()
                   ->GetExtension(GetLoginStatusReply::reply)
                   .boot_lockbox_finalized());
}

TEST_F(ServiceExTest, GetLoginStatusBadArgs) {
  // Try with bad proto data.
  service_.DoVerifyBootLockbox(brillo::BlobFromString("not_a_protobuf"), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_NE("", *error_reply());
}

TEST_F(ServiceExTest, GetKeyDataExNoMatch) {
  PrepareArguments();

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));

  id_->set_account_id("unittest@example.com");
  GetKeyDataRequest req;
  req.mutable_key()->mutable_data()->set_label("non-existent label");
  // Ensure there are no matches.
  EXPECT_CALL(homedirs_, GetVaultKeyset(_, _))
      .Times(1)
      .WillRepeatedly(Return(static_cast<VaultKeyset*>(NULL)));
  service_.DoGetKeyDataEx(id_.get(), auth_.get(), &req, NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
  GetKeyDataReply sub_reply = reply()->GetExtension(GetKeyDataReply::reply);
  EXPECT_EQ(0, sub_reply.key_data_size());
}

TEST_F(ServiceExTest, GetKeyDataExOneMatch) {
  // Request the single key by label.
  PrepareArguments();

  static const char* kExpectedLabel = "find-me";
  GetKeyDataRequest req;
  req.mutable_key()->mutable_data()->set_label(kExpectedLabel);

  EXPECT_CALL(homedirs_, Exists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(homedirs_, GetVaultKeyset(_, _))
      .Times(1)
      .WillRepeatedly(Invoke(this, &ServiceExTest::GetNiceMockVaultKeyset));

  id_->set_account_id("unittest@example.com");
  service_.DoGetKeyDataEx(id_.get(), auth_.get(), &req, NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());

  GetKeyDataReply sub_reply = reply()->GetExtension(GetKeyDataReply::reply);
  ASSERT_EQ(1, sub_reply.key_data_size());
  EXPECT_EQ(std::string(kExpectedLabel), sub_reply.key_data(0).label());
}

TEST_F(ServiceExTest, GetKeyDataInvalidArgsNoEmail) {
  PrepareArguments();
  GetKeyDataRequest req;
  service_.DoGetKeyDataEx(id_.get(), auth_.get(), &req, NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No email supplied", *error_reply());
}

TEST_F(ServiceExTest, ListKeysInvalidArgsNoEmail) {
  PrepareArguments();
  // Run will never be called because we aren't running the event loop.
  // For the same reason, DoListKeysEx is called directly.
  service_.DoListKeysEx(id_.get(), auth_.get(), list_keys_req_.get(), NULL);
  DispatchEvents();
  ASSERT_TRUE(error_reply());
  EXPECT_EQ("No email supplied", *error_reply());
}

TEST_F(ServiceExTest, GetFirmwareManagementParametersSuccess) {
  brillo::Blob hash = brillo::BlobFromString("its_a_hash");

  EXPECT_CALL(fwmp_, Load()).WillOnce(Return(true));
  EXPECT_CALL(fwmp_, GetFlags(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(0x1234), Return(true)));
  EXPECT_CALL(fwmp_, GetDeveloperKeyHash(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(hash), Return(true)));

  GetFirmwareManagementParametersRequest request;
  service_.DoGetFirmwareManagementParameters(SecureBlobFromProtobuf(request),
                                             NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
  EXPECT_TRUE(
      reply()->HasExtension(GetFirmwareManagementParametersReply::reply));
  EXPECT_EQ(reply()
                ->GetExtension(GetFirmwareManagementParametersReply::reply)
                .flags(),
            0x1234);
  EXPECT_EQ(reply()
                ->GetExtension(GetFirmwareManagementParametersReply::reply)
                .developer_key_hash(),
            brillo::BlobToString(hash));
}

TEST_F(ServiceExTest, GetFirmwareManagementParametersError) {
  EXPECT_CALL(fwmp_, Load()).WillRepeatedly(Return(false));

  GetFirmwareManagementParametersRequest request;
  service_.DoGetFirmwareManagementParameters(SecureBlobFromProtobuf(request),
                                             NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID,
            reply()->error());
}

TEST_F(ServiceExTest, SetFirmwareManagementParametersSuccess) {
  brillo::Blob hash = brillo::BlobFromString("its_a_hash");
  brillo::Blob out_hash;

  EXPECT_CALL(fwmp_, Create()).WillOnce(Return(true));
  EXPECT_CALL(fwmp_, Store(0x1234, _))
      .WillOnce(DoAll(SaveArgPointee<1>(&out_hash), Return(true)));

  SetFirmwareManagementParametersRequest request;
  request.set_flags(0x1234);
  request.set_developer_key_hash(brillo::BlobToString(hash));
  service_.DoSetFirmwareManagementParameters(SecureBlobFromProtobuf(request),
                                             NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
  EXPECT_EQ(hash, out_hash);
}

TEST_F(ServiceExTest, SetFirmwareManagementParametersNoHash) {
  brillo::Blob hash(0);

  EXPECT_CALL(fwmp_, Create()).WillOnce(Return(true));
  EXPECT_CALL(fwmp_, Store(0x1234, NULL)).WillOnce(Return(true));

  SetFirmwareManagementParametersRequest request;
  request.set_flags(0x1234);
  service_.DoSetFirmwareManagementParameters(SecureBlobFromProtobuf(request),
                                             NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
}

TEST_F(ServiceExTest, SetFirmwareManagementParametersCreateError) {
  brillo::Blob hash = brillo::BlobFromString("its_a_hash");

  EXPECT_CALL(fwmp_, Create()).WillOnce(Return(false));

  SetFirmwareManagementParametersRequest request;
  request.set_flags(0x1234);
  request.set_developer_key_hash(brillo::BlobToString(hash));
  service_.DoSetFirmwareManagementParameters(SecureBlobFromProtobuf(request),
                                             NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE,
            reply()->error());
}

TEST_F(ServiceExTest, SetFirmwareManagementParametersStoreError) {
  brillo::Blob hash = brillo::BlobFromString("its_a_hash");

  EXPECT_CALL(fwmp_, Create()).WillOnce(Return(true));
  EXPECT_CALL(fwmp_, Store(_, _)).WillOnce(Return(false));

  SetFirmwareManagementParametersRequest request;
  request.set_flags(0x1234);
  request.set_developer_key_hash(brillo::BlobToString(hash));
  service_.DoSetFirmwareManagementParameters(SecureBlobFromProtobuf(request),
                                             NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE,
            reply()->error());
}

TEST_F(ServiceExTest, RemoveFirmwareManagementParametersSuccess) {
  EXPECT_CALL(fwmp_, Destroy()).WillOnce(Return(true));

  RemoveFirmwareManagementParametersRequest request;
  service_.DoRemoveFirmwareManagementParameters(SecureBlobFromProtobuf(request),
                                                NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_FALSE(reply()->has_error());
}

TEST_F(ServiceExTest, RemoveFirmwareManagementParametersError) {
  EXPECT_CALL(fwmp_, Destroy()).WillOnce(Return(false));

  RemoveFirmwareManagementParametersRequest request;
  service_.DoRemoveFirmwareManagementParameters(SecureBlobFromProtobuf(request),
                                                NULL);
  DispatchEvents();
  ASSERT_TRUE(reply());
  EXPECT_TRUE(reply()->has_error());
  EXPECT_EQ(CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE,
            reply()->error());
}

void ImmediatelySignalOwnership(TpmInit::OwnershipCallback callback) {
  callback.Run(true, false);
}

TEST_F(ServiceTestNotInitialized, CheckTpmInitRace) {
  // Emulate quick tpm initialization by calling the ownership callback from
  // TpmInit::Init(). In reality, it is called from the thread created by
  // TpmInit::AsyncTakeOwnership(), but since it's guarded by a command line
  // switch, call it from Init() instead. It should be safe to call the
  // ownership callback from the main thread.
  EXPECT_CALL(tpm_init_, Init(_)).WillOnce(Invoke(ImmediatelySignalOwnership));
  service_.set_tpm(&tpm_);
  service_.set_tpm_init(&tpm_init_);
  service_.set_initialize_tpm(true);
  service_.Initialize();
}

TEST_F(ServiceTestNotInitialized, CheckTpmGetPassword) {
  NiceMock<MockTpmInit> tpm_init;
  service_.set_tpm_init(&tpm_init);

  std::string pwd1_ascii_str("abcdefgh", 8);
  SecureBlob pwd1_ascii_blob(pwd1_ascii_str);
  std::string pwd2_non_ascii_str("ab\xB2\xFF\x00\xA0gh", 8);
  SecureBlob pwd2_non_ascii_blob(pwd2_non_ascii_str);
  std::wstring pwd2_non_ascii_wstr(
      pwd2_non_ascii_blob.data(),
      pwd2_non_ascii_blob.data() + pwd2_non_ascii_blob.size());
  std::string pwd2_non_ascii_str_utf8(base::SysWideToUTF8(pwd2_non_ascii_wstr));
  EXPECT_CALL(tpm_init, GetTpmPassword(_))
      .WillOnce(Return(false))
      .WillOnce(DoAll(SetArgPointee<0>(pwd1_ascii_blob), Return(true)))
      .WillOnce(DoAll(SetArgPointee<0>(pwd2_non_ascii_blob), Return(true)));

  gchar* res_pwd;
  GError* res_err;
  // Return success and NULL if getting tpm password failed.
  EXPECT_TRUE(service_.TpmGetPassword(&res_pwd, &res_err));
  EXPECT_EQ(NULL, res_pwd);
  g_free(res_pwd);
  // Check that the ASCII password is returned as is.
  EXPECT_TRUE(service_.TpmGetPassword(&res_pwd, &res_err));
  EXPECT_EQ(0, memcmp(pwd1_ascii_str.data(), res_pwd, pwd1_ascii_str.size()));
  g_free(res_pwd);
  // Check that non-ASCII password is converted to UTF-8.
  EXPECT_TRUE(service_.TpmGetPassword(&res_pwd, &res_err));
  EXPECT_EQ(0, memcmp(pwd2_non_ascii_str_utf8.data(), res_pwd,
                      pwd2_non_ascii_str_utf8.size()));
  g_free(res_pwd);
}

TEST_F(ServiceTestNotInitialized, InitializeArcDiskQuota) {
  EXPECT_CALL(arc_disk_quota_, Initialize()).Times(1);
  EXPECT_TRUE(service_.Initialize());
}

TEST_F(ServiceTestNotInitialized, IsQuotaSupported) {
  EXPECT_CALL(arc_disk_quota_, IsQuotaSupported()).WillOnce(Return(true));

  GError* res_err;
  gboolean quota_supported;
  EXPECT_TRUE(service_.IsQuotaSupported(&quota_supported, &res_err));
  EXPECT_EQ(TRUE, quota_supported);
}

TEST_F(ServiceTestNotInitialized, GetCurrentSpaceForUid) {
  EXPECT_CALL(arc_disk_quota_, GetCurrentSpaceForUid(10)).WillOnce(Return(20));
  GError* res_err;
  gint64 cur_space;
  EXPECT_TRUE(service_.GetCurrentSpaceForUid(10, &cur_space, &res_err));
  EXPECT_EQ(20, cur_space);
}

TEST_F(ServiceTestNotInitialized, GetCurrentSpaceForGid) {
  EXPECT_CALL(arc_disk_quota_, GetCurrentSpaceForGid(10)).WillOnce(Return(20));
  GError* res_err;
  gint64 cur_space;
  EXPECT_TRUE(service_.GetCurrentSpaceForGid(10, &cur_space, &res_err));
  EXPECT_EQ(20, cur_space);
}

TEST_F(ServiceTest, PostTaskToEventLoop) {
  // In this test, we take the IsQuotaSupported() function to test
  // if PostTaskToEventLoop() actually causes the posted OnceClosure
  // to run.
  EXPECT_CALL(arc_disk_quota_, IsQuotaSupported()).WillOnce(Return(true));

  service_.PostTaskToEventLoop(base::BindOnce(
      [](Service* service) {
        GError* res_err;
        gboolean quota_supported;
        EXPECT_TRUE(service->IsQuotaSupported(&quota_supported, &res_err));
        EXPECT_EQ(TRUE, quota_supported);
      },
      base::Unretained(&service_)));

  DispatchEvents();

  PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(20));
}

TEST_F(ServiceTest, InstallAttributesGetExistingValue) {
  const std::string value_str = "value";
  const brillo::Blob value_blob = brillo::BlobFromString(value_str);
  // Not const because InstallAttributesGet takes gchar*.
  char attr_name[] = "attr";

  EXPECT_CALL(attrs_, Get(StrEq(attr_name), _))
      .WillOnce(DoAll(SetArgPointee<1>(value_blob), Return(true)));

  GArray* value_result = nullptr;
  gboolean successful_result = false;
  GError* error_result = nullptr;
  EXPECT_TRUE(service_.InstallAttributesGet(attr_name, &value_result,
                                            &successful_result, &error_result));

  EXPECT_TRUE(successful_result);
  ASSERT_TRUE(value_result);
  ASSERT_EQ(value_str.size(), value_result->len);
  for (size_t i = 0; i < value_str.size(); ++i) {
    EXPECT_EQ(value_str[i], g_array_index(value_result, uint8_t, i));
  }
  g_array_free(value_result, true);
}

TEST_F(ServiceTest, InstallAttributesGetMissingValue) {
  // Not const because InstallAttributesGet takes gchar*.
  char attr_name[] = "attr";

  EXPECT_CALL(attrs_, Get(StrEq(attr_name), _)).WillOnce(Return(false));

  GArray* value_result = nullptr;
  gboolean successful_result = true;
  GError* error_result = nullptr;
  EXPECT_TRUE(service_.InstallAttributesGet(attr_name, &value_result,
                                            &successful_result, &error_result));
  EXPECT_TRUE(value_result);
}

TEST_F(ServiceTest, InstallAttributesSetSuccess) {
  const std::string value_str = "value";
  const brillo::Blob value_blob = brillo::BlobFromString(value_str);
  // Not const because InstallAttributesGet takes gchar*.
  char attr_name[] = "attr";

  EXPECT_CALL(attrs_, Set(StrEq(attr_name), value_blob)).WillOnce(Return(true));

  GArray* value_array = g_array_new(false, false, sizeof(value_blob.front()));
  ASSERT_TRUE(value_array);
  g_array_append_vals(value_array, value_blob.data(), value_blob.size());

  gboolean successful_result = false;
  GError* error_result = nullptr;
  EXPECT_TRUE(service_.InstallAttributesSet(attr_name, value_array,
                                            &successful_result, &error_result));
  g_array_free(value_array, true);

  EXPECT_TRUE(successful_result);
}

TEST_F(ServiceTest, InstallAttributesSetFailure) {
  const std::string value_str = "value";
  const brillo::Blob value_blob = brillo::BlobFromString(value_str);
  // Not const because InstallAttributesGet takes gchar*.
  char attr_name[] = "attr";

  EXPECT_CALL(attrs_, Set(StrEq(attr_name), value_blob))
      .WillOnce(Return(false));

  GArray* value_array = g_array_new(false, false, sizeof(value_blob.front()));
  ASSERT_TRUE(value_array);
  g_array_append_vals(value_array, value_blob.data(), value_blob.size());

  gboolean successful_result = false;
  GError* error_result = nullptr;
  EXPECT_TRUE(service_.InstallAttributesSet(attr_name, value_array,
                                            &successful_result, &error_result));
  g_array_free(value_array, true);

  EXPECT_FALSE(successful_result);
}

TEST_F(ServiceTest, InstallAttributesFinalizeSuccess) {
  EXPECT_CALL(attrs_, Finalize()).WillOnce(Return(true));

  gboolean finalized_result = false;
  GError* error_result = nullptr;
  EXPECT_TRUE(
      service_.InstallAttributesFinalize(&finalized_result, &error_result));
  EXPECT_TRUE(finalized_result);

  // TODO(https://crbug.com/1009096): Also test that if the device is enterprise
  // owned according to install attributes, this got transferred to homedirs_
  // and mount_ - see Service::DetectEnterpriseOwnership.
}

TEST_F(ServiceTest, InstallAttributesFinalizeFailure) {
  EXPECT_CALL(attrs_, Finalize()).WillOnce(Return(false));

  gboolean finalized_result = false;
  GError* error_result = nullptr;
  EXPECT_TRUE(
      service_.InstallAttributesFinalize(&finalized_result, &error_result));
  EXPECT_FALSE(finalized_result);
}

TEST_F(ServiceTest, InstallAttributesCount) {
  EXPECT_CALL(attrs_, Count()).WillOnce(Return(3));

  gint count_result = 0;
  GError* error_result = nullptr;
  EXPECT_TRUE(service_.InstallAttributesCount(&count_result, &error_result));
  EXPECT_EQ(3, count_result);
}

TEST_F(ServiceTest, InstallAttributesIsSecureTrue) {
  EXPECT_CALL(attrs_, is_secure()).WillOnce(Return(true));

  gboolean is_secure_result = false;
  GError* error_result = nullptr;
  EXPECT_TRUE(
      service_.InstallAttributesIsSecure(&is_secure_result, &error_result));
  EXPECT_TRUE(is_secure_result);
}

TEST_F(ServiceTest, InstallAttributesIsSecureFalse) {
  EXPECT_CALL(attrs_, is_secure()).WillOnce(Return(false));

  gboolean is_secure_result = true;
  GError* error_result = nullptr;
  EXPECT_TRUE(
      service_.InstallAttributesIsSecure(&is_secure_result, &error_result));
  EXPECT_FALSE(is_secure_result);
}

TEST_F(ServiceTest, InstallAttributesStatusQueries) {
  EXPECT_CALL(attrs_, status())
      .WillRepeatedly(Return(InstallAttributes::Status::kUnknown));
  EXPECT_FALSE(GetInstallAttributesIsReady(&service_));
  EXPECT_FALSE(GetInstallAttributesIsInvalid(&service_));
  EXPECT_FALSE(GetInstallAttributesIsFirstInstall(&service_));

  Mock::VerifyAndClearExpectations(&homedirs_);
  EXPECT_CALL(attrs_, status())
      .WillRepeatedly(Return(InstallAttributes::Status::kTpmNotOwned));
  EXPECT_FALSE(GetInstallAttributesIsReady(&service_));
  EXPECT_FALSE(GetInstallAttributesIsInvalid(&service_));
  EXPECT_FALSE(GetInstallAttributesIsFirstInstall(&service_));

  Mock::VerifyAndClearExpectations(&homedirs_);
  EXPECT_CALL(attrs_, status())
      .WillRepeatedly(Return(InstallAttributes::Status::kFirstInstall));
  EXPECT_TRUE(GetInstallAttributesIsReady(&service_));
  EXPECT_FALSE(GetInstallAttributesIsInvalid(&service_));
  EXPECT_TRUE(GetInstallAttributesIsFirstInstall(&service_));

  Mock::VerifyAndClearExpectations(&homedirs_);
  EXPECT_CALL(attrs_, status())
      .WillRepeatedly(Return(InstallAttributes::Status::kValid));
  EXPECT_TRUE(GetInstallAttributesIsReady(&service_));
  EXPECT_FALSE(GetInstallAttributesIsInvalid(&service_));
  EXPECT_FALSE(GetInstallAttributesIsFirstInstall(&service_));

  Mock::VerifyAndClearExpectations(&homedirs_);
  EXPECT_CALL(attrs_, status())
      .WillRepeatedly(Return(InstallAttributes::Status::kInvalid));
  EXPECT_TRUE(GetInstallAttributesIsReady(&service_));
  EXPECT_TRUE(GetInstallAttributesIsInvalid(&service_));
  EXPECT_FALSE(GetInstallAttributesIsFirstInstall(&service_));
}

TEST_F(ServiceTestNotInitialized, OwnershipCallbackRepeated) {
  service_.set_use_tpm(true);
  service_.set_tpm(&tpm_);
  service_.set_tpm_init(&tpm_init_);
  service_.set_initialize_tpm(true);

  service_.Initialize();

  SetupMount("foo@gmail.com");

  // Called by OwnershipCallback().
  EXPECT_CALL(tpm_, HandleOwnershipTakenEvent).WillOnce(Return());
  // Called by ResetAllTPMContext().
  mount_->set_crypto(&crypto_);
  EXPECT_CALL(crypto_, EnsureTpm(true)).WillOnce(Return(CryptoError::CE_NONE));
  // Called by InitializeInstallAttributes()
  EXPECT_CALL(attrs_, Init(_)).WillOnce(Return(true));

  // Call OwnershipCallback twice and see if any of the above gets called more
  // than once.
  service_.OwnershipCallback(true, true);
  service_.OwnershipCallback(true, true);
}

}  // namespace cryptohome
