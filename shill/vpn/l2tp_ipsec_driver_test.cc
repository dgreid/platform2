// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/l2tp_ipsec_driver.h"

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ptr_util.h>
#include <base/memory/weak_ptr.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>
#include <vpn-manager/service_error.h>

#include "shill/ipconfig.h"
#include "shill/mock_adaptors.h"
#include "shill/mock_certificate_file.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_external_task.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_ppp_device.h"
#include "shill/mock_ppp_device_factory.h"
#include "shill/mock_process_manager.h"
#include "shill/test_event_dispatcher.h"
#include "shill/vpn/mock_vpn_provider.h"
#include "shill/vpn/mock_vpn_service.h"

using base::FilePath;
using std::map;
using std::string;
using std::vector;
using testing::_;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace shill {

class L2TPIPSecDriverTest : public testing::Test, public RpcTaskDelegate {
 public:
  L2TPIPSecDriverTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        device_info_(&manager_),
        driver_(new L2TPIPSecDriver(&manager_, &process_manager_)),
        service_(new MockVPNService(&manager_, base::WrapUnique(driver_))),
        device_(new MockPPPDevice(&manager_, kInterfaceName, kInterfaceIndex)),
        certificate_file_(new MockCertificateFile()),
        weak_ptr_factory_(this) {
    manager_.set_mock_device_info(&device_info_);
    driver_->certificate_file_.reset(certificate_file_);  // Passes ownership.
  }

  ~L2TPIPSecDriverTest() override = default;

  void SetUp() override {
    manager_.vpn_provider_ = std::make_unique<MockVPNProvider>();
    manager_.vpn_provider_->manager_ = &manager_;
    manager_.user_traffic_uids_.push_back(1000);
    manager_.UpdateProviderMapping();

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  void TearDown() override {
    SetService(nullptr);
    ASSERT_TRUE(temp_dir_.Delete());

    // The ExternalTask instance initially held by |driver_->external_task_|
    // could be scheduled to be destroyed after |driver_| is destroyed. To
    // avoid leaking any ExternalTask instance when the test finishes, we
    // explicitly destroy |service_| here, which indirectly destroys |driver_|
    // and in turn schedules the destruction of |driver_->external_task_| in
    // the message loop. Then we run until the message loop becomes idle to
    // exercise the destruction task of ExternalTask.
    service_ = nullptr;
    dispatcher_.PostTask(
        FROM_HERE,
        base::Bind(&EventDispatcherForTest::QuitDispatchForever,
                   // dispatcher_ will not be deleted before RunLoop quits.
                   base::Unretained(&dispatcher_)));
    dispatcher_.DispatchForever();
  }

 protected:
  static const char kInterfaceName[];
  static const int kInterfaceIndex;

  void SetArg(const string& arg, const string& value) {
    driver_->args()->Set<string>(arg, value);
  }

  void SetArgArray(const string& arg, const vector<string>& value) {
    driver_->args()->Set<Strings>(arg, value);
  }

  KeyValueStore* GetArgs() { return driver_->args(); }

  string GetProviderType() { return driver_->GetProviderType(); }

  void SetService(const scoped_refptr<MockVPNService>& service) {
    if (service) {
      driver_->service_callback_ = service->GetCallback();
    } else {
      driver_->service_callback_.Reset();
    }
  }

  void OnConnectTimeout() { driver_->OnConnectTimeout(); }

  void StartConnectTimeout(int timeout_seconds) {
    driver_->StartConnectTimeout(timeout_seconds);
  }

  bool IsConnectTimeoutStarted() const {
    return driver_->IsConnectTimeoutStarted();
  }

  bool IsPSKFileCleared(const FilePath& psk_file_path) const {
    return !base::PathExists(psk_file_path) && GetPSKFile().empty();
  }

  bool IsXauthCredentialsFileCleared(
      const FilePath& xauth_credentials_file_path) const {
    return !base::PathExists(xauth_credentials_file_path) &&
           GetXauthCredentialsFile().empty();
  }

  // Used to assert that a flag appears in the options.
  void ExpectInFlags(const vector<string>& options,
                     const string& flag,
                     const string& value);

  FilePath SetupPSKFile();
  FilePath SetupXauthCredentialsFile();

  FilePath GetPSKFile() const { return driver_->psk_file_; }
  FilePath GetXauthCredentialsFile() const {
    return driver_->xauth_credentials_file_;
  }

  void InvokeNotify(const string& reason, const map<string, string>& dict) {
    driver_->Notify(reason, dict);
  }

  void FakeUpConnect(FilePath* psk_file, FilePath* xauth_credentials_file) {
    *psk_file = SetupPSKFile();
    *xauth_credentials_file = SetupXauthCredentialsFile();
    SetService(service_);
    StartConnectTimeout(0);
  }

  void ExpectMetricsReported() {
    Error unused_error;
    PropertyStore store;
    driver_->InitPropertyStore(&store);
    store.SetStringProperty(kL2tpIpsecPskProperty, "x", &unused_error);
    store.SetStringProperty(kL2tpIpsecPasswordProperty, "y", &unused_error);
    EXPECT_CALL(metrics_, SendEnumToUMA(Metrics::kMetricVpnDriver,
                                        Metrics::kVpnDriverL2tpIpsec,
                                        Metrics::kMetricVpnDriverMax));
    EXPECT_CALL(metrics_,
                SendEnumToUMA(Metrics::kMetricVpnRemoteAuthenticationType,
                              Metrics::kVpnRemoteAuthenticationTypeL2tpIpsecPsk,
                              Metrics::kVpnRemoteAuthenticationTypeMax));
    EXPECT_CALL(
        metrics_,
        SendEnumToUMA(
            Metrics::kMetricVpnUserAuthenticationType,
            Metrics::kVpnUserAuthenticationTypeL2tpIpsecUsernamePassword,
            Metrics::kVpnUserAuthenticationTypeMax));
  }

  // Inherited from RpcTaskDelegate.
  void GetLogin(string* user, string* password) override;
  void Notify(const string& reason, const map<string, string>& dict) override;

  base::ScopedTempDir temp_dir_;
  MockControl control_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockProcessManager process_manager_;
  MockManager manager_;
  NiceMock<MockDeviceInfo> device_info_;
  L2TPIPSecDriver* driver_;  // Owned by |service_|.
  scoped_refptr<MockVPNService> service_;
  scoped_refptr<MockPPPDevice> device_;
  MockCertificateFile* certificate_file_;  // Owned by |driver_|.
  base::WeakPtrFactory<L2TPIPSecDriverTest> weak_ptr_factory_;
};

const char L2TPIPSecDriverTest::kInterfaceName[] = "ppp0";
const int L2TPIPSecDriverTest::kInterfaceIndex = 123;

void L2TPIPSecDriverTest::GetLogin(string* /*user*/, string* /*password*/) {}

void L2TPIPSecDriverTest::Notify(const string& /*reason*/,
                                 const map<string, string>& /*dict*/) {}

void L2TPIPSecDriverTest::ExpectInFlags(const vector<string>& options,
                                        const string& flag,
                                        const string& value) {
  string flag_value = base::StringPrintf("%s=%s", flag.c_str(), value.c_str());
  EXPECT_TRUE(base::Contains(options, flag_value));
}

FilePath L2TPIPSecDriverTest::SetupPSKFile() {
  FilePath psk_file;
  EXPECT_TRUE(base::CreateTemporaryFileInDir(temp_dir_.GetPath(), &psk_file));
  EXPECT_FALSE(psk_file.empty());
  EXPECT_TRUE(base::PathExists(psk_file));
  driver_->psk_file_ = psk_file;
  return psk_file;
}

FilePath L2TPIPSecDriverTest::SetupXauthCredentialsFile() {
  FilePath xauth_credentials_file;
  EXPECT_TRUE(base::CreateTemporaryFileInDir(temp_dir_.GetPath(),
                                             &xauth_credentials_file));
  EXPECT_FALSE(xauth_credentials_file.empty());
  EXPECT_TRUE(base::PathExists(xauth_credentials_file));
  driver_->xauth_credentials_file_ = xauth_credentials_file;
  return xauth_credentials_file;
}

TEST_F(L2TPIPSecDriverTest, GetProviderType) {
  EXPECT_EQ(kProviderL2tpIpsec, GetProviderType());
}

TEST_F(L2TPIPSecDriverTest, Cleanup) {
  driver_->Cleanup();  // Ensure no crash.

  FilePath psk_file;
  FilePath xauth_credentials_file;
  FakeUpConnect(&psk_file, &xauth_credentials_file);
  driver_->external_task_.reset(new MockExternalTask(
      &control_, &process_manager_, weak_ptr_factory_.GetWeakPtr(),
      base::Callback<void(pid_t, int)>()));
  SetService(service_);
  EXPECT_CALL(*service_, OnDriverEvent(VPNService::kEventDriverFailure,
                                       Service::kFailureBadPassphrase, _));
  driver_->FailService(Service::kFailureBadPassphrase);  // Trigger Cleanup.
  EXPECT_TRUE(IsPSKFileCleared(psk_file));
  EXPECT_TRUE(IsXauthCredentialsFileCleared(xauth_credentials_file));
  EXPECT_FALSE(driver_->service_callback_);
  EXPECT_FALSE(driver_->IsConnectTimeoutStarted());
  EXPECT_FALSE(driver_->external_task_);

  SetService(service_);
  driver_->Disconnect();
  EXPECT_FALSE(driver_->service_callback_);
}

TEST_F(L2TPIPSecDriverTest, DeleteTemporaryFiles) {
  FilePath psk_file = SetupPSKFile();
  FilePath xauth_credentials_file = SetupXauthCredentialsFile();
  driver_->DeleteTemporaryFiles();
  EXPECT_TRUE(IsPSKFileCleared(psk_file));
  EXPECT_TRUE(IsXauthCredentialsFileCleared(xauth_credentials_file));
}

TEST_F(L2TPIPSecDriverTest, InitOptionsNoHost) {
  Error error;
  vector<string> options;
  EXPECT_FALSE(driver_->InitOptions(&options, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_TRUE(options.empty());
}

TEST_F(L2TPIPSecDriverTest, InitOptions) {
  static const char kHost[] = "192.168.2.254";
  static const char kPSK[] = "foobar";
  static const char kXauthUser[] = "silly";
  static const char kXauthPassword[] = "rabbit";
  const vector<string> kCaCertPEM{"Insert PEM encoded data here"};
  static const char kPEMCertfile[] = "/tmp/der-file-from-pem-cert";
  FilePath pem_cert(kPEMCertfile);

  SetArg(kProviderHostProperty, kHost);
  SetArg(kL2tpIpsecPskProperty, kPSK);
  SetArg(kL2tpIpsecXauthUserProperty, kXauthUser);
  SetArg(kL2tpIpsecXauthPasswordProperty, kXauthPassword);
  SetArgArray(kL2tpIpsecCaCertPemProperty, kCaCertPEM);

  EXPECT_CALL(*certificate_file_, CreatePEMFromStrings(kCaCertPEM))
      .WillOnce(Return(pem_cert));
  const FilePath temp_dir(temp_dir_.GetPath());
  // Once each for PSK and Xauth options.
  EXPECT_CALL(manager_, run_path())
      .WillOnce(ReturnRef(temp_dir))
      .WillOnce(ReturnRef(temp_dir));

  Error error;
  vector<string> options;
  EXPECT_TRUE(driver_->InitOptions(&options, &error));
  EXPECT_TRUE(error.IsSuccess());

  ExpectInFlags(options, "--remote_host", kHost);
  ASSERT_FALSE(driver_->psk_file_.empty());
  ExpectInFlags(options, "--psk_file", driver_->psk_file_.value());
  ASSERT_FALSE(driver_->xauth_credentials_file_.empty());
  ExpectInFlags(options, "--xauth_credentials_file",
                driver_->xauth_credentials_file_.value());
  ExpectInFlags(options, "--server_ca_file", kPEMCertfile);
}

TEST_F(L2TPIPSecDriverTest, InitPSKOptions) {
  Error error;
  vector<string> options;
  static const char kPSK[] = "foobar";
  const FilePath bad_dir("/non/existent/directory");
  const FilePath temp_dir(temp_dir_.GetPath());
  EXPECT_CALL(manager_, run_path())
      .WillOnce(ReturnRef(bad_dir))
      .WillOnce(ReturnRef(temp_dir));

  EXPECT_TRUE(driver_->InitPSKOptions(&options, &error));
  EXPECT_TRUE(options.empty());
  EXPECT_TRUE(error.IsSuccess());

  SetArg(kL2tpIpsecPskProperty, kPSK);

  EXPECT_FALSE(driver_->InitPSKOptions(&options, &error));
  EXPECT_TRUE(options.empty());
  EXPECT_EQ(Error::kInternalError, error.type());
  error.Reset();

  EXPECT_TRUE(driver_->InitPSKOptions(&options, &error));
  ASSERT_FALSE(driver_->psk_file_.empty());
  ExpectInFlags(options, "--psk_file", driver_->psk_file_.value());
  EXPECT_TRUE(error.IsSuccess());
  string contents;
  EXPECT_TRUE(base::ReadFileToString(driver_->psk_file_, &contents));
  EXPECT_EQ(kPSK, contents);
  struct stat buf;
  ASSERT_EQ(0, stat(driver_->psk_file_.value().c_str(), &buf));
  EXPECT_EQ(S_IFREG | S_IRUSR | S_IWUSR, buf.st_mode);
}

TEST_F(L2TPIPSecDriverTest, InitPEMOptions) {
  const vector<string> kCaCertPEM{"Insert PEM encoded data here"};
  static const char kPEMCertfile[] = "/tmp/der-file-from-pem-cert";
  FilePath empty_cert;
  FilePath pem_cert(kPEMCertfile);
  SetArgArray(kL2tpIpsecCaCertPemProperty, kCaCertPEM);
  EXPECT_CALL(*certificate_file_, CreatePEMFromStrings(kCaCertPEM))
      .WillOnce(Return(empty_cert))
      .WillOnce(Return(pem_cert));

  vector<string> options;
  driver_->InitPEMOptions(&options);
  EXPECT_TRUE(options.empty());
  driver_->InitPEMOptions(&options);
  ExpectInFlags(options, "--server_ca_file", kPEMCertfile);
}

TEST_F(L2TPIPSecDriverTest, InitXauthOptions) {
  vector<string> options;
  EXPECT_CALL(manager_, run_path()).Times(0);
  {
    Error error;
    EXPECT_TRUE(driver_->InitXauthOptions(&options, &error));
    EXPECT_TRUE(error.IsSuccess());
  }
  EXPECT_TRUE(options.empty());

  static const char kUser[] = "foobar";
  SetArg(kL2tpIpsecXauthUserProperty, kUser);
  {
    Error error;
    EXPECT_FALSE(driver_->InitXauthOptions(&options, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  EXPECT_TRUE(options.empty());

  static const char kPassword[] = "foobar";
  SetArg(kL2tpIpsecXauthUserProperty, "");
  SetArg(kL2tpIpsecXauthPasswordProperty, kPassword);
  {
    Error error;
    EXPECT_FALSE(driver_->InitXauthOptions(&options, &error));
    EXPECT_EQ(Error::kInvalidArguments, error.type());
  }
  EXPECT_TRUE(options.empty());
  Mock::VerifyAndClearExpectations(&manager_);

  SetArg(kL2tpIpsecXauthUserProperty, kUser);
  const FilePath bad_dir("/non/existent/directory");
  const FilePath temp_dir(temp_dir_.GetPath());
  EXPECT_CALL(manager_, run_path())
      .WillOnce(ReturnRef(bad_dir))
      .WillOnce(ReturnRef(temp_dir));

  {
    Error error;
    EXPECT_FALSE(driver_->InitXauthOptions(&options, &error));
    EXPECT_EQ(Error::kInternalError, error.type());
  }
  EXPECT_TRUE(options.empty());

  {
    Error error;
    EXPECT_TRUE(driver_->InitXauthOptions(&options, &error));
    EXPECT_TRUE(error.IsSuccess());
  }
  ASSERT_FALSE(driver_->xauth_credentials_file_.empty());
  ExpectInFlags(options, "--xauth_credentials_file",
                driver_->xauth_credentials_file_.value());
  string contents;
  EXPECT_TRUE(
      base::ReadFileToString(driver_->xauth_credentials_file_, &contents));
  string expected_contents(string(kUser) + "\n" + kPassword + "\n");
  EXPECT_EQ(expected_contents, contents);
  struct stat buf;
  ASSERT_EQ(0, stat(driver_->xauth_credentials_file_.value().c_str(), &buf));
  EXPECT_EQ(S_IFREG | S_IRUSR | S_IWUSR, buf.st_mode);
}

TEST_F(L2TPIPSecDriverTest, AppendValueOption) {
  static const char kOption[] = "--l2tpipsec-option";
  static const char kProperty[] = "L2TPIPSec.SomeProperty";
  static const char kValue[] = "some-property-value";
  static const char kOption2[] = "--l2tpipsec-option2";
  static const char kProperty2[] = "L2TPIPSec.SomeProperty2";
  static const char kValue2[] = "some-property-value2";

  vector<string> options;
  EXPECT_FALSE(driver_->AppendValueOption("L2TPIPSec.UnknownProperty", kOption,
                                          &options));
  EXPECT_TRUE(options.empty());

  SetArg(kProperty, "");
  EXPECT_FALSE(driver_->AppendValueOption(kProperty, kOption, &options));
  EXPECT_TRUE(options.empty());

  SetArg(kProperty, kValue);
  SetArg(kProperty2, kValue2);
  EXPECT_TRUE(driver_->AppendValueOption(kProperty, kOption, &options));
  EXPECT_TRUE(driver_->AppendValueOption(kProperty2, kOption2, &options));
  EXPECT_EQ(2, options.size());
  EXPECT_EQ(base::StringPrintf("%s=%s", kOption, kValue), options[0]);
  EXPECT_EQ(base::StringPrintf("%s=%s", kOption2, kValue2), options[1]);
}

TEST_F(L2TPIPSecDriverTest, AppendFlag) {
  static const char kTrueOption[] = "--l2tpipsec-option";
  static const char kFalseOption[] = "--nol2tpipsec-option";
  static const char kProperty[] = "L2TPIPSec.SomeProperty";
  static const char kTrueOption2[] = "--l2tpipsec-option2";
  static const char kFalseOption2[] = "--nol2tpipsec-option2";
  static const char kProperty2[] = "L2TPIPSec.SomeProperty2";

  vector<string> options;
  EXPECT_FALSE(driver_->AppendFlag("L2TPIPSec.UnknownProperty", kTrueOption,
                                   kFalseOption, &options));
  EXPECT_TRUE(options.empty());

  SetArg(kProperty, "");
  EXPECT_FALSE(
      driver_->AppendFlag(kProperty, kTrueOption, kFalseOption, &options));
  EXPECT_TRUE(options.empty());

  SetArg(kProperty, "true");
  SetArg(kProperty2, "false");
  EXPECT_TRUE(
      driver_->AppendFlag(kProperty, kTrueOption, kFalseOption, &options));
  EXPECT_TRUE(
      driver_->AppendFlag(kProperty2, kTrueOption2, kFalseOption2, &options));
  EXPECT_EQ(2, options.size());
  EXPECT_EQ(kTrueOption, options[0]);
  EXPECT_EQ(kFalseOption2, options[1]);
}

TEST_F(L2TPIPSecDriverTest, GetLogin) {
  static const char kUser[] = "joesmith";
  static const char kPassword[] = "random-password";
  string user, password;
  SetArg(kL2tpIpsecUserProperty, kUser);
  driver_->GetLogin(&user, &password);
  EXPECT_TRUE(user.empty());
  EXPECT_TRUE(password.empty());
  SetArg(kL2tpIpsecUserProperty, "");
  SetArg(kL2tpIpsecPasswordProperty, kPassword);
  driver_->GetLogin(&user, &password);
  EXPECT_TRUE(user.empty());
  EXPECT_TRUE(password.empty());
  SetArg(kL2tpIpsecUserProperty, kUser);
  driver_->GetLogin(&user, &password);
  EXPECT_EQ(kUser, user);
  EXPECT_EQ(kPassword, password);
}

TEST_F(L2TPIPSecDriverTest, OnL2TPIPSecVPNDied) {
  const int kPID = 123456;
  SetService(service_);
  EXPECT_CALL(*service_, OnDriverEvent(VPNService::kEventDriverFailure,
                                       Service::kFailureDNSLookup, _));
  driver_->OnL2TPIPSecVPNDied(kPID,
                              vpn_manager::kServiceErrorResolveHostnameFailed);
  EXPECT_FALSE(driver_->service_callback_);
}

TEST_F(L2TPIPSecDriverTest, SpawnL2TPIPSecVPN) {
  Error error;
  // Fail without sufficient arguments.
  EXPECT_FALSE(driver_->SpawnL2TPIPSecVPN(&error));
  EXPECT_TRUE(error.IsFailure());

  // Provide the required arguments.
  static const char kHost[] = "192.168.2.254";
  SetArg(kProviderHostProperty, kHost);

  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, _, _, _, _, _, _, _, true, _))
      .WillOnce(Return(-1))
      .WillOnce(Return(1));

  EXPECT_FALSE(driver_->SpawnL2TPIPSecVPN(&error));
  EXPECT_FALSE(driver_->external_task_);
  EXPECT_TRUE(driver_->SpawnL2TPIPSecVPN(&error));
  EXPECT_NE(nullptr, driver_->external_task_);
}

TEST_F(L2TPIPSecDriverTest, Connect) {
  static const char kHost[] = "192.168.2.254";
  SetArg(kProviderHostProperty, kHost);

  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, _, _, _, _, _, _, _, true, _))
      .WillOnce(Return(1));
  driver_->ConnectAsync(service_->GetCallback());
  EXPECT_TRUE(driver_->IsConnectTimeoutStarted());
}

TEST_F(L2TPIPSecDriverTest, Disconnect) {
  SetService(service_);
  driver_->Disconnect();
  EXPECT_FALSE(driver_->service_callback_);
}

TEST_F(L2TPIPSecDriverTest, OnConnectTimeout) {
  StartConnectTimeout(0);
  SetService(service_);
  EXPECT_CALL(*service_, OnDriverEvent(VPNService::kEventDriverFailure,
                                       Service::kFailureConnect, _));
  OnConnectTimeout();
  EXPECT_FALSE(driver_->service_callback_);
  EXPECT_FALSE(IsConnectTimeoutStarted());
}

TEST_F(L2TPIPSecDriverTest, InitPropertyStore) {
  // Sanity test property store initialization.
  PropertyStore store;
  driver_->InitPropertyStore(&store);
  const string kUser = "joe";
  Error error;
  EXPECT_TRUE(store.SetStringProperty(kL2tpIpsecUserProperty, kUser, &error));
  EXPECT_TRUE(error.IsSuccess());
  EXPECT_EQ(kUser, GetArgs()->Lookup<string>(kL2tpIpsecUserProperty, ""));
}

TEST_F(L2TPIPSecDriverTest, GetProvider) {
  PropertyStore store;
  driver_->InitPropertyStore(&store);
  {
    KeyValueStore props;
    Error error;
    SetArg(kL2tpIpsecClientCertIdProperty, "");
    EXPECT_TRUE(
        store.GetKeyValueStoreProperty(kProviderProperty, &props, &error));
    EXPECT_TRUE(props.Lookup<bool>(kPassphraseRequiredProperty, false));
    EXPECT_TRUE(props.Lookup<bool>(kL2tpIpsecPskRequiredProperty, false));
  }
  {
    KeyValueStore props;
    Error error;
    SetArg(kL2tpIpsecClientCertIdProperty, "some-cert-id");
    EXPECT_TRUE(
        store.GetKeyValueStoreProperty(kProviderProperty, &props, &error));
    EXPECT_TRUE(props.Lookup<bool>(kPassphraseRequiredProperty, false));
    EXPECT_FALSE(props.Lookup<bool>(kL2tpIpsecPskRequiredProperty, true));
    SetArg(kL2tpIpsecClientCertIdProperty, "");
  }
  {
    KeyValueStore props;
    SetArg(kL2tpIpsecPasswordProperty, "random-password");
    SetArg(kL2tpIpsecPskProperty, "random-psk");
    Error error;
    EXPECT_TRUE(
        store.GetKeyValueStoreProperty(kProviderProperty, &props, &error));
    EXPECT_FALSE(props.Lookup<bool>(kPassphraseRequiredProperty, true));
    EXPECT_FALSE(props.Lookup<bool>(kL2tpIpsecPskRequiredProperty, true));
    EXPECT_FALSE(props.Contains<string>(kL2tpIpsecPasswordProperty));
  }
}

TEST_F(L2TPIPSecDriverTest, Notify) {
  map<string, string> config{{kPPPInterfaceName, kInterfaceName}};
  FilePath psk_file;
  FilePath xauth_credentials_file;
  FakeUpConnect(&psk_file, &xauth_credentials_file);

  // Make sure that a notification of an intermediate state doesn't cause
  // the driver to fail the connection.
  EXPECT_CALL(*service_, OnDriverEvent(_, _, _)).Times(0);
  EXPECT_TRUE(driver_->service_callback_);
  InvokeNotify(kPPPReasonAuthenticating, config);
  EXPECT_TRUE(driver_->service_callback_);
  InvokeNotify(kPPPReasonAuthenticated, config);
  EXPECT_TRUE(driver_->service_callback_);

  ExpectMetricsReported();
  EXPECT_CALL(*service_,
              OnDriverEvent(VPNService::kEventConnectionSuccess, _, _));
  InvokeNotify(kPPPReasonConnect, config);
  EXPECT_EQ(driver_->interface_name_, kInterfaceName);
  EXPECT_TRUE(IsPSKFileCleared(psk_file));
  EXPECT_TRUE(IsXauthCredentialsFileCleared(xauth_credentials_file));
  EXPECT_FALSE(IsConnectTimeoutStarted());
}

TEST_F(L2TPIPSecDriverTest, NotifyDisconnected) {
  map<string, string> dict;
  SetService(service_);
  base::Callback<void(pid_t, int)> death_callback;
  MockExternalTask* local_external_task =
      new MockExternalTask(&control_, &process_manager_,
                           weak_ptr_factory_.GetWeakPtr(), death_callback);
  driver_->external_task_.reset(local_external_task);  // passes ownership
  EXPECT_CALL(*service_, OnDriverEvent(VPNService::kEventDriverFailure, _, _));
  EXPECT_CALL(*local_external_task, OnDelete());
  driver_->Notify(kPPPReasonDisconnect, dict);
  EXPECT_EQ(nullptr, driver_->external_task_);
  EXPECT_FALSE(IsConnectTimeoutStarted());
}

}  // namespace shill
