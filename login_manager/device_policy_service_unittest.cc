// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/device_policy_service.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <base/basictypes.h>
#include <base/file_util.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/message_loop/message_loop.h>
#include <base/message_loop/message_loop_proxy.h>
#include <base/run_loop.h>
#include <crypto/scoped_nss_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/chrome_device_policy.pb.h"
#include "login_manager/device_management_backend.pb.h"
#include "login_manager/install_attributes.pb.h"
#include "login_manager/matchers.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_mitigator.h"
#include "login_manager/mock_nss_util.h"
#include "login_manager/mock_policy_key.h"
#include "login_manager/mock_policy_service.h"
#include "login_manager/mock_policy_store.h"

namespace em = enterprise_management;

using google::protobuf::RepeatedPtrField;

using testing::AnyNumber;
using testing::AtLeast;
using testing::DoAll;
using testing::Expectation;
using testing::Mock;
using testing::Return;
using testing::ReturnRef;
using testing::Sequence;
using testing::StrictMock;
using testing::WithArg;
using testing::_;

namespace {

ACTION_P(AssignVector, str) {
  arg0->assign(str.begin(), str.end());
}

}  // namespace

namespace login_manager {

class DevicePolicyServiceTest : public ::testing::Test {
 public:
  DevicePolicyServiceTest()
      : owner_("user@somewhere"),
        fake_sig_("fake_signature"),
        fake_key_("fake_key"),
        new_fake_sig_("new_fake_signature"),
        store_(NULL) {
    fake_key_vector_.assign(fake_key_.begin(), fake_key_.end());
  }

  virtual void SetUp() {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
    serial_recovery_flag_file_ =
        tmpdir_.path().AppendASCII("serial_recovery_flag");
    policy_file_ =
        tmpdir_.path().AppendASCII("policy");
    install_attributes_file_ =
        tmpdir_.path().AppendASCII("install_attributes.pb");
  }

  void InitPolicy(const em::ChromeDeviceSettingsProto& settings,
                  const std::string& owner,
                  const std::string& signature,
                  const std::string& request_token,
                  bool valid_serial_number_missing) {
    std::string settings_str;
    ASSERT_TRUE(settings.SerializeToString(&settings_str));

    em::PolicyData policy_data;
    policy_data.set_policy_type(DevicePolicyService::kDevicePolicyType);
    policy_data.set_policy_value(settings_str);
    if (!owner.empty())
      policy_data.set_username(owner);
    if (!request_token.empty())
      policy_data.set_request_token(request_token);
    if (valid_serial_number_missing)
      policy_data.set_valid_serial_number_missing(true);
    std::string policy_data_str;
    ASSERT_TRUE(policy_data.SerializeToString(&policy_data_str));

    policy_proto_.Clear();
    policy_proto_.set_policy_data(policy_data_str);
    policy_proto_.set_policy_data_signature(signature);
    ASSERT_TRUE(policy_proto_.SerializeToString(&policy_str_));
  }

  void InitEmptyPolicy(const std::string& owner,
                       const std::string& signature,
                       const std::string& request_token) {
    em::ChromeDeviceSettingsProto settings;
    InitPolicy(settings, owner, signature, request_token, false);
  }

  void InitService(NssUtil* nss) {
    store_ = new StrictMock<MockPolicyStore>;
    metrics_.reset(new MockMetrics);
    mitigator_.reset(new StrictMock<MockMitigator>);
    scoped_refptr<base::MessageLoopProxy> message_loop(
        base::MessageLoopProxy::current());
    service_.reset(new DevicePolicyService(
        serial_recovery_flag_file_,
        policy_file_,
        install_attributes_file_,
        scoped_ptr<PolicyStore>(store_),
        &key_,
        message_loop,
        metrics_.get(),
        mitigator_.get(),
        nss));

    // Allow the key to be read any time.
    EXPECT_CALL(key_, public_key_der())
        .WillRepeatedly(ReturnRef(fake_key_vector_));
  }

  void RecordNewPolicy(const em::PolicyFetchResponse& policy) {
    new_policy_proto_.CopyFrom(policy);
  }

  // Checks that the recorded |new_policy_| has been modified accordingly from
  // |policy_proto_| while logging in |owner_| as a device owner.
  void CheckNewOwnerSettings(
      const em::ChromeDeviceSettingsProto& old_settings) {
    // Check PolicyFetchResponse wrapper.
    ASSERT_EQ(new_fake_sig_, new_policy_proto_.policy_data_signature());
    ASSERT_EQ(fake_key_, new_policy_proto_.new_public_key());
    ASSERT_EQ(std::string(), new_policy_proto_.new_public_key_signature());
    ASSERT_TRUE(new_policy_proto_.has_policy_data());

    // Check signed policy data.
    em::PolicyData policy_data;
    ASSERT_TRUE(policy_data.ParseFromString(new_policy_proto_.policy_data()));
    ASSERT_EQ(DevicePolicyService::kDevicePolicyType,
              policy_data.policy_type());
    ASSERT_FALSE(policy_data.has_request_token());
    ASSERT_TRUE(policy_data.has_policy_value());
    ASSERT_EQ(policy_data.username(), owner_);

    // Check the device settings.
    em::ChromeDeviceSettingsProto settings;
    ASSERT_TRUE(settings.ParseFromString(policy_data.policy_value()));
    ASSERT_TRUE(settings.has_user_whitelist());
    const RepeatedPtrField<std::string>& old_whitelist(
        old_settings.user_whitelist().user_whitelist());
    const RepeatedPtrField<std::string>& whitelist(
        settings.user_whitelist().user_whitelist());
    ASSERT_LE(old_whitelist.size(), whitelist.size());
    ASSERT_GE(old_whitelist.size() + 1, whitelist.size());
    std::set<std::string> old_whitelist_set(old_whitelist.begin(),
                                            old_whitelist.end());
    old_whitelist_set.insert(owner_);
    std::set<std::string> whitelist_set(whitelist.begin(),
                                        whitelist.end());
    ASSERT_EQ(old_whitelist_set.size(), whitelist_set.size());
    ASSERT_TRUE(std::equal(whitelist_set.begin(), whitelist_set.end(),
                           old_whitelist_set.begin()));
    ASSERT_TRUE(settings.has_allow_new_users());

    // Make sure no other fields have been touched.
    settings.clear_user_whitelist();
    settings.clear_allow_new_users();
    em::ChromeDeviceSettingsProto blank_settings;
    ASSERT_EQ(settings.SerializeAsString(), blank_settings.SerializeAsString());
  }

  void ExpectMitigating(bool mitigating) {
    EXPECT_CALL(*mitigator_.get(), Mitigating())
        .WillRepeatedly(Return(mitigating));
  }

  void ExpectGetPolicy(Sequence sequence,
                       const em::PolicyFetchResponse& policy) {
    EXPECT_CALL(*store_, Get())
        .InSequence(sequence)
        .WillRepeatedly(ReturnRef(policy));
  }

  void ExpectInstallNewOwnerPolicy(Sequence sequence, MockNssUtil* nss) {
    Expectation get_policy =
        EXPECT_CALL(*store_, Get())
            .WillRepeatedly(ReturnRef(policy_proto_));
    Expectation compare_keys =
        EXPECT_CALL(key_, Equals(_))
            .WillRepeatedly(Return(false));
    Expectation sign =
        EXPECT_CALL(*nss, Sign(_, _, _, _))
            .After(get_policy)
            .WillOnce(DoAll(WithArg<2>(AssignVector(new_fake_sig_)),
                            Return(true)));
    Expectation set_policy = EXPECT_CALL(*store_, Set(_))
        .InSequence(sequence)
        .After(sign, compare_keys)
        .WillOnce(Invoke(this, &DevicePolicyServiceTest::RecordNewPolicy));
  }

  void ExpectFailedInstallNewOwnerPolicy(Sequence sequence, MockNssUtil* nss) {
    Expectation get_policy =
        EXPECT_CALL(*store_, Get())
            .WillRepeatedly(ReturnRef(policy_proto_));
    Expectation compare_keys =
        EXPECT_CALL(key_, Equals(_))
            .WillRepeatedly(Return(false));
    Expectation sign =
        EXPECT_CALL(*nss, Sign(_, _, _, _))
            .After(get_policy)
            .WillOnce(DoAll(WithArg<2>(AssignVector(new_fake_sig_)),
                            Return(false)));
  }

  void ExpectPersistKeyAndPolicy() {
    Mock::VerifyAndClearExpectations(&key_);
    Mock::VerifyAndClearExpectations(store_);

    EXPECT_CALL(key_, Persist())
        .WillOnce(Return(true));
    EXPECT_CALL(*store_, Persist())
        .WillOnce(Return(true));
    base::RunLoop().RunUntilIdle();
  }

  void ExpectNoPersistKeyAndPolicy() {
    Mock::VerifyAndClearExpectations(&key_);
    Mock::VerifyAndClearExpectations(store_);

    EXPECT_CALL(key_, Persist()).Times(0);
    EXPECT_CALL(*store_, Persist()).Times(0);
    base::RunLoop().RunUntilIdle();
  }

  void ExpectKeyPopulated(bool key_populated) {
    EXPECT_CALL(key_, HaveCheckedDisk())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(key_, IsPopulated())
        .WillRepeatedly(Return(key_populated));
  }

  LoginMetrics::PolicyFileState SimulateNullPolicy() {
    EXPECT_CALL(*store_, Get())
        .WillRepeatedly(ReturnRef(new_policy_proto_));
    return LoginMetrics::NOT_PRESENT;
  }

  LoginMetrics::PolicyFileState SimulateGoodPolicy() {
    InitEmptyPolicy(owner_, fake_sig_, "");
    EXPECT_CALL(*store_, Get()).WillRepeatedly(ReturnRef(policy_proto_));
    return LoginMetrics::GOOD;
  }

  LoginMetrics::PolicyFileState SimulateNullPrefs() {
    EXPECT_CALL(*store_, DefunctPrefsFilePresent()).WillOnce(Return(false));
    return LoginMetrics::NOT_PRESENT;
  }

  LoginMetrics::PolicyFileState SimulateExtantPrefs() {
    EXPECT_CALL(*store_, DefunctPrefsFilePresent()).WillOnce(Return(true));
    return LoginMetrics::GOOD;
  }

  LoginMetrics::PolicyFileState SimulateNullOwnerKey() {
    EXPECT_CALL(key_, IsPopulated()).WillRepeatedly(Return(false));
    return LoginMetrics::NOT_PRESENT;
  }

  LoginMetrics::PolicyFileState SimulateBadOwnerKey(MockNssUtil* nss) {
    EXPECT_CALL(key_, IsPopulated()).WillRepeatedly(Return(true));
    EXPECT_CALL(*nss, CheckPublicKeyBlob(fake_key_vector_))
        .WillRepeatedly(Return(false));
    return LoginMetrics::MALFORMED;
  }

  LoginMetrics::PolicyFileState SimulateGoodOwnerKey(MockNssUtil* nss) {
    EXPECT_CALL(key_, IsPopulated()).WillRepeatedly(Return(true));
    EXPECT_CALL(*nss, CheckPublicKeyBlob(fake_key_vector_))
        .WillRepeatedly(Return(true));
    return LoginMetrics::GOOD;
  }

  bool PolicyAllowsNewUsers(const em::ChromeDeviceSettingsProto settings) {
    InitPolicy(settings, owner_, fake_sig_, "", false);
    return DevicePolicyService::PolicyAllowsNewUsers(policy_proto_);
  }

  em::PolicyFetchResponse policy_proto_;
  std::string policy_str_;

  em::PolicyFetchResponse new_policy_proto_;

  std::string owner_;
  std::string fake_sig_;
  std::string fake_key_;
  std::vector<uint8> fake_key_vector_;
  std::string new_fake_sig_;

  base::MessageLoop loop_;

  base::ScopedTempDir tmpdir_;
  base::FilePath serial_recovery_flag_file_;
  base::FilePath policy_file_;
  base::FilePath install_attributes_file_;

  // Use StrictMock to make sure that no unexpected policy or key mutations can
  // occur without the test failing.
  StrictMock<MockPolicyKey> key_;
  StrictMock<MockPolicyStore>* store_;
  scoped_ptr<MockMetrics> metrics_;
  scoped_ptr<StrictMock<MockMitigator> > mitigator_;
  MockPolicyServiceCompletion completion_;

  scoped_ptr<DevicePolicyService> service_;
};

TEST_F(DevicePolicyServiceTest, CheckAndHandleOwnerLogin_SuccessEmptyPolicy) {
  KeyCheckUtil nss;
  InitService(&nss);
  em::ChromeDeviceSettingsProto settings;
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  ExpectInstallNewOwnerPolicy(s, &nss);
  ExpectGetPolicy(s, new_policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_)).Times(0);
  ExpectKeyPopulated(true);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(1);

  PolicyService::Error error;
  bool is_owner = false;
  EXPECT_TRUE(service_->CheckAndHandleOwnerLogin(owner_,
                                                 nss.GetSlot(),
                                                 &is_owner,
                                                 &error));
  EXPECT_TRUE(is_owner);
  EXPECT_NO_FATAL_FAILURE(CheckNewOwnerSettings(settings));
}

TEST_F(DevicePolicyServiceTest, CheckAndHandleOwnerLogin_SuccessAddOwner) {
  KeyCheckUtil nss;
  InitService(&nss);
  em::ChromeDeviceSettingsProto settings;
  settings.mutable_user_whitelist()->add_user_whitelist("a@b");
  settings.mutable_user_whitelist()->add_user_whitelist("c@d");
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  ExpectInstallNewOwnerPolicy(s, &nss);
  ExpectGetPolicy(s, new_policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_)).Times(0);
  ExpectKeyPopulated(true);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(1);

  PolicyService::Error error;
  bool is_owner = false;
  EXPECT_TRUE(service_->CheckAndHandleOwnerLogin(owner_,
                                                 nss.GetSlot(),
                                                 &is_owner,
                                                 &error));
  EXPECT_TRUE(is_owner);
  EXPECT_NO_FATAL_FAILURE(CheckNewOwnerSettings(settings));
}

TEST_F(DevicePolicyServiceTest, CheckAndHandleOwnerLogin_SuccessOwnerPresent) {
  KeyCheckUtil nss;
  InitService(&nss);
  em::ChromeDeviceSettingsProto settings;
  settings.mutable_user_whitelist()->add_user_whitelist("a@b");
  settings.mutable_user_whitelist()->add_user_whitelist("c@d");
  settings.mutable_user_whitelist()->add_user_whitelist(owner_);
  settings.mutable_allow_new_users()->set_allow_new_users(true);
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  ExpectInstallNewOwnerPolicy(s, &nss);
  ExpectGetPolicy(s, new_policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_)).Times(0);
  ExpectKeyPopulated(true);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(1);

  PolicyService::Error error;
  bool is_owner = false;
  EXPECT_TRUE(service_->CheckAndHandleOwnerLogin(owner_,
                                                 nss.GetSlot(),
                                                 &is_owner,
                                                 &error));
  EXPECT_TRUE(is_owner);
  EXPECT_NO_FATAL_FAILURE(CheckNewOwnerSettings(settings));
}

TEST_F(DevicePolicyServiceTest, CheckAndHandleOwnerLogin_NotOwner) {
  KeyFailUtil nss;
  InitService(&nss);
  ASSERT_NO_FATAL_FAILURE(InitEmptyPolicy(owner_, fake_sig_, ""));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_)).Times(0);
  ExpectKeyPopulated(true);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(1);

  PolicyService::Error error;
  bool is_owner = true;
  EXPECT_TRUE(service_->CheckAndHandleOwnerLogin("regular_user@somewhere",
                                                 nss.GetSlot(),
                                                 &is_owner,
                                                 &error));
  EXPECT_FALSE(is_owner);
}

TEST_F(DevicePolicyServiceTest, CheckAndHandleOwnerLogin_EnterpriseDevice) {
  KeyFailUtil nss;
  InitService(&nss);
  ASSERT_NO_FATAL_FAILURE(InitEmptyPolicy(owner_, fake_sig_, "fake_token"));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_)).Times(0);
  ExpectKeyPopulated(true);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(0);

  PolicyService::Error error;
  bool is_owner = true;
  EXPECT_TRUE(service_->CheckAndHandleOwnerLogin(owner_,
                                                 nss.GetSlot(),
                                                 &is_owner,
                                                 &error));
  EXPECT_FALSE(is_owner);
}


TEST_F(DevicePolicyServiceTest, CheckAndHandleOwnerLogin_MissingKey) {
  KeyFailUtil nss;
  InitService(&nss);
  ASSERT_NO_FATAL_FAILURE(InitEmptyPolicy(owner_, fake_sig_, ""));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_))
      .InSequence(s)
      .WillOnce(Return(true));
  ExpectKeyPopulated(true);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(1);

  PolicyService::Error error;
  bool is_owner = false;
  EXPECT_TRUE(service_->CheckAndHandleOwnerLogin(owner_,
                                                 nss.GetSlot(),
                                                 &is_owner,
                                                 &error));
  EXPECT_TRUE(is_owner);
}

TEST_F(DevicePolicyServiceTest,
       CheckAndHandleOwnerLogin_MissingPublicKeyOwner) {
  KeyFailUtil nss;
  InitService(&nss);
  ASSERT_NO_FATAL_FAILURE(InitEmptyPolicy(owner_, fake_sig_, ""));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_))
      .InSequence(s)
      .WillOnce(Return(true));
  ExpectKeyPopulated(true);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(1);

  PolicyService::Error error;
  bool is_owner = false;
  EXPECT_TRUE(service_->CheckAndHandleOwnerLogin(owner_,
                                                 nss.GetSlot(),
                                                 &is_owner,
                                                 &error));
  EXPECT_TRUE(is_owner);
}

TEST_F(DevicePolicyServiceTest,
       CheckAndHandleOwnerLogin_MissingPublicKeyNonOwner) {
  KeyFailUtil nss;
  InitService(&nss);
  ASSERT_NO_FATAL_FAILURE(InitEmptyPolicy(owner_, fake_sig_, ""));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_)).Times(0);
  ExpectKeyPopulated(false);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(1);

  PolicyService::Error error;
  bool is_owner = true;
  EXPECT_TRUE(service_->CheckAndHandleOwnerLogin("other@somwhere",
                                                 nss.GetSlot(),
                                                 &is_owner,
                                                 &error));
  EXPECT_FALSE(is_owner);
}

TEST_F(DevicePolicyServiceTest, CheckAndHandleOwnerLogin_MitigationFailure) {
  KeyFailUtil nss;
  InitService(&nss);
  ASSERT_NO_FATAL_FAILURE(InitEmptyPolicy(owner_, fake_sig_, ""));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_))
      .InSequence(s)
      .WillOnce(Return(false));
  ExpectKeyPopulated(true);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(1);

  PolicyService::Error error;
  bool is_owner = false;
  EXPECT_FALSE(service_->CheckAndHandleOwnerLogin(owner_,
                                                  nss.GetSlot(),
                                                  &is_owner,
                                                  &error));
}

TEST_F(DevicePolicyServiceTest, CheckAndHandleOwnerLogin_SigningFailure) {
  KeyCheckUtil nss;
  InitService(&nss);
  em::ChromeDeviceSettingsProto settings;
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);

  EXPECT_CALL(*store_, Get())
      .WillRepeatedly(ReturnRef(policy_proto_));
  Expectation compare_keys =
      EXPECT_CALL(key_, Equals(_))
          .WillRepeatedly(Return(false));
  Expectation sign =
      EXPECT_CALL(nss, Sign(_, _, _, _))
          .After(compare_keys)
          .WillOnce(Return(false));

  ExpectGetPolicy(s, new_policy_proto_);
  EXPECT_CALL(*mitigator_.get(), Mitigate(_)).Times(0);
  ExpectKeyPopulated(true);
  EXPECT_CALL(*metrics_.get(), SendConsumerAllowsNewUsers(_)).Times(0);

  PolicyService::Error error;
  bool is_owner = false;
  EXPECT_FALSE(service_->CheckAndHandleOwnerLogin(owner_,
                                                  nss.GetSlot(),
                                                  &is_owner,
                                                  &error));
}

TEST_F(DevicePolicyServiceTest, PolicyAllowsNewUsers) {
  em::ChromeDeviceSettingsProto allowed;
  allowed.mutable_allow_new_users()->set_allow_new_users(true);
  EXPECT_TRUE(PolicyAllowsNewUsers(allowed));

  allowed.mutable_user_whitelist();
  EXPECT_TRUE(PolicyAllowsNewUsers(allowed));

  allowed.mutable_user_whitelist()->add_user_whitelist("a@b");
  EXPECT_TRUE(PolicyAllowsNewUsers(allowed));

  em::ChromeDeviceSettingsProto broken;
  broken.mutable_allow_new_users()->set_allow_new_users(false);
  EXPECT_TRUE(PolicyAllowsNewUsers(broken));

  em::ChromeDeviceSettingsProto disallowed = broken;
  disallowed.mutable_user_whitelist();
  disallowed.mutable_user_whitelist()->add_user_whitelist("a@b");
  EXPECT_FALSE(PolicyAllowsNewUsers(disallowed));

  em::ChromeDeviceSettingsProto not_disallowed;
  EXPECT_TRUE(PolicyAllowsNewUsers(not_disallowed));
  not_disallowed.mutable_user_whitelist();
  EXPECT_TRUE(PolicyAllowsNewUsers(not_disallowed));

  em::ChromeDeviceSettingsProto implicitly_disallowed = not_disallowed;
  implicitly_disallowed.mutable_user_whitelist()->add_user_whitelist("a@b");
  EXPECT_FALSE(PolicyAllowsNewUsers(implicitly_disallowed));
}

TEST_F(DevicePolicyServiceTest, ValidateAndStoreOwnerKey_SuccessNewKey) {
  KeyCheckUtil nss;
  InitService(&nss);

  ExpectMitigating(false);

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(key_, PopulateFromBuffer(fake_key_vector_))
      .InSequence(s)
      .WillOnce(Return(true));
  EXPECT_CALL(*store_, Set(PolicyEq(em::PolicyFetchResponse())));
  ExpectInstallNewOwnerPolicy(s, &nss);

  service_->ValidateAndStoreOwnerKey(owner_, fake_key_, nss.GetSlot());

  ExpectPersistKeyAndPolicy();
}

TEST_F(DevicePolicyServiceTest, ValidateAndStoreOwnerKey_SuccessMitigating) {
  KeyCheckUtil nss;
  InitService(&nss);

  ExpectMitigating(true);

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(key_, IsPopulated())
      .InSequence(s)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, ClobberCompromisedKey(fake_key_vector_))
      .InSequence(s)
      .WillOnce(Return(true));
  EXPECT_CALL(*store_, Set(_)).Times(0);
  ExpectInstallNewOwnerPolicy(s, &nss);

  service_->ValidateAndStoreOwnerKey(owner_, fake_key_, nss.GetSlot());

  ExpectPersistKeyAndPolicy();
}

TEST_F(DevicePolicyServiceTest, ValidateAndStoreOwnerKey_FailedMitigating) {
  KeyCheckUtil nss;
  InitService(&nss);

  ExpectMitigating(true);

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(key_, IsPopulated())
      .InSequence(s)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, ClobberCompromisedKey(fake_key_vector_))
      .InSequence(s)
      .WillOnce(Return(true));
  ExpectFailedInstallNewOwnerPolicy(s, &nss);

  service_->ValidateAndStoreOwnerKey(owner_, fake_key_, nss.GetSlot());

  ExpectNoPersistKeyAndPolicy();
}

TEST_F(DevicePolicyServiceTest, ValidateAndStoreOwnerKey_SuccessAddOwner) {
  KeyCheckUtil nss;
  InitService(&nss);
  em::ChromeDeviceSettingsProto settings;
  settings.mutable_user_whitelist()->add_user_whitelist("a@b");
  settings.mutable_user_whitelist()->add_user_whitelist("c@d");
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));

  ExpectMitigating(false);

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(key_, PopulateFromBuffer(fake_key_vector_))
      .InSequence(s)
      .WillOnce(Return(true));
  EXPECT_CALL(*store_, Set(PolicyEq(em::PolicyFetchResponse())));
  ExpectInstallNewOwnerPolicy(s, &nss);

  service_->ValidateAndStoreOwnerKey(owner_, fake_key_, nss.GetSlot());

  ExpectPersistKeyAndPolicy();
}

TEST_F(DevicePolicyServiceTest, ValidateAndStoreOwnerKey_NoPrivateKey) {
  KeyFailUtil nss;
  InitService(&nss);

  service_->ValidateAndStoreOwnerKey(owner_, fake_key_, nss.GetSlot());
}

TEST_F(DevicePolicyServiceTest, ValidateAndStoreOwnerKey_NewKeyInstallFails) {
  KeyCheckUtil nss;
  InitService(&nss);

  ExpectMitigating(false);

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(key_, PopulateFromBuffer(fake_key_vector_))
      .InSequence(s)
      .WillOnce(Return(false));

  service_->ValidateAndStoreOwnerKey(owner_, fake_key_, nss.GetSlot());
}

TEST_F(DevicePolicyServiceTest, ValidateAndStoreOwnerKey_KeyClobberFails) {
  KeyCheckUtil nss;
  InitService(&nss);

  ExpectMitigating(true);

  Sequence s;
  ExpectGetPolicy(s, policy_proto_);
  EXPECT_CALL(key_, IsPopulated())
      .InSequence(s)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, ClobberCompromisedKey(fake_key_vector_))
      .InSequence(s)
      .WillOnce(Return(false));

  service_->ValidateAndStoreOwnerKey(owner_, fake_key_, nss.GetSlot());
}

TEST_F(DevicePolicyServiceTest, KeyMissing_Present) {
  MockNssUtil nss;
  InitService(&nss);

  ExpectKeyPopulated(true);

  EXPECT_FALSE(service_->KeyMissing());
}

TEST_F(DevicePolicyServiceTest, KeyMissing_NoDiskCheck) {
  MockNssUtil nss;
  InitService(&nss);

  EXPECT_CALL(key_, HaveCheckedDisk())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(key_, IsPopulated())
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(service_->KeyMissing());
}

TEST_F(DevicePolicyServiceTest, KeyMissing_CheckedAndMissing) {
  MockNssUtil nss;
  InitService(&nss);

  ExpectKeyPopulated(false);

  EXPECT_TRUE(service_->KeyMissing());
}

TEST_F(DevicePolicyServiceTest, Metrics_NoKeyNoPolicyNoPrefs) {
  MockNssUtil nss;
  InitService(&nss);

  LoginMetrics::PolicyFilesStatus status;
  status.owner_key_file_state = SimulateNullOwnerKey();
  status.policy_file_state = SimulateNullPolicy();
  status.defunct_prefs_file_state = SimulateNullPrefs();

  EXPECT_CALL(*metrics_.get(), SendPolicyFilesStatus(StatusEq(status)))
      .Times(1);
  service_->ReportPolicyFileMetrics(true, true);
}

TEST_F(DevicePolicyServiceTest, Metrics_UnloadableKeyNoPolicyNoPrefs) {
  MockNssUtil nss;
  InitService(&nss);

  LoginMetrics::PolicyFilesStatus status;
  status.owner_key_file_state = LoginMetrics::MALFORMED;
  status.policy_file_state = SimulateNullPolicy();
  status.defunct_prefs_file_state = SimulateNullPrefs();

  EXPECT_CALL(*metrics_.get(), SendPolicyFilesStatus(StatusEq(status)))
      .Times(1);
  service_->ReportPolicyFileMetrics(false, true);
}

TEST_F(DevicePolicyServiceTest, Metrics_BadKeyNoPolicyNoPrefs) {
  MockNssUtil nss;
  InitService(&nss);

  LoginMetrics::PolicyFilesStatus status;
  status.owner_key_file_state = SimulateBadOwnerKey(&nss);
  status.policy_file_state = SimulateNullPolicy();
  status.defunct_prefs_file_state = SimulateNullPrefs();

  EXPECT_CALL(*metrics_.get(), SendPolicyFilesStatus(StatusEq(status)))
      .Times(1);
  service_->ReportPolicyFileMetrics(true, true);
}

TEST_F(DevicePolicyServiceTest, Metrics_GoodKeyNoPolicyNoPrefs) {
  MockNssUtil nss;
  InitService(&nss);

  LoginMetrics::PolicyFilesStatus status;
  status.owner_key_file_state = SimulateGoodOwnerKey(&nss);
  status.policy_file_state = SimulateNullPolicy();
  status.defunct_prefs_file_state = SimulateNullPrefs();

  EXPECT_CALL(*metrics_.get(), SendPolicyFilesStatus(StatusEq(status)))
      .Times(1);
  service_->ReportPolicyFileMetrics(true, true);
}

TEST_F(DevicePolicyServiceTest, Metrics_GoodKeyUnloadablePolicyNoPrefs) {
  MockNssUtil nss;
  InitService(&nss);

  LoginMetrics::PolicyFilesStatus status;
  status.owner_key_file_state = SimulateGoodOwnerKey(&nss);
  status.policy_file_state = LoginMetrics::MALFORMED;
  status.defunct_prefs_file_state = SimulateNullPrefs();

  EXPECT_CALL(*metrics_.get(), SendPolicyFilesStatus(StatusEq(status)))
      .Times(1);
  service_->ReportPolicyFileMetrics(true, false);
}

TEST_F(DevicePolicyServiceTest, Metrics_GoodKeyGoodPolicyNoPrefs) {
  MockNssUtil nss;
  InitService(&nss);

  LoginMetrics::PolicyFilesStatus status;
  status.owner_key_file_state = SimulateGoodOwnerKey(&nss);
  status.policy_file_state = SimulateGoodPolicy();
  status.defunct_prefs_file_state = SimulateNullPrefs();

  EXPECT_CALL(*metrics_.get(), SendPolicyFilesStatus(StatusEq(status)))
      .Times(1);
  service_->ReportPolicyFileMetrics(true, true);
}

TEST_F(DevicePolicyServiceTest, Metrics_GoodKeyNoPolicyExtantPrefs) {
  // This is http://crosbug.com/24361
  MockNssUtil nss;
  InitService(&nss);

  LoginMetrics::PolicyFilesStatus status;
  status.owner_key_file_state = SimulateGoodOwnerKey(&nss);
  status.policy_file_state = SimulateNullPolicy();
  status.defunct_prefs_file_state = SimulateExtantPrefs();

  EXPECT_CALL(*metrics_.get(), SendPolicyFilesStatus(StatusEq(status)))
      .Times(1);
  service_->ReportPolicyFileMetrics(true, true);
}

TEST_F(DevicePolicyServiceTest, SerialRecoveryFlagFileInitialization) {
  MockNssUtil nss;
  InitService(&nss);

  EXPECT_CALL(nss, CheckPublicKeyBlob(fake_key_vector_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, PopulateFromDiskIfPossible())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, HaveCheckedDisk())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, IsPopulated())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, LoadOrCreate())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, Get())
      .WillRepeatedly(ReturnRef(policy_proto_));
  EXPECT_CALL(*store_, DefunctPrefsFilePresent())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*metrics_.get(), SendPolicyFilesStatus(_))
      .Times(AnyNumber());

  // Fake the policy file existence.
  base::WriteFile(policy_file_, ".", 1);

  em::ChromeDeviceSettingsProto settings;
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "t", true));
  EXPECT_TRUE(service_->Initialize());
  EXPECT_TRUE(base::PathExists(serial_recovery_flag_file_));

  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));
  EXPECT_TRUE(service_->Initialize());
  EXPECT_FALSE(base::PathExists(serial_recovery_flag_file_));

  // Fake the policy file gone.
  base::DeleteFile(policy_file_, false);
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));
  EXPECT_TRUE(service_->Initialize());
  EXPECT_TRUE(base::PathExists(serial_recovery_flag_file_));
}

TEST_F(DevicePolicyServiceTest, RecoverOwnerKeyFromPolicy) {
  MockNssUtil nss;
  InitService(&nss);

  EXPECT_CALL(nss, CheckPublicKeyBlob(fake_key_vector_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, PopulateFromDiskIfPossible())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(key_, PopulateFromBuffer(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, ClobberCompromisedKey(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, IsPopulated())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(key_, Persist())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, LoadOrCreate())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, Get())
      .WillRepeatedly(ReturnRef(policy_proto_));
  EXPECT_CALL(*store_, DefunctPrefsFilePresent())
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*metrics_.get(), SendPolicyFilesStatus(_))
      .Times(AnyNumber());

  em::ChromeDeviceSettingsProto settings;
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));
  EXPECT_FALSE(service_->Initialize());

  policy_proto_.set_new_public_key(fake_key_);
  EXPECT_TRUE(service_->Initialize());
}

TEST_F(DevicePolicyServiceTest, SerialRecoveryFlagFileUpdating) {
  // Fake the policy file existence.
  base::WriteFile(policy_file_, ".", 1);

  MockNssUtil nss;
  InitService(&nss);
  em::ChromeDeviceSettingsProto settings;
  EXPECT_FALSE(base::PathExists(serial_recovery_flag_file_));

  EXPECT_CALL(key_, Verify(_, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, Set(_)).Times(AnyNumber());
  EXPECT_CALL(*store_, Get())
      .WillRepeatedly(ReturnRef(policy_proto_));
  EXPECT_CALL(completion_, ReportSuccess()).Times(AnyNumber());
  EXPECT_CALL(completion_, ReportFailure(_)).Times(AnyNumber());

  // Installing a policy blob that doesn't have a request token (indicates local
  // owner) should not create the file.
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", true));
  EXPECT_TRUE(
      service_->Store(reinterpret_cast<const uint8*>(policy_str_.c_str()),
                      policy_str_.size(), &completion_,
                      PolicyService::KEY_CLOBBER));
  EXPECT_FALSE(base::PathExists(serial_recovery_flag_file_));

  // Storing an enterprise policy blob with the |valid_serial_number_missing|
  // flag set should create the flag file.
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "t", true));
  EXPECT_TRUE(
      service_->Store(reinterpret_cast<const uint8*>(policy_str_.c_str()),
                      policy_str_.size(), &completion_,
                      PolicyService::KEY_CLOBBER));
  EXPECT_TRUE(base::PathExists(serial_recovery_flag_file_));

  // Storing bad policy shouldn't remove the file.
  EXPECT_FALSE(service_->Store(NULL, 0, &completion_,
                               PolicyService::KEY_CLOBBER));
  EXPECT_TRUE(base::PathExists(serial_recovery_flag_file_));

  // Clearing the flag should remove the file.
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "t", false));
  EXPECT_TRUE(
      service_->Store(reinterpret_cast<const uint8*>(policy_str_.c_str()),
                      policy_str_.size(), &completion_,
                      PolicyService::KEY_CLOBBER));
  EXPECT_FALSE(base::PathExists(serial_recovery_flag_file_));


  // Create install attributes file to mock enterprise enrolled device.
  cryptohome::SerializedInstallAttributes install_attributes;
  cryptohome::SerializedInstallAttributes_Attribute *attribute =
      install_attributes.add_attributes();
  attribute->set_name(DevicePolicyService::kAttrEnterpriseMode);
  attribute->set_value(DevicePolicyService::kEnterpriseDeviceMode);
  std::string serialized;
  EXPECT_TRUE(install_attributes.SerializeToString(&serialized));
  base::WriteFile(install_attributes_file_,
                  serialized.c_str(), serialized.size());

  // In case DM tokens exists, the flag file should not be created.
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "t", false));
  EXPECT_TRUE(
      service_->Store(reinterpret_cast<const uint8*>(policy_str_.c_str()),
                      policy_str_.size(), &completion_,
                      PolicyService::KEY_CLOBBER));
  EXPECT_FALSE(base::PathExists(serial_recovery_flag_file_));

  // Missing DM token should lead to creation of flag file.
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));
  EXPECT_TRUE(
      service_->Store(reinterpret_cast<const uint8*>(policy_str_.c_str()),
                      policy_str_.size(), &completion_,
                      PolicyService::KEY_CLOBBER));
  EXPECT_TRUE(base::PathExists(serial_recovery_flag_file_));
}

TEST_F(DevicePolicyServiceTest, GetSettings) {
  MockNssUtil nss;
  InitService(&nss);

  // No policy blob should result in an empty settings protobuf.
  em::ChromeDeviceSettingsProto settings;
  EXPECT_CALL(*store_, Get()).WillRepeatedly(ReturnRef(policy_proto_));
  EXPECT_EQ(service_->GetSettings().SerializeAsString(),
            settings.SerializeAsString());
  Mock::VerifyAndClearExpectations(store_);

  // Storing new policy should cause the settings to update as well.
  settings.mutable_metrics_enabled()->set_metrics_enabled(true);
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "t", true));
  EXPECT_CALL(key_, Verify(_, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, Persist())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, Set(_)).Times(AnyNumber());
  EXPECT_CALL(*store_, Get())
      .WillRepeatedly(ReturnRef(policy_proto_));
  EXPECT_CALL(completion_, ReportSuccess()).Times(AnyNumber());
  EXPECT_CALL(completion_, ReportFailure(_)).Times(AnyNumber());
  EXPECT_TRUE(
      service_->Store(reinterpret_cast<const uint8*>(policy_str_.c_str()),
                      policy_str_.size(), &completion_,
                      PolicyService::KEY_CLOBBER));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(service_->GetSettings().SerializeAsString(),
            settings.SerializeAsString());
}

TEST_F(DevicePolicyServiceTest, StartUpFlagsSanitizer) {
  MockNssUtil nss;
  InitService(&nss);

  em::ChromeDeviceSettingsProto settings;
  // Some valid flags.
  settings.mutable_start_up_flags()->add_flags("a");
  settings.mutable_start_up_flags()->add_flags("bb");
  settings.mutable_start_up_flags()->add_flags("-c");
  settings.mutable_start_up_flags()->add_flags("--d");
  // Some invalid ones.
  settings.mutable_start_up_flags()->add_flags("");
  settings.mutable_start_up_flags()->add_flags("-");
  settings.mutable_start_up_flags()->add_flags("--");
  ASSERT_NO_FATAL_FAILURE(InitPolicy(settings, owner_, fake_sig_, "", false));
  EXPECT_CALL(key_, Verify(_, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, Persist())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*store_, Set(_)).Times(AnyNumber());
  EXPECT_CALL(*store_, Get())
      .WillRepeatedly(ReturnRef(policy_proto_));
  EXPECT_CALL(completion_, ReportSuccess()).Times(AnyNumber());
  EXPECT_CALL(completion_, ReportFailure(_)).Times(AnyNumber());
  EXPECT_TRUE(
      service_->Store(reinterpret_cast<const uint8*>(policy_str_.c_str()),
                      policy_str_.size(), &completion_,
                      PolicyService::KEY_CLOBBER));
  base::RunLoop().RunUntilIdle();

  std::vector<std::string> flags = service_->GetStartUpFlags();
  EXPECT_EQ(6, flags.size());
  EXPECT_EQ("--policy-switches-begin", flags[0]);
  EXPECT_EQ("--a", flags[1]);
  EXPECT_EQ("--bb", flags[2]);
  EXPECT_EQ("-c", flags[3]);
  EXPECT_EQ("--d", flags[4]);
  EXPECT_EQ("--policy-switches-end", flags[5]);
}

}  // namespace login_manager
