// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/session_manager_impl.h"

#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ref_counted.h>
#include <base/strings/string_util.h>
#include <brillo/cryptohome.h>
#include <chromeos/dbus/service_constants.h>
#include <crypto/scoped_nss_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "login_manager/dbus_error_types.h"
#include "login_manager/device_local_account_policy_service.h"
#include "login_manager/fake_container_manager.h"
#include "login_manager/fake_crossystem.h"
#include "login_manager/file_checker.h"
#include "login_manager/matchers.h"
#include "login_manager/mock_dbus_signal_emitter.h"
#include "login_manager/mock_device_policy_service.h"
#include "login_manager/mock_file_checker.h"
#include "login_manager/mock_key_generator.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_nss_util.h"
#include "login_manager/mock_policy_key.h"
#include "login_manager/mock_policy_service.h"
#include "login_manager/mock_process_manager_service.h"
#include "login_manager/mock_system_utils.h"
#include "login_manager/mock_user_policy_service_factory.h"
#include "login_manager/mock_vpd_process.h"
#include "login_manager/server_backed_state_key_generator.h"
#include "login_manager/stub_upstart_signal_emitter.h"

using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Mock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgumentPointee;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::_;

using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SetSystemSalt;
using brillo::cryptohome::home::kGuestUserName;

using enterprise_management::ChromeDeviceSettingsProto;

using std::map;
using std::string;
using std::vector;

namespace login_manager {

namespace {

constexpr pid_t kAndroidPid = 10;

enum class DataDirType {
  DATA_DIR_AVAILABLE = 0,
  DATA_DIR_MISSING = 1,
};

enum class OldDataDirType {
  OLD_DATA_DIR_NOT_EMPTY = 0,
  OLD_DATA_DIR_EMPTY = 1,
  OLD_DATA_FILE_EXISTS = 2,
};

}  // anonymous namespace

class SessionManagerImplTest : public ::testing::Test {
 public:
  SessionManagerImplTest()
      : device_policy_service_(new MockDevicePolicyService),
        state_key_generator_(&utils_, &metrics_),
        android_container_(kAndroidPid),
        impl_(scoped_ptr<UpstartSignalEmitter>(new StubUpstartSignalEmitter(
                  &upstart_signal_emitter_delegate_)),
              &dbus_emitter_,
              base::Bind(&SessionManagerImplTest::FakeLockScreen,
                         base::Unretained(this)),
              base::Bind(&SessionManagerImplTest::FakeRestartDevice,
                         base::Unretained(this)),
              base::Bind(&SessionManagerImplTest::FakeStartArcInstance,
                         base::Unretained(this)),
              &key_gen_,
              &state_key_generator_,
              &manager_,
              &metrics_,
              &nss_,
              &utils_,
              &crossystem_,
              &vpd_process_,
              &owner_key_,
              &android_container_),
        fake_salt_("fake salt"),
        actual_locks_(0),
        expected_locks_(0),
        actual_restarts_(0),
        expected_restarts_(0) {}

  virtual ~SessionManagerImplTest() {}

  void SetUp() override {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    SetSystemSalt(&fake_salt_);

    android_data_dir_ = GetRootPath(kSaneEmail).Append(
        SessionManagerImpl::kAndroidDataDirName);
    android_data_old_dir_ = GetRootPath(kSaneEmail).Append(
        SessionManagerImpl::kAndroidDataOldDirName);

    MockUserPolicyServiceFactory* factory = new MockUserPolicyServiceFactory;
    EXPECT_CALL(*factory, Create(_))
        .WillRepeatedly(
            Invoke(this, &SessionManagerImplTest::CreateUserPolicyService));
    scoped_ptr<DeviceLocalAccountPolicyService> device_local_account_policy(
        new DeviceLocalAccountPolicyService(tmpdir_.path(), NULL));
    impl_.InjectPolicyServices(
        scoped_ptr<DevicePolicyService>(device_policy_service_),
        scoped_ptr<UserPolicyServiceFactory>(factory),
        std::move(device_local_account_policy));
  }

  void TearDown() override {
    SetSystemSalt(NULL);
    EXPECT_EQ(actual_locks_, expected_locks_);
    EXPECT_EQ(actual_restarts_, expected_restarts_);
  }

 protected:
  void ExpectStartSession(const string& account_id_string) {
    ExpectSessionBoilerplate(account_id_string, false, false);
  }

  void ExpectGuestSession() {
    ExpectSessionBoilerplate(kGuestUserName, true, false);
  }

  void ExpectStartOwnerSession(const string& account_id_string) {
    ExpectSessionBoilerplate(account_id_string, false, true);
  }

  void ExpectStartSessionUnowned(const string& account_id_string) {
    ExpectStartSessionUnownedBoilerplate(account_id_string, false, false);
  }

  void ExpectStartSessionOwningInProcess(const string& account_id_string) {
    ExpectStartSessionUnownedBoilerplate(account_id_string, false, true);
  }

  void ExpectStartSessionOwnerLost(const string& account_id_string) {
    ExpectStartSessionUnownedBoilerplate(account_id_string, true, false);
  }

  void ExpectRemoveArcData(DataDirType data_dir_type,
                           OldDataDirType old_data_dir_type) {
#if USE_CHEETS
    if (data_dir_type == DataDirType::DATA_DIR_MISSING &&
        old_data_dir_type == OldDataDirType::OLD_DATA_DIR_EMPTY) {
      return;  // RemoveArcDataInternal does nothing in this case.
    }
    EXPECT_CALL(
        upstart_signal_emitter_delegate_,
        OnSignalEmitted(
            StrEq(SessionManagerImpl::kArcRemoveOldDataSignal),
            ElementsAre(StartsWith("ANDROID_DATA_OLD_DIR="))))
        .Times(1);
#endif
  }

  void ExpectLockScreen() { expected_locks_ = 1; }

  void ExpectDeviceRestart() { expected_restarts_ = 1; }

  void ExpectStorePolicy(MockDevicePolicyService* service,
                         const string& policy,
                         int flags) {
    EXPECT_CALL(*service, Store(CastEq(policy), policy.size(), _, flags))
        .WillOnce(Return(true));
  }

  void ExpectAndRunStartSession(const string& email) {
    ExpectStartSession(email);
    EXPECT_TRUE(impl_.StartSession(email, kNothing, NULL));
  }

  void ExpectAndRunGuestSession() {
    ExpectGuestSession();
    EXPECT_TRUE(impl_.StartSession(kGuestUserName, kNothing, NULL));
  }

  PolicyService* CreateUserPolicyService(const string& username) {
    user_policy_services_[username] = new MockPolicyService();
    return user_policy_services_[username];
  }

  void VerifyAndClearExpectations() {
    Mock::VerifyAndClearExpectations(device_policy_service_);
    for (map<string, MockPolicyService*>::iterator it =
             user_policy_services_.begin();
         it != user_policy_services_.end(); ++it) {
      Mock::VerifyAndClearExpectations(it->second);
    }
    Mock::VerifyAndClearExpectations(&manager_);
    Mock::VerifyAndClearExpectations(&metrics_);
    Mock::VerifyAndClearExpectations(&nss_);
    Mock::VerifyAndClearExpectations(&utils_);
  }

  bool RemoveArcDataInternal(const base::FilePath& android_data_dir,
                             const base::FilePath& android_data_old_dir,
                             SessionManagerImpl::Error* error) {
    return impl_.RemoveArcDataInternal(
        android_data_dir, android_data_old_dir, error);
  }

  // These are bare pointers, not scoped_ptrs, because we need to give them
  // to a SessionManagerImpl instance, but also be able to set expectations
  // on them after we hand them off.
  MockDevicePolicyService* device_policy_service_;
  map<string, MockPolicyService*> user_policy_services_;

  MockDBusSignalEmitter dbus_emitter_;
  StubUpstartSignalEmitter::MockDelegate upstart_signal_emitter_delegate_;
  MockKeyGenerator key_gen_;
  ServerBackedStateKeyGenerator state_key_generator_;
  MockProcessManagerService manager_;
  MockMetrics metrics_;
  MockNssUtil nss_;
  MockSystemUtils utils_;
  FakeCrossystem crossystem_;
  MockVpdProcess vpd_process_;
  MockPolicyKey owner_key_;
  FakeContainerManager android_container_;

  SessionManagerImpl impl_;
  SessionManagerImpl::Error error_;
  base::ScopedTempDir tmpdir_;

  base::FilePath android_data_dir_;
  base::FilePath android_data_old_dir_;

  static const pid_t kDummyPid;
  static const char kNothing[];
  static const char kSaneEmail[];

 private:
  void ExpectSessionBoilerplate(const string& account_id_string,
                                bool guest,
                                bool for_owner) {
    EXPECT_CALL(manager_, SetBrowserSessionForUser(
                              StrEq(account_id_string),
                              StrEq(SanitizeUserName(account_id_string))))
        .Times(1);
    // Expect initialization of the device policy service, return success.
    EXPECT_CALL(*device_policy_service_,
                CheckAndHandleOwnerLogin(StrEq(account_id_string), _, _, _))
        .WillOnce(DoAll(SetArgumentPointee<2>(for_owner), Return(true)));
    // Confirm that the key is present.
    EXPECT_CALL(*device_policy_service_, KeyMissing()).WillOnce(Return(false));

    EXPECT_CALL(metrics_, SendLoginUserType(false, guest, for_owner)).Times(1);
    EXPECT_CALL(
        dbus_emitter_,
        EmitSignalWithString(StrEq(login_manager::kSessionStateChangedSignal),
                             StrEq(SessionManagerImpl::kStarted)))
        .Times(1);
    EXPECT_CALL(utils_, IsDevMode()).WillOnce(Return(false));
  }

  void ExpectStartSessionUnownedBoilerplate(const string& account_id_string,
                                            bool mitigating,
                                            bool owning_in_progress) {
    EXPECT_CALL(manager_, SetBrowserSessionForUser(
                              StrEq(account_id_string),
                              StrEq(SanitizeUserName(account_id_string))))
        .Times(1);

    // Expect initialization of the device policy service, return success.
    EXPECT_CALL(*device_policy_service_,
                CheckAndHandleOwnerLogin(StrEq(account_id_string), _, _, _))
        .WillOnce(DoAll(SetArgumentPointee<2>(false), Return(true)));

    // Indicate that there is no owner key in order to trigger a new one to be
    // generated.
    EXPECT_CALL(*device_policy_service_, KeyMissing()).WillOnce(Return(true));
    EXPECT_CALL(*device_policy_service_, Mitigating())
        .WillRepeatedly(Return(mitigating));
    if (!mitigating && !owning_in_progress)
      EXPECT_CALL(key_gen_, Start(StrEq(account_id_string))).Times(1);
    else
      EXPECT_CALL(key_gen_, Start(_)).Times(0);

    EXPECT_CALL(
        dbus_emitter_,
        EmitSignalWithString(StrEq(login_manager::kSessionStateChangedSignal),
                             StrEq(SessionManagerImpl::kStarted)))
        .Times(1);
    EXPECT_CALL(utils_, IsDevMode()).WillOnce(Return(false));
  }

  void FakeLockScreen() { actual_locks_++; }

  void FakeRestartDevice() { actual_restarts_++; }

  void FakeStartArcInstance() {}

  string fake_salt_;

  // Used by fake closures that simulate calling chrome and powerd to lock
  // the screen and restart the device.
  uint32_t actual_locks_;
  uint32_t expected_locks_;
  uint32_t actual_restarts_;
  uint32_t expected_restarts_;

  DISALLOW_COPY_AND_ASSIGN(SessionManagerImplTest);
};

const pid_t SessionManagerImplTest::kDummyPid = 4;
const char SessionManagerImplTest::kNothing[] = "";
const char SessionManagerImplTest::kSaneEmail[] = "user@somewhere.com";

TEST_F(SessionManagerImplTest, EmitLoginPromptVisible) {
  const char event_name[] = "login-prompt-visible";
  EXPECT_CALL(metrics_, RecordStats(StrEq(event_name))).Times(1);
  EXPECT_CALL(dbus_emitter_,
              EmitSignal(StrEq(login_manager::kLoginPromptVisibleSignal)))
      .Times(1);
  impl_.EmitLoginPromptVisible(&error_);
}

TEST_F(SessionManagerImplTest, EnableChromeTesting) {
  std::vector<std::string> args;
  args.push_back("--repeat-arg");
  args.push_back("--one-time-arg");

  base::FilePath expected = utils_.GetUniqueFilename();
  ASSERT_FALSE(expected.empty());
  string expected_testing_path = expected.value();

  EXPECT_CALL(manager_, RestartBrowserWithArgs(
                            ElementsAre(args[0], args[1],
                                        HasSubstr(expected_testing_path)),
                            true))
      .Times(1);

  string testing_path = impl_.EnableChromeTesting(false, args, NULL);
  EXPECT_TRUE(base::EndsWith(testing_path, expected_testing_path,
                             base::CompareCase::INSENSITIVE_ASCII));

  // Calling again, without forcing relaunch, should not do anything.
  testing_path.clear();
  testing_path = impl_.EnableChromeTesting(false, args, NULL);
  EXPECT_TRUE(base::EndsWith(testing_path, expected_testing_path,
                             base::CompareCase::INSENSITIVE_ASCII));

  // Force relaunch.  Should go through the whole path again.
  args[0] = "--dummy";
  args[1] = "--repeat-arg";
  testing_path.empty();
  EXPECT_CALL(manager_, RestartBrowserWithArgs(
                            ElementsAre(args[0], args[1],
                                        HasSubstr(expected_testing_path)),
                            true))
      .Times(1);

  testing_path = impl_.EnableChromeTesting(true, args, NULL);
  EXPECT_TRUE(base::EndsWith(testing_path, expected_testing_path,
                             base::CompareCase::INSENSITIVE_ASCII));
}

TEST_F(SessionManagerImplTest, StartSession) {
  ExpectStartSession(kSaneEmail);
  EXPECT_TRUE(impl_.StartSession(kSaneEmail, kNothing, &error_));
}

TEST_F(SessionManagerImplTest, StartSession_New) {
  ExpectStartSessionUnowned(kSaneEmail);
  EXPECT_TRUE(impl_.StartSession(kSaneEmail, kNothing, &error_));
}

TEST_F(SessionManagerImplTest, StartSession_InvalidUser) {
  const char bad_email[] = "user";
  EXPECT_FALSE(impl_.StartSession(bad_email, kNothing, &error_));
  EXPECT_EQ(dbus_error::kInvalidAccount, error_.name());
}

TEST_F(SessionManagerImplTest, StartSession_Twice) {
  ExpectStartSession(kSaneEmail);
  EXPECT_TRUE(impl_.StartSession(kSaneEmail, kNothing, NULL));

  EXPECT_FALSE(impl_.StartSession(kSaneEmail, kNothing, &error_));
  EXPECT_EQ(dbus_error::kSessionExists, error_.name());
}

TEST_F(SessionManagerImplTest, StartSession_TwoUsers) {
  ExpectStartSession(kSaneEmail);
  EXPECT_TRUE(impl_.StartSession(kSaneEmail, kNothing, NULL));
  VerifyAndClearExpectations();

  const char email2[] = "user2@somewhere";
  ExpectStartSession(email2);
  EXPECT_TRUE(impl_.StartSession(email2, kNothing, NULL));
}

TEST_F(SessionManagerImplTest, StartSession_OwnerAndOther) {
  ExpectStartSessionUnowned(kSaneEmail);

  EXPECT_TRUE(impl_.StartSession(kSaneEmail, kNothing, NULL));
  VerifyAndClearExpectations();

  const char email2[] = "user2@somewhere";
  ExpectStartSession(email2);
  EXPECT_TRUE(impl_.StartSession(email2, kNothing, NULL));
}

TEST_F(SessionManagerImplTest, StartSession_OwnerRace) {
  ExpectStartSessionUnowned(kSaneEmail);

  EXPECT_TRUE(impl_.StartSession(kSaneEmail, kNothing, NULL));
  VerifyAndClearExpectations();

  const char email2[] = "user2@somewhere";
  ExpectStartSessionOwningInProcess(email2);
  EXPECT_TRUE(impl_.StartSession(email2, kNothing, NULL));
}

TEST_F(SessionManagerImplTest, StartSession_BadNssDB) {
  nss_.MakeBadDB();
  EXPECT_FALSE(impl_.StartSession(kSaneEmail, kNothing, &error_));
  EXPECT_EQ(dbus_error::kNoUserNssDb, error_.name());
}

TEST_F(SessionManagerImplTest, StartSession_DevicePolicyFailure) {
  // Upon the owner login check, return an error.
  EXPECT_CALL(*device_policy_service_,
              CheckAndHandleOwnerLogin(StrEq(kSaneEmail), _, _, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(impl_.StartSession(kSaneEmail, kNothing, &error_));
}

TEST_F(SessionManagerImplTest, StartSession_Owner) {
  ExpectStartOwnerSession(kSaneEmail);
  EXPECT_TRUE(impl_.StartSession(kSaneEmail, kNothing, NULL));
}

TEST_F(SessionManagerImplTest, StartSession_KeyMitigation) {
  ExpectStartSessionOwnerLost(kSaneEmail);
  EXPECT_TRUE(impl_.StartSession(kSaneEmail, kNothing, NULL));
}

TEST_F(SessionManagerImplTest, StopSession) {
  EXPECT_CALL(manager_, ScheduleShutdown()).Times(1);
  EXPECT_TRUE(impl_.StopSession());
}

TEST_F(SessionManagerImplTest, StorePolicy_NoSession) {
  const string fake_policy("fake policy");
  const vector<uint8_t> policy_blob(fake_policy.begin(), fake_policy.end());
  ExpectStorePolicy(device_policy_service_, fake_policy,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW |
                        PolicyService::KEY_CLOBBER);
  impl_.StorePolicy(policy_blob.data(), policy_blob.size(),
                    MockPolicyService::CreateDoNothing());
}

TEST_F(SessionManagerImplTest, StorePolicy_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const string fake_policy("fake policy");
  const vector<uint8_t> policy_blob(fake_policy.begin(), fake_policy.end());
  ExpectStorePolicy(device_policy_service_, fake_policy,
                    PolicyService::KEY_ROTATE);
  impl_.StorePolicy(policy_blob.data(), policy_blob.size(),
                    MockPolicyService::CreateDoNothing());
}

TEST_F(SessionManagerImplTest, RetrievePolicy) {
  const string fake_policy("fake policy");
  const vector<uint8_t> policy_data(fake_policy.begin(), fake_policy.end());
  EXPECT_CALL(*device_policy_service_, Retrieve(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(policy_data), Return(true)));
  vector<uint8_t> out_blob;
  impl_.RetrievePolicy(&out_blob, NULL);

  EXPECT_EQ(fake_policy.size(), out_blob.size());
  EXPECT_TRUE(
      std::equal(fake_policy.begin(), fake_policy.end(), out_blob.begin()));
}

namespace {
void CaptureErrorCode(std::string* storage,
                      const PolicyService::Error& to_capture) {
  DCHECK(storage);
  *storage = to_capture.code();
}
}  // namespace

TEST_F(SessionManagerImplTest, StoreUserPolicy_NoSession) {
  const string fake_policy("fake policy");
  const vector<uint8_t> policy_blob(fake_policy.begin(), fake_policy.end());
  string error_code;
  impl_.StorePolicyForUser(kSaneEmail, policy_blob.data(), policy_blob.size(),
                           base::Bind(&CaptureErrorCode, &error_code));
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error_code);
}

TEST_F(SessionManagerImplTest, StoreUserPolicy_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const string fake_policy("fake policy");
  const vector<uint8_t> policy_blob(fake_policy.begin(), fake_policy.end());
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Store(CastEq(fake_policy), fake_policy.size(), _,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW))
      .WillOnce(Return(true));
  impl_.StorePolicyForUser(kSaneEmail, policy_blob.data(), policy_blob.size(),
                           MockPolicyService::CreateDoNothing());
}

TEST_F(SessionManagerImplTest, StoreUserPolicy_SecondSession) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(user_policy_services_[kSaneEmail]);

  // Store policy for the signed-in user.
  const std::string fake_policy("fake policy");
  const vector<uint8_t> policy_blob(fake_policy.begin(), fake_policy.end());
  EXPECT_CALL(*user_policy_services_[kSaneEmail],
              Store(CastEq(fake_policy), fake_policy.size(), _,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW))
      .WillOnce(Return(true));
  impl_.StorePolicyForUser(kSaneEmail, policy_blob.data(), policy_blob.size(),
                           MockPolicyService::CreateDoNothing());
  Mock::VerifyAndClearExpectations(user_policy_services_[kSaneEmail]);

  // Storing policy for another username fails before his session starts.
  const char user2[] = "user2@somewhere.com";
  string error_code;
  impl_.StorePolicyForUser(user2, policy_blob.data(), policy_blob.size(),
                           base::Bind(&CaptureErrorCode, &error_code));
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error_code);

  // Now start another session for the 2nd user.
  ExpectAndRunStartSession(user2);
  ASSERT_TRUE(user_policy_services_[user2]);

  // Storing policy for that user now succeeds.
  EXPECT_CALL(*user_policy_services_[user2],
              Store(CastEq(fake_policy), fake_policy.size(), _,
                    PolicyService::KEY_ROTATE | PolicyService::KEY_INSTALL_NEW))
      .WillOnce(Return(true));
  impl_.StorePolicyForUser(user2, policy_blob.data(), policy_blob.size(),
                           MockPolicyService::CreateDoNothing());
  Mock::VerifyAndClearExpectations(user_policy_services_[user2]);
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicy_NoSession) {
  vector<uint8_t> out_blob;
  impl_.RetrievePolicyForUser(kSaneEmail, &out_blob, &error_);
  EXPECT_EQ(out_blob.size(), 0);
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error_.name());
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicy_SessionStarted) {
  ExpectAndRunStartSession(kSaneEmail);
  const string fake_policy("fake policy");
  const vector<uint8_t> policy_data(fake_policy.begin(), fake_policy.end());
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Retrieve(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(policy_data), Return(true)));
  vector<uint8_t> out_blob;
  impl_.RetrievePolicyForUser(kSaneEmail, &out_blob, &error_);
  EXPECT_EQ(fake_policy.size(), out_blob.size());
  EXPECT_TRUE(
      std::equal(fake_policy.begin(), fake_policy.end(), out_blob.begin()));
}

TEST_F(SessionManagerImplTest, RetrieveUserPolicy_SecondSession) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(user_policy_services_[kSaneEmail]);

  // Retrieve policy for the signed-in user.
  const std::string fake_policy("fake policy");
  const std::vector<uint8_t> policy_data(fake_policy.begin(),
                                         fake_policy.end());
  EXPECT_CALL(*user_policy_services_[kSaneEmail], Retrieve(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(policy_data), Return(true)));
  std::vector<uint8_t> out_blob;
  impl_.RetrievePolicyForUser(kSaneEmail, &out_blob, NULL);
  Mock::VerifyAndClearExpectations(user_policy_services_[kSaneEmail]);
  EXPECT_EQ(fake_policy.size(), out_blob.size());
  EXPECT_TRUE(
      std::equal(fake_policy.begin(), fake_policy.end(), out_blob.begin()));

  // Retrieving policy for another username fails before his session starts.
  const char user2[] = "user2@somewhere.com";
  out_blob.clear();
  impl_.RetrievePolicyForUser(user2, &out_blob, &error_);
  EXPECT_EQ(error_.name(), dbus_error::kSessionDoesNotExist);

  // Now start another session for the 2nd user.
  ExpectAndRunStartSession(user2);
  ASSERT_TRUE(user_policy_services_[user2]);

  // Retrieving policy for that user now succeeds.
  EXPECT_CALL(*user_policy_services_[user2], Retrieve(_))
      .WillOnce(DoAll(SetArgumentPointee<0>(policy_data), Return(true)));
  out_blob.clear();
  impl_.RetrievePolicyForUser(user2, &out_blob, NULL);
  Mock::VerifyAndClearExpectations(user_policy_services_[user2]);
  EXPECT_EQ(fake_policy.size(), out_blob.size());
  EXPECT_TRUE(
      std::equal(fake_policy.begin(), fake_policy.end(), out_blob.begin()));
}

TEST_F(SessionManagerImplTest, RetrieveActiveSessions) {
  ExpectStartSession(kSaneEmail);
  EXPECT_TRUE(impl_.StartSession(kSaneEmail, kNothing, NULL));
  std::map<std::string, std::string> active_users;
  impl_.RetrieveActiveSessions(&active_users);
  EXPECT_EQ(active_users.size(), 1);
  EXPECT_EQ(active_users[kSaneEmail], SanitizeUserName(kSaneEmail));
  VerifyAndClearExpectations();

  const char email2[] = "user2@somewhere";
  ExpectStartSession(email2);
  EXPECT_TRUE(impl_.StartSession(email2, kNothing, NULL));
  active_users.clear();
  impl_.RetrieveActiveSessions(&active_users);
  EXPECT_EQ(active_users.size(), 2);
  EXPECT_EQ(active_users[kSaneEmail], SanitizeUserName(kSaneEmail));
  EXPECT_EQ(active_users[email2], SanitizeUserName(email2));
}

TEST_F(SessionManagerImplTest, RestartJobBadSocket) {
  EXPECT_FALSE(impl_.RestartJob(-1, {}, &error_));
  EXPECT_EQ("GetPeerCredsFailed", error_.name());
}

TEST_F(SessionManagerImplTest, RestartJobBadPid) {
  int sockets[2] = {-1, -1};
  ASSERT_GE(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(false));
  EXPECT_FALSE(impl_.RestartJob(sockets[1], {}, &error_));
  EXPECT_EQ(dbus_error::kUnknownPid, error_.name());
}

TEST_F(SessionManagerImplTest, RestartJobSuccess) {
  int sockets[2] = {-1, -1};
  ASSERT_GE(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  const std::vector<std::string> argv = {
      "program",
      "--switch1",
      "--switch2=switch2_value",
      "--switch3=escaped_\"_quote",
      "--switch4=white space",
      "arg1",
      "arg 2",
  };

  EXPECT_CALL(manager_, IsBrowser(getpid())).WillRepeatedly(Return(true));
  EXPECT_CALL(manager_, RestartBrowserWithArgs(ElementsAreArray(argv), false))
      .Times(1);
  ExpectGuestSession();

  EXPECT_EQ(TRUE, impl_.RestartJob(sockets[1], argv, &error_));
}

TEST_F(SessionManagerImplTest, SupervisedUserCreation) {
  impl_.HandleSupervisedUserCreationStarting();
  EXPECT_TRUE(impl_.ShouldEndSession());
  impl_.HandleSupervisedUserCreationFinished();
  EXPECT_FALSE(impl_.ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockScreen) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  impl_.LockScreen(NULL);
  EXPECT_TRUE(impl_.ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockScreen_DuringSupervisedUserCreation) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  EXPECT_CALL(dbus_emitter_, EmitSignal(_)).Times(AnyNumber());

  impl_.HandleSupervisedUserCreationStarting();
  EXPECT_TRUE(impl_.ShouldEndSession());
  impl_.LockScreen(NULL);
  EXPECT_TRUE(impl_.ShouldEndSession());
  impl_.HandleLockScreenShown();
  EXPECT_TRUE(impl_.ShouldEndSession());
  impl_.HandleLockScreenDismissed();
  EXPECT_TRUE(impl_.ShouldEndSession());
  impl_.HandleSupervisedUserCreationFinished();
  EXPECT_FALSE(impl_.ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockScreen_InterleavedSupervisedUserCreation) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  EXPECT_CALL(dbus_emitter_, EmitSignal(_)).Times(AnyNumber());

  impl_.HandleSupervisedUserCreationStarting();
  EXPECT_TRUE(impl_.ShouldEndSession());
  impl_.LockScreen(NULL);
  EXPECT_TRUE(impl_.ShouldEndSession());
  impl_.HandleLockScreenShown();
  EXPECT_TRUE(impl_.ShouldEndSession());
  impl_.HandleSupervisedUserCreationFinished();
  EXPECT_TRUE(impl_.ShouldEndSession());
  impl_.HandleLockScreenDismissed();
  EXPECT_FALSE(impl_.ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockScreen_MultiSession) {
  ExpectAndRunStartSession("user@somewhere");
  ExpectAndRunStartSession("user2@somewhere");
  ExpectLockScreen();
  impl_.LockScreen(NULL);
  EXPECT_EQ(TRUE, impl_.ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockScreen_NoSession) {
  impl_.LockScreen(&error_);
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error_.name());
}

TEST_F(SessionManagerImplTest, LockScreen_Guest) {
  ExpectAndRunGuestSession();
  impl_.LockScreen(&error_);
  EXPECT_EQ(dbus_error::kSessionExists, error_.name());
}

TEST_F(SessionManagerImplTest, LockScreen_UserAndGuest) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectAndRunGuestSession();
  ExpectLockScreen();
  impl_.LockScreen(&error_);
  EXPECT_EQ(TRUE, impl_.ShouldEndSession());
}

TEST_F(SessionManagerImplTest, LockUnlockScreen) {
  ExpectAndRunStartSession(kSaneEmail);
  ExpectLockScreen();
  impl_.LockScreen(&error_);
  EXPECT_EQ(TRUE, impl_.ShouldEndSession());

  EXPECT_CALL(dbus_emitter_,
              EmitSignal(StrEq(login_manager::kScreenIsLockedSignal)))
      .Times(1);
  impl_.HandleLockScreenShown();
  EXPECT_EQ(TRUE, impl_.ShouldEndSession());

  EXPECT_CALL(dbus_emitter_,
              EmitSignal(StrEq(login_manager::kScreenIsUnlockedSignal)))
      .Times(1);
  impl_.HandleLockScreenDismissed();
  EXPECT_EQ(FALSE, impl_.ShouldEndSession());
}

TEST_F(SessionManagerImplTest, StartDeviceWipe_AlreadyLoggedIn) {
  base::FilePath logged_in_path(SessionManagerImpl::kLoggedInFlag);
  ASSERT_FALSE(utils_.Exists(logged_in_path));
  ASSERT_TRUE(utils_.AtomicFileWrite(logged_in_path, "1"));
  impl_.StartDeviceWipe("test", &error_);
  EXPECT_EQ(error_.name(), dbus_error::kSessionExists);
}

TEST_F(SessionManagerImplTest, StartDeviceWipe) {
  base::FilePath logged_in_path(SessionManagerImpl::kLoggedInFlag);
  base::FilePath reset_path(SessionManagerImpl::kResetFile);
  ASSERT_TRUE(utils_.RemoveFile(logged_in_path));
  ExpectDeviceRestart();
  impl_.StartDeviceWipe(
      "overly long test message with\nspecial/chars$\t\xa4\xd6 1234567890",
      NULL);
  std::string contents;
  ASSERT_TRUE(utils_.ReadFileToString(reset_path, &contents));
  ASSERT_EQ(
      "fast safe keepimg reason="
      "overly_long_test_message_with_special_chars_____12",
      contents);
}

TEST_F(SessionManagerImplTest, ImportValidateAndStoreGeneratedKey) {
  base::FilePath key_file_path;
  string key("key_contents");
  ASSERT_TRUE(base::CreateTemporaryFileInDir(tmpdir_.path(), &key_file_path));
  ASSERT_EQ(base::WriteFile(key_file_path, key.c_str(), key.size()),
            key.size());

  // Start a session, to set up NSSDB for the user.
  ExpectStartOwnerSession(kSaneEmail);
  ASSERT_TRUE(impl_.StartSession(kSaneEmail, kNothing, NULL));

  EXPECT_CALL(
      *device_policy_service_,
      ValidateAndStoreOwnerKey(StrEq(kSaneEmail), StrEq(key), nss_.GetSlot()))
      .WillOnce(Return(true));

  impl_.OnKeyGenerated(kSaneEmail, key_file_path);
  EXPECT_FALSE(base::PathExists(key_file_path));
}

TEST_F(SessionManagerImplTest, ContainerStart) {
  const std::string kContainerName = "testc";

  ExpectAndRunStartSession(kSaneEmail);

  impl_.StartContainer(kContainerName, &error_);
  EXPECT_FALSE(android_container_.running());
}

TEST_F(SessionManagerImplTest, ArcInstanceStart) {
  ExpectAndRunStartSession(kSaneEmail);
  SessionManagerImpl::Error start_time_error;
#if USE_CHEETS
  impl_.GetArcStartTime(&start_time_error);
  EXPECT_EQ(dbus_error::kNotStarted, start_time_error.name());

  // Check that StartArcInstance emits the signal with ANDROID_DATA_OLD_DIR=
  // when |android_data_old_dir_| is not empty.
  ASSERT_TRUE(utils_.AtomicFileWrite(
      android_data_old_dir_.Append("foo"), "test"));

  // TODO(lhchavez): Once session_manager controls the ARC instance process and
  // upstart is not used, verify that the instance is killed when the session
  // ends.
  EXPECT_CALL(
      upstart_signal_emitter_delegate_,
      OnSignalEmitted(
          StrEq(SessionManagerImpl::kArcStartSignal),
          ElementsAre(StartsWith("ANDROID_DATA_DIR="),
                      std::string("CHROMEOS_USER=") + kSaneEmail,
                      StartsWith("ANDROID_DATA_OLD_DIR="))))
      .Times(1);
  EXPECT_CALL(
      upstart_signal_emitter_delegate_,
      OnSignalEmitted(StrEq(SessionManagerImpl::kArcStopSignal),
                      ElementsAre(std::string("ANDROID_PID=") +
                                      std::to_string(kAndroidPid))))
      .Times(1);
  EXPECT_CALL(
      upstart_signal_emitter_delegate_,
      OnSignalEmitted(StrEq(SessionManagerImpl::kArcNetworkStartSignal),
                      ElementsAre(std::string("CONTAINER_NAME=") +
                                      SessionManagerImpl::kArcContainerName,
                                  std::string("CONTAINER_PATH="),
                                  std::string("CONTAINER_PID=") +
                                      std::to_string(kAndroidPid))))
      .Times(1);
  EXPECT_CALL(
      upstart_signal_emitter_delegate_,
      OnSignalEmitted(StrEq(SessionManagerImpl::kArcNetworkStopSignal),
                      ElementsAre()))
      .Times(1);
  impl_.StartArcInstance(kSaneEmail, &error_);
  EXPECT_TRUE(android_container_.running());
  EXPECT_NE(base::TimeTicks(), impl_.GetArcStartTime(&start_time_error));
  impl_.StopArcInstance(&error_);
  EXPECT_FALSE(error_.is_set());
  EXPECT_FALSE(android_container_.running());
#else
  impl_.StartArcInstance(kSaneEmail, &error_);
  EXPECT_EQ(dbus_error::kNotAvailable, error_.name());
  impl_.GetArcStartTime(&start_time_error);
  EXPECT_EQ(dbus_error::kNotAvailable, start_time_error.name());
#endif
}

TEST_F(SessionManagerImplTest, ArcInstanceStart_NoSession) {
#if USE_CHEETS
  impl_.StartArcInstance(kSaneEmail, &error_);
  EXPECT_EQ(dbus_error::kSessionDoesNotExist, error_.name());
#endif
}

TEST_F(SessionManagerImplTest, ArcInstanceCrash) {
#if USE_CHEETS
  ExpectAndRunStartSession(kSaneEmail);

  EXPECT_CALL(
      upstart_signal_emitter_delegate_,
      OnSignalEmitted(StrEq(SessionManagerImpl::kArcStartSignal),
                      ElementsAre(StartsWith("ANDROID_DATA_DIR="),
                                  std::string("CHROMEOS_USER=") + kSaneEmail)))
      .Times(1);
  EXPECT_CALL(
      upstart_signal_emitter_delegate_,
      OnSignalEmitted(StrEq(SessionManagerImpl::kArcStopSignal),
                      ElementsAre(std::string("ANDROID_PID=") +
                                      std::to_string(kAndroidPid))))
      .Times(1);
  EXPECT_CALL(
      dbus_emitter_,
      EmitSignalWithBool(StrEq(login_manager::kArcInstanceStopped),
                         false))
      .Times(1);
  EXPECT_CALL(
      upstart_signal_emitter_delegate_,
      OnSignalEmitted(StrEq(SessionManagerImpl::kArcNetworkStartSignal),
                      ElementsAre(std::string("CONTAINER_NAME=") +
                                      SessionManagerImpl::kArcContainerName,
                                  std::string("CONTAINER_PATH="),
                                  std::string("CONTAINER_PID=") +
                                      std::to_string(kAndroidPid))))
      .Times(1);
  EXPECT_CALL(
      upstart_signal_emitter_delegate_,
      OnSignalEmitted(StrEq(SessionManagerImpl::kArcNetworkStopSignal),
                      ElementsAre()))
      .Times(1);
  impl_.StartArcInstance(kSaneEmail, &error_);
  EXPECT_TRUE(android_container_.running());
  android_container_.SimulateCrash();
  EXPECT_FALSE(android_container_.running());
  // This should now fail since the container was cleaned up already.
  impl_.StopArcInstance(&error_);
  EXPECT_EQ(dbus_error::kContainerShutdownFail, error_.name());
#endif
}

#if USE_CHEETS
TEST_F(SessionManagerImplTest, ArcRemoveDataInternal) {
  // Test that RemoveArcDataInternal() removes |android_data_dir_| and returns
  // true even if the directory is not empty.
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_FALSE(utils_.Exists(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  EXPECT_TRUE(RemoveArcDataInternal(
      android_data_dir_, android_data_old_dir_, &error_));
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveDataInternal_NoSourceDirectory) {
  // Test that RemoveArcDataInternal() returns true when the directory does not
  // exist.
  ASSERT_FALSE(utils_.Exists(android_data_dir_));
  ASSERT_FALSE(utils_.Exists(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_MISSING,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  EXPECT_TRUE(RemoveArcDataInternal(
      android_data_dir_, android_data_old_dir_, &error_));
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveDataInternal_OldDirectoryExists) {
  // Test that RemoveArcDataInternal() can remove |android_data_dir_| and
  // returns true even if the "old" directory already exists.
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_TRUE(utils_.CreateDir(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  EXPECT_TRUE(RemoveArcDataInternal(
      android_data_dir_, android_data_old_dir_, &error_));
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest,
       ArcRemoveDataInternal_NonEmptyOldDirectoryExists) {
  // Test that RemoveArcDataInternal() can remove |android_data_dir_| and
  // returns true even if the "old" directory already exists and is not empty.
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_TRUE(utils_.AtomicFileWrite(
      android_data_old_dir_.Append("bar"), "test2"));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_NOT_EMPTY);
  EXPECT_TRUE(RemoveArcDataInternal(
      android_data_dir_, android_data_old_dir_, &error_));
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest,
       ArcRemoveDataInternal_NoSourceDirectoryButOldDirectoryExists) {
  // Test that RemoveArcDataInternal() removes the "old" directory and returns
  // true even when |android_data_dir_| does not exist at all.
  ASSERT_FALSE(utils_.Exists(android_data_dir_));
  ASSERT_TRUE(utils_.CreateDir(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_MISSING,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  EXPECT_TRUE(RemoveArcDataInternal(
      android_data_dir_, android_data_old_dir_, &error_));
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest,
       ArcRemoveDataInternal_NoSourceDirectoryButNonEmptyOldDirectoryExists) {
  // Test that RemoveArcDataInternal() removes the "old" directory and returns
  // true even when |android_data_dir_| does not exist at all.
  ASSERT_FALSE(utils_.Exists(android_data_dir_));
  ASSERT_TRUE(utils_.AtomicFileWrite(
      android_data_old_dir_.Append("foo"), "test"));
  ExpectRemoveArcData(DataDirType::DATA_DIR_MISSING,
                      OldDataDirType::OLD_DATA_DIR_NOT_EMPTY);
  EXPECT_TRUE(RemoveArcDataInternal(
      android_data_dir_, android_data_old_dir_, &error_));
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveDataInternal_OldFileExists) {
  // Test that RemoveArcDataInternal() can remove |android_data_dir_| and
  // returns true even if the "old" path exists as a file. This should never
  // happen, but RemoveArcDataInternal() can handle the case.
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_old_dir_, "test2"));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_FILE_EXISTS);
  EXPECT_TRUE(RemoveArcDataInternal(
      android_data_dir_, android_data_old_dir_, &error_));
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveData) {
  // Test the wrapper function, ArcRemoveData, without running ARC.
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_FALSE(utils_.Exists(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  impl_.RemoveArcData(kSaneEmail, &error_);
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveData_ArcRunning) {
  // Test that RemoveArcData does nothing when ARC is running.
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_FALSE(utils_.Exists(android_data_old_dir_));
  impl_.StartArcInstance(kSaneEmail, &error_);
  impl_.RemoveArcData(kSaneEmail, &error_);
  EXPECT_EQ(dbus_error::kArcInstanceRunning, error_.name());
  EXPECT_TRUE(utils_.Exists(android_data_dir_));
}

TEST_F(SessionManagerImplTest, ArcRemoveData_ArcStopped) {
  ExpectAndRunStartSession(kSaneEmail);
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_TRUE(utils_.AtomicFileWrite(
      android_data_old_dir_.Append("bar"), "test2"));
  impl_.StartArcInstance(kSaneEmail, &error_);
  impl_.StopArcInstance(&error_);
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_NOT_EMPTY);
  impl_.RemoveArcData(kSaneEmail, &error_);
  EXPECT_FALSE(utils_.Exists(android_data_dir_));
}
#else
// When USE_CHEETS is not defined, these methods should immediately return
// dbus_error::kNotAvailable.
TEST_F(SessionManagerImplTest, ArcRemoveDataInternal) {
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_FALSE(utils_.Exists(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  EXPECT_TRUE(RemoveArcDataInternal(
      android_data_dir_, android_data_old_dir_, &error_));
  EXPECT_EQ(dbus_error::kNotAvailable, error_.name());
}

TEST_F(SessionManagerImplTest, ArcRemoveData) {
  ASSERT_TRUE(utils_.AtomicFileWrite(android_data_dir_.Append("foo"), "test"));
  ASSERT_FALSE(utils_.Exists(android_data_old_dir_));
  ExpectRemoveArcData(DataDirType::DATA_DIR_AVAILABLE,
                      OldDataDirType::OLD_DATA_DIR_EMPTY);
  impl_.RemoveArcData(kSaneEmail, &error_);
  EXPECT_EQ(dbus_error::kNotAvailable, error_.name());
}
#endif

TEST_F(SessionManagerImplTest, PrioritizeArcInstance) {
#if !USE_CHEETS
  impl_.PrioritizeArcInstance(&error_);
  EXPECT_EQ(dbus_error::kNotAvailable, error_.name());
#endif
}

class SessionManagerImplStaticTest : public ::testing::Test {
 public:
  SessionManagerImplStaticTest() {}
  virtual ~SessionManagerImplStaticTest() {}

  bool ValidateEmail(const string& email_address) {
    return SessionManagerImpl::ValidateEmail(email_address);
  }

  bool ValidateGaiaIdKey(const string& account_id) {
    return SessionManagerImpl::ValidateGaiaIdKey(account_id);
  }
};

TEST_F(SessionManagerImplStaticTest, EmailAddressTest) {
  EXPECT_TRUE(ValidateEmail("user_who+we.like@some-where.com"));
  EXPECT_TRUE(ValidateEmail("john_doe's_mail@some-where.com"));
}

TEST_F(SessionManagerImplStaticTest, EmailAddressNonAsciiTest) {
  char invalid[4] = "a@m";
  invalid[2] = static_cast<char>(254);
  EXPECT_FALSE(ValidateEmail(invalid));
}

TEST_F(SessionManagerImplStaticTest, EmailAddressNoAtTest) {
  const char no_at[] = "user";
  EXPECT_FALSE(ValidateEmail(no_at));
}

TEST_F(SessionManagerImplStaticTest, EmailAddressTooMuchAtTest) {
  const char extra_at[] = "user@what@where";
  EXPECT_FALSE(ValidateEmail(extra_at));
}

TEST_F(SessionManagerImplStaticTest, GaiaIdKeyTest) {
  EXPECT_TRUE(ValidateGaiaIdKey("g-1234567890123456"));
  // email string is invalid GaiaIdKey
  EXPECT_FALSE(ValidateGaiaIdKey("john@some.where.com"));
  // Only alphanumeric characters plus a colon are allowed.
  EXPECT_TRUE(ValidateGaiaIdKey("g-1234567890"));
  EXPECT_TRUE(ValidateGaiaIdKey("g-abcdef0123456789"));
  EXPECT_TRUE(ValidateGaiaIdKey("g-ABCDEF0123456789"));
  EXPECT_FALSE(ValidateGaiaIdKey("g-123@some.where.com"));
  EXPECT_FALSE(ValidateGaiaIdKey("g-123@localhost"));
}

}  // namespace login_manager
