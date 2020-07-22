// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/authpolicy.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/test/simple_test_clock.h>
#include <base/test/task_environment.h>
#include <brillo/asan.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/file_utils.h>
#include <dbus/bus.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/scoped_dbus_error.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <login_manager/proto_bindings/policy_descriptor.pb.h>
#include <policy/device_policy_impl.h>

#include "authpolicy/anonymizer.h"
#include "authpolicy/path_service.h"
#include "authpolicy/policy/preg_policy_writer.h"
#include "authpolicy/proto_bindings/active_directory_info.pb.h"
#include "authpolicy/samba_interface.h"
#include "authpolicy/stub_common.h"
#include "bindings/chrome_device_policy.pb.h"
#include "bindings/cloud_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "bindings/policy_constants.h"

using brillo::dbus_utils::DBusObject;
using brillo::dbus_utils::ExtractMethodCallResults;
using dbus::MessageWriter;
using dbus::MockBus;
using dbus::MockExportedObject;
using dbus::MockObjectProxy;
using dbus::ObjectPath;
using dbus::ObjectProxy;
using dbus::Response;
using login_manager::PolicyDescriptor;
using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;

namespace em = enterprise_management;

namespace authpolicy {
namespace {

// Some arbitrary D-Bus message serial number. Required for mocking D-Bus calls.
const int kDBusSerial = 123;

// Some constants for policy testing.
const bool kPolicyBool = true;
const int kPolicyInt = 321;

const bool kOtherPolicyBool = false;
const int kOtherPolicyInt = 234;

constexpr char kPolicyStr[] = "Str";
constexpr char kOtherPolicyStr[] = "OtherStr";

constexpr char kExtensionId[] = "abcdeFGHabcdefghAbcdefGhabcdEfgh";
constexpr char kOtherExtensionId[] = "ababababcdcdcdcdefefefefghghghgh";

constexpr char kExtensionPolicy1[] = "Policy1";
constexpr char kExtensionPolicy2[] = "Policy2";

constexpr char kMandatoryKey[] = "Policy";
constexpr char kRecommendedKey[] = "Recommended";

// Encryption types in krb5.conf.
constexpr char kKrb5EncTypesAll[] =
    "aes256-cts-hmac-sha1-96 aes128-cts-hmac-sha1-96 rc4-hmac";
constexpr char kKrb5EncTypesStrong[] =
    "aes256-cts-hmac-sha1-96 aes128-cts-hmac-sha1-96";

// Error message when passing different account IDs to authpolicy.
constexpr char kMultiUserNotSupported[] = "Multi-user not supported";

// Stub user hash, returned from the stub Cryptohome proxy's
// GetSanitizedUsername call. Used as part of the user daemon store path.
constexpr char kSanitizedUsername[] = "user_hash";
// Stub daemon store directory used to back up auth state.
constexpr char kDaemonStoreDir[] = "daemon-store";

// SessionStateChanged signal payload we care about.
constexpr char kSessionStarted[] = "started";
constexpr char kSessionStopped[] = "stopped";

// Stub data for auth data cache testing.
constexpr char kCacheTestUserWorkgroup[] = "cache_test_user_workgroup";
constexpr char kCacheTestUserKdcIp[] = "cache_test_user_kdc_ip";
constexpr char kCacheTestUserDcName[] = "cache_test_user_dc_name";
constexpr char kCacheTestMachineWorkgroup[] = "cache_test_machine_workgroup";
constexpr char kCacheTestMachineDcName[] = "cache_test_machine_dc_name";

// See stub_net_main for the strings. Don't bother to make those constants.
constexpr char kDefaultWorkgroup[] = "WOKGROUP";
constexpr char kDefaultKdcIp[] = "111.222.33.2";
constexpr char kDefaultDcName[] = "DCNAME.EXAMPLE.COM";

// Policy values for cache testing.
const int kZeroHours = 0;
const int kNonZeroHours = 1;

struct SmbConf {
  std::string machine_name;
  std::string realm;
  std::string kerberos_encryption_types;
  std::string workgroup;
};

struct Krb5Conf {
  std::string default_tgs_enctypes;
  std::string default_tkt_enctypes;
  std::string permitted_enctypes;
  std::string allow_weak_crypto;
  std::string kdc;
};

// Checks and casts an integer |error| to the corresponding ErrorType.
WARN_UNUSED_RESULT ErrorType CastError(int error) {
  EXPECT_GE(error, 0);
  EXPECT_LT(error, ERROR_COUNT);
  return static_cast<ErrorType>(error);
}

// Create a file descriptor pointing to a pipe that contains the given data.
base::ScopedFD MakeFileDescriptor(const char* data) {
  int fds[2];
  EXPECT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  base::ScopedFD write_scoped_fd(fds[1]);
  EXPECT_TRUE(
      base::WriteFileDescriptor(write_scoped_fd.get(), data, strlen(data)));
  return read_scoped_fd;
}

// Shortcut to create a file descriptor from a valid password (valid in the
// sense that the stub executables won't trigger any error behavior).
base::ScopedFD MakePasswordFd() {
  return MakeFileDescriptor(kPassword);
}

// Stub completion callback for RegisterAsync().
void DoNothing(bool /* unused */) {}

// Creates a D-Bus response with the given |response_str| as message.
std::unique_ptr<dbus::Response> RespondWithString(
    dbus::MethodCall* method_call, const std::string& response_str) {
  method_call->SetSerial(kDBusSerial);
  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendString(response_str);
  return response;
}

// If |error| is ERROR_NONE, parses |proto_blob| into the |proto| if given.
// Otherwise, makes sure |proto_blob| is empty.
template <typename T>
void MaybeParseProto(int error,
                     const std::vector<uint8_t>& proto_blob,
                     T* proto) {
  if (error != ERROR_NONE) {
    EXPECT_TRUE(proto_blob.empty());
    return;
  }
  if (proto) {
    EXPECT_TRUE(proto->ParseFromArray(proto_blob.data(),
                                      static_cast<int>(proto_blob.size())));
  }
}

// Reads the smb.conf file at |smb_conf_path| and extracts some values.
void ReadSmbConf(const std::string& smb_conf_path, SmbConf* conf) {
  std::string smb_conf;
  EXPECT_TRUE(base::ReadFileToString(base::FilePath(smb_conf_path), &smb_conf));
  FindToken(smb_conf, '=', "netbios name", &conf->machine_name);
  EXPECT_TRUE(FindToken(smb_conf, '=', "realm", &conf->realm));
  EXPECT_TRUE(FindToken(smb_conf, '=', "kerberos encryption types",
                        &conf->kerberos_encryption_types));
  EXPECT_TRUE(FindToken(smb_conf, '=', "workgroup", &conf->workgroup));
}

// Checks whether the file at smb_conf_path is an smb.conf file and has the
// expected encryption types |expected_enc_types| set.
void CheckSmbEncTypes(const std::string& smb_conf_path,
                      const char* expected_enc_types) {
  SmbConf conf;
  ReadSmbConf(smb_conf_path, &conf);
  EXPECT_EQ(expected_enc_types, conf.kerberos_encryption_types);
}

// Reads the krb5.conf file at |krb5_conf_path| and extracts some values.
void ReadKrb5Conf(const std::string& krb5_conf_path, Krb5Conf* conf) {
  std::string krb5_conf;
  EXPECT_TRUE(
      base::ReadFileToString(base::FilePath(krb5_conf_path), &krb5_conf));
  EXPECT_TRUE(FindToken(krb5_conf, '=', "default_tgs_enctypes",
                        &conf->default_tgs_enctypes));
  EXPECT_TRUE(FindToken(krb5_conf, '=', "default_tkt_enctypes",
                        &conf->default_tkt_enctypes));
  EXPECT_TRUE(FindToken(krb5_conf, '=', "permitted_enctypes",
                        &conf->permitted_enctypes));
  EXPECT_TRUE(
      FindToken(krb5_conf, '=', "allow_weak_crypto", &conf->allow_weak_crypto));

  // KDC is optional.
  if (!FindToken(krb5_conf, '=', "kdc", &conf->kdc))
    conf->kdc.clear();
}

// Checks whether the file at krb5_conf_path is a krb5.conf file and has the
// expected encryption types |expected_enc_types| set.
void CheckKrb5EncTypes(const std::string& krb5_conf_path,
                       const char* expected_enc_types) {
  Krb5Conf conf;
  ReadKrb5Conf(krb5_conf_path, &conf);
  EXPECT_EQ(expected_enc_types, conf.default_tgs_enctypes);
  EXPECT_EQ(expected_enc_types, conf.default_tkt_enctypes);
  EXPECT_EQ(expected_enc_types, conf.permitted_enctypes);
  EXPECT_EQ("false", conf.allow_weak_crypto);
}

// Returns [|ip|].
std::string EmbraceIp(const std::string& ip) {
  return "[" + ip + "]";
}

// Helper class that points some paths to convenient locations we can write to.
class TestPathService : public PathService {
 public:
  explicit TestPathService(const base::FilePath& base_path)
      : PathService(false) {
    // Stub binaries are in the OUT folder politely provided by the test script.
    base::FilePath stub_path(getenv("OUT"));
    CHECK(!stub_path.empty());

    // Need to create the run dir since it is usually created by the ebuild.
    base::FilePath run_dir = base_path.Append("run");
    CHECK(base::CreateDirectory(run_dir));

    // Override base directories.
    Insert(Path::TEMP_DIR, base_path.Append("temp").value());
    Insert(Path::STATE_DIR, base_path.Append("state").value());
    Insert(Path::RUN_DIR, run_dir.value());
    Insert(Path::DAEMON_STORE_DIR, base_path.Append(kDaemonStoreDir).value());

    // Override binaries.
    Insert(Path::KINIT, stub_path.Append("stub_kinit").value());
    Insert(Path::KLIST, stub_path.Append("stub_klist").value());
    Insert(Path::KPASSWD, stub_path.Append("stub_kpasswd").value());
    Insert(Path::NET, stub_path.Append("stub_net").value());
    Insert(Path::SMBCLIENT, stub_path.Append("stub_smbclient").value());

    // Fill in the rest of the paths and build dependend paths.
    Initialize();
  }
};

// Metrics library that eats in particular timer errors.
class TestMetricsLibrary : public MetricsLibrary {
 public:
  bool SendToUMA(const std::string&, int, int, int, int) override {
    return true;
  }
};

// Version of AuthPolicyMetrics that just counts stats.
class TestMetrics : public AuthPolicyMetrics {
 public:
  TestMetrics() {
    // Prevent some error messages from timers.
    chromeos_metrics::TimerReporter::set_metrics_lib(&test_metrics_);
  }

  ~TestMetrics() override {
    chromeos_metrics::TimerReporter::set_metrics_lib(nullptr);
  }

  void Report(MetricType metric_type, int sample) override {
    last_metrics_sample_[metric_type] = sample;
    metrics_report_count_[metric_type]++;
  }

  void ReportError(ErrorMetricType metric_type,
                   ErrorType /* error */) override {
    error_report_count_[metric_type]++;
  }

  // Returns the most recently reported sample for the given |metric_type| or
  // -1 if the metric has not been reported.
  int GetLastMetricSample(MetricType metric_type) {
    auto iter = last_metrics_sample_.find(metric_type);
    return iter != last_metrics_sample_.end() ? iter->second : -1;
  }

  // Returns how often Report() was called with given |metric_type| and erases
  // the count. Inefficient if metric_type isn't in the map, but shorter :)
  int GetNumMetricReports(MetricType metric_type) {
    const int count = metrics_report_count_[metric_type];
    metrics_report_count_.erase(metric_type);
    return count;
  }

  // Returns how often ReportDBusResult() was called with given |metric_type|
  // and erases the count. Inefficient if |metric_type| isn't in the map, but
  // shorter :)
  int GetNumErrorReports(ErrorMetricType metric_type) {
    const int count = error_report_count_[metric_type];
    error_report_count_.erase(metric_type);
    return count;
  }

 private:
  TestMetricsLibrary test_metrics_;
  std::map<MetricType, int> last_metrics_sample_;
  std::map<MetricType, int> metrics_report_count_;
  std::map<ErrorMetricType, int> error_report_count_;
};

// Helper to check the ErrorType value returned by authpolicy D-Bus calls.
// |was_called| is a marker used by the code that queues this callback to make
// sure that this callback was indeed called.
void CheckError(ErrorType expected_error,
                bool* was_called,
                std::unique_ptr<Response> response) {
  EXPECT_TRUE(response.get());
  dbus::MessageReader reader(response.get());
  int32_t int_error;
  EXPECT_TRUE(reader.PopInt32(&int_error));
  ErrorType actual_error = CastError(int_error);
  EXPECT_EQ(expected_error, actual_error);
  EXPECT_TRUE(was_called);
  EXPECT_FALSE(*was_called);
  *was_called = true;
}

// Response callback for AuthenticateUser calls in Auth(). Sets |was_called| to
// true and deserializes the |error| and |account_info_blob| parameters.
void AuthResponseCallback(bool* was_called,
                          int* error,
                          std::vector<uint8_t>* account_info_blob,
                          std::unique_ptr<Response> response) {
  EXPECT_TRUE(response.get());
  dbus::MessageReader reader(response.get());
  EXPECT_TRUE(reader.PopInt32(error));
  // Note: reader owns buf.
  const uint8_t* buf = NULL;
  size_t buf_size = 0;
  EXPECT_TRUE(reader.PopArrayOfBytes(&buf, &buf_size));
  if (*error == ERROR_NONE) {
    EXPECT_TRUE(buf);
    EXPECT_LT(0, buf_size);
    EXPECT_TRUE(account_info_blob);
    account_info_blob->assign(buf, buf + buf_size);
  } else {
    account_info_blob->clear();
  }
  EXPECT_TRUE(was_called);
  EXPECT_FALSE(*was_called);
  *was_called = true;
}

// Matcher for D-Bus method names to be used in CallMethod*().
MATCHER_P(IsMethod, method_name, "") {
  return arg->GetMember() == method_name;
}

}  // namespace

// Integration test for the authpolicyd D-Bus interface.
//
// Since the Active Directory protocols are a black box to us, a stub local
// server cannot be used. Instead, the Samba/Kerberos binaries are stubbed out.
//
// Error behavior is triggered by passing special user principals or passwords
// to the stub binaries. For instance, using |kNonExistingUserPrincipal| makes
// stub_kinit behave as if the requested account does not exist on the server.
// The same principle is used throughout this test.
//
// During policy fetch, authpolicy sends D-Bus messages to Session Manager. This
// communication is mocked out.
class AuthPolicyTest : public testing::Test {
 public:
  void SetUp() override {
    // The message loop registers a task runner with the current thread, which
    // is used by TgtManager to post automatic TGT renewal tasks.
    const ObjectPath object_path(std::string("/object/path"));
    auto dbus_object =
        std::make_unique<DBusObject>(nullptr, mock_bus_, object_path);

    metrics_ = std::make_unique<TestMetrics>();

    // Create path service with all paths pointing into a temp directory.
    CHECK(base::CreateNewTempDirectory("" /* prefix (ignored) */, &base_path_));
    paths_ = std::make_unique<TestPathService>(base_path_);

    // Create the state directory since authpolicyd assumes its existence.
    const base::FilePath state_path =
        base::FilePath(paths_->Get(Path::STATE_DIR));
    CHECK(base::CreateDirectory(state_path));

    // Create daemon store directory where authpolicyd backs up auth state.
    user_daemon_store_path_ =
        base_path_.Append(kDaemonStoreDir).Append(kSanitizedUsername);
    CHECK(base::CreateDirectory(user_daemon_store_path_));

    // Stub path where the Kerberos ticket is backed up.
    backup_path_ = user_daemon_store_path_.Append("user_backup_data");

    // Set stub preg path. Since it is not trivial to pass the full path to the
    // stub binaries, we simply use the directory from the krb5.conf file.
    const base::FilePath gpo_dir =
        base::FilePath(paths_->Get(Path::USER_KRB5_CONF)).DirName();
    DCHECK(gpo_dir ==
           base::FilePath(paths_->Get(Path::DEVICE_KRB5_CONF)).DirName());
    stub_gpo1_path_ = gpo_dir.Append(kGpo1Filename);
    stub_gpo2_path_ = gpo_dir.Append(kGpo2Filename);

    // Mock out D-Bus initialization.
    mock_exported_object_ =
        new MockExportedObject(mock_bus_.get(), object_path);
    EXPECT_CALL(*mock_bus_, GetExportedObject(object_path))
        .Times(1)
        .WillOnce(Return(mock_exported_object_.get()));
    EXPECT_CALL(*mock_bus_, GetDBusTaskRunner())
        .Times(1)
        .WillOnce(Return(task_environment_.GetMainThreadTaskRunner().get()));
    EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
        .Times(AnyNumber());
    EXPECT_CALL(*mock_exported_object_, SendSignal(_))
        .WillRepeatedly(
            Invoke(this, &AuthPolicyTest::HandleUserKerberosFilesChanged));

    // Set up mock object proxy for session manager called from authpolicy.
    mock_session_manager_proxy_ = new MockObjectProxy(
        mock_bus_.get(), login_manager::kSessionManagerServiceName,
        dbus::ObjectPath(login_manager::kSessionManagerServicePath));
    EXPECT_CALL(*mock_bus_,
                GetObjectProxy(login_manager::kSessionManagerServiceName, _))
        .WillOnce(Return(mock_session_manager_proxy_.get()));
    EXPECT_CALL(
        *mock_session_manager_proxy_,
        DoCallMethodWithErrorCallback(
            IsMethod(login_manager::kSessionManagerStoreUnsignedPolicyEx), _, _,
            _))
        .WillRepeatedly(
            Invoke(this, &AuthPolicyTest::StubCallStorePolicyMethod));
    EXPECT_CALL(
        *mock_session_manager_proxy_,
        CallMethodAndBlockWithErrorDetails(
            IsMethod(login_manager::kSessionManagerListStoredComponentPolicies),
            _, _))
        .WillRepeatedly(
            Invoke(this, &AuthPolicyTest::StubListComponentIdsMethod));
    EXPECT_CALL(
        *mock_session_manager_proxy_,
        DoConnectToSignal(login_manager::kSessionManagerInterface,
                          login_manager::kSessionStateChangedSignal, _, _))
        .WillOnce((SaveArg<2>(&session_state_changed_callback_)));
    EXPECT_CALL(
        *mock_session_manager_proxy_.get(),
        CallMethodAndBlockWithErrorDetails(
            IsMethod(login_manager::kSessionManagerRetrieveSessionState), _, _))
        .WillOnce(Invoke([](dbus::MethodCall* method_call, int timeout,
                            dbus::ScopedDBusError* error) {
          return RespondWithString(method_call, kSessionStopped);
        }));

    // Set up mock object proxy for Cryptohome called from authpolicy.
    mock_cryptohome_proxy_ = new NiceMock<MockObjectProxy>(
        mock_bus_.get(), cryptohome::kCryptohomeServiceName,
        dbus::ObjectPath(cryptohome::kCryptohomeServicePath));
    EXPECT_CALL(*mock_bus_,
                GetObjectProxy(cryptohome::kCryptohomeServiceName, _))
        .WillOnce(Return(mock_cryptohome_proxy_.get()));

    // Make Cryptohome's GetSanitizedUsername call return kSanitizedUsername.
    ON_CALL(*mock_cryptohome_proxy_, CallMethodAndBlock(_, _))
        .WillByDefault(Invoke([](dbus::MethodCall* method_call, int timeout) {
          return RespondWithString(method_call, kSanitizedUsername);
        }));

    // Create AuthPolicy instance. Do this AFTER creating the proxy mocks since
    // they might be accessed during initialization.
    authpolicy_ = std::make_unique<AuthPolicy>(metrics_.get(), paths_.get());
    EXPECT_EQ(ERROR_NONE, authpolicy_->Initialize(false /* expect_config */));
    authpolicy_->RegisterAsync(std::move(dbus_object), base::Bind(&DoNothing));

    // Don't sleep for kinit/smbclient retries, it just prolongs our tests.
    samba().DisableRetrySleepForTesting();

    // Unit tests usually run code that only exists in tests (like the
    // framework), so disable the seccomp filters.
    samba().DisableSeccompForTesting(true);
  }

  // Stub method called by the Session Manager mock to store policy. Validates
  // the type of policy (user/device) contained in the |method_call|. If set by
  // the individual unit tests, calls |validate_user_policy_| or
  // |validate_device_policy_|  to validate the contents of the policy proto.
  void StubCallStorePolicyMethod(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      ObjectProxy::ResponseCallback* callback,
      dbus::ObjectProxy::ErrorCallback* error_callback) {
    // Safety check to make sure that old values are not carried along.
    if (!store_policy_called_) {
      EXPECT_FALSE(user_policy_validated_);
      EXPECT_FALSE(device_policy_validated_);
      EXPECT_EQ(0, validated_extension_ids_.size());
    } else {
      // The first policy stored is always user or device policy.
      EXPECT_TRUE(user_policy_validated_ ^ device_policy_validated_);
    }
    store_policy_called_ = true;

    // Based on the method name, check whether this is user or device policy.
    EXPECT_TRUE(method_call);
    EXPECT_TRUE(method_call->GetMember() ==
                login_manager::kSessionManagerStoreUnsignedPolicyEx);

    // Extract the policy blob from the method call.
    std::vector<uint8_t> descriptor_blob;
    std::vector<uint8_t> response_blob;
    brillo::ErrorPtr error;
    EXPECT_TRUE(ExtractMethodCallResults(method_call, &error, &descriptor_blob,
                                         &response_blob));

    // Unpack descriptor.
    PolicyDescriptor descriptor;
    const std::string descriptor_blob_str(descriptor_blob.begin(),
                                          descriptor_blob.end());
    EXPECT_TRUE(descriptor.ParseFromString(descriptor_blob_str));

    // If policy is deleted, response_blob is an empty string.
    if (response_blob.empty()) {
      EXPECT_EQ(descriptor.domain(), login_manager::POLICY_DOMAIN_EXTENSIONS);
      deleted_extension_ids_.insert(descriptor.component_id());
    } else {
      // Unwrap the three gazillion layers or policy.
      const std::string response_blob_str(response_blob.begin(),
                                          response_blob.end());
      em::PolicyFetchResponse policy_response;
      EXPECT_TRUE(policy_response.ParseFromString(response_blob_str));
      em::PolicyData policy_data;
      EXPECT_TRUE(policy_data.ParseFromString(policy_response.policy_data()));

      // Run the policy through the appropriate policy validator.
      ValidatePolicy(descriptor, policy_data);
    }

    // Answer authpolicy with an empty response to signal that policy has been
    // stored.
    EXPECT_FALSE((*callback).is_null());
    std::move(*callback).Run(Response::CreateEmpty().get());
  }

  // Stub method called by the Session Manager mock to list stored component
  // policy ids.
  std::unique_ptr<dbus::Response> StubListComponentIdsMethod(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      dbus::ScopedDBusError* error) {
    method_call->SetSerial(kDBusSerial);
    auto response = dbus::Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());
    writer.AppendArrayOfStrings(stored_extension_ids_);

    // Note: The mock wraps this back into a std::unique_ptr.
    return response;
  }

  void TearDown() override {
    EXPECT_EQ(expected_error_reports[ERROR_OF_AUTHENTICATE_USER],
              metrics_->GetNumErrorReports(ERROR_OF_AUTHENTICATE_USER));
    EXPECT_EQ(expected_error_reports[ERROR_OF_GET_USER_STATUS],
              metrics_->GetNumErrorReports(ERROR_OF_GET_USER_STATUS));
    EXPECT_EQ(expected_error_reports[ERROR_OF_GET_USER_KERBEROS_FILES],
              metrics_->GetNumErrorReports(ERROR_OF_GET_USER_KERBEROS_FILES));
    EXPECT_EQ(expected_error_reports[ERROR_OF_JOIN_AD_DOMAIN],
              metrics_->GetNumErrorReports(ERROR_OF_JOIN_AD_DOMAIN));
    EXPECT_EQ(expected_error_reports[ERROR_OF_REFRESH_USER_POLICY],
              metrics_->GetNumErrorReports(ERROR_OF_REFRESH_USER_POLICY));
    EXPECT_EQ(expected_error_reports[ERROR_OF_REFRESH_DEVICE_POLICY],
              metrics_->GetNumErrorReports(ERROR_OF_REFRESH_DEVICE_POLICY));

    EXPECT_CALL(*mock_exported_object_, Unregister()).Times(1);
    // Don't not leave no mess behind.
    base::DeleteFile(base_path_, true /* recursive */);
  }

 protected:
  // Joins a (stub) Active Directory domain. Returns the error code.
  ErrorType Join(const std::string& machine_name,
                 const std::string& user_principal,
                 base::ScopedFD password_fd) WARN_UNUSED_RESULT {
    JoinDomainRequest request;
    request.set_machine_name(machine_name);
    request.set_user_principal_name(user_principal);
    return JoinEx(request, std::move(password_fd));
  }

  // Joins a (stub) Active Directory domain, locks the device and fetches empty
  // device policy. Expects success.
  void JoinAndFetchDevicePolicy(const std::string& machine_name) {
    EXPECT_EQ(ERROR_NONE, Join(machine_name, kUserPrincipal, MakePasswordFd()));
    MarkDeviceAsLocked();
    validate_device_policy_ = &CheckDevicePolicyEmpty;
    FetchAndValidateDevicePolicy(ERROR_NONE);
  }

  // Extended Join() that takes a full JoinDomainRequest proto.
  ErrorType JoinEx(const JoinDomainRequest& request,
                   base::ScopedFD password_fd,
                   std::string* joined_domain = nullptr) WARN_UNUSED_RESULT {
    expected_error_reports[ERROR_OF_JOIN_AD_DOMAIN]++;
    std::vector<uint8_t> blob(request.ByteSizeLong());
    request.SerializeToArray(blob.data(), blob.size());
    int error;
    authpolicy_->JoinADDomain(blob, password_fd, &error, joined_domain);
    return CastError(error);
  }

  // Authenticates to a (stub) Active Directory domain with the given
  // credentials and returns the error code. Assigns the user account info to
  // |account_info| if a non-nullptr is provided.
  ErrorType Auth(const std::string& user_principal,
                 const std::string& account_id,
                 base::ScopedFD password_fd,
                 ActiveDirectoryAccountInfo* account_info = nullptr)
      WARN_UNUSED_RESULT {
    int32_t error = ERROR_NONE;
    std::vector<uint8_t> account_info_blob;
    expected_error_reports[ERROR_OF_AUTHENTICATE_USER]++;
    int prev_files_changed_count = user_kerberos_files_changed_count_;
    AuthenticateUserRequest request;
    request.set_user_principal_name(user_principal);
    request.set_account_id(account_id);
    std::vector<uint8_t> request_blob(request.ByteSizeLong());
    request.SerializeToArray(request_blob.data(), request_blob.size());

    bool callback_was_called = false;
    dbus::MethodCall method_call(kAuthPolicyInterface, kAuthenticateUserMethod);
    method_call.SetSerial(kDBusSerial);
    auto callback = std::make_unique<
        brillo::dbus_utils::DBusMethodResponse<int32_t, std::vector<uint8_t>>>(
        &method_call, base::Bind(&AuthResponseCallback, &callback_was_called,
                                 &error, &account_info_blob));
    authpolicy_->AuthenticateUser(std::move(callback), request_blob,
                                  password_fd);
    EXPECT_TRUE(callback_was_called);

    MaybeParseProto(error, account_info_blob, account_info);
    // At most one UserKerberosFilesChanged signal should have been fired.
    EXPECT_LE(user_kerberos_files_changed_count_, prev_files_changed_count + 1);
    return CastError(error);
  }

  // Gets a fake user status from a (stub) Active Directory service.
  // |account_id| is the id (aka objectGUID) of the user. Assigns the user's
  // status to |user_status| if a non-nullptr is given.
  ErrorType GetUserStatus(const std::string& user_principal,
                          const std::string& account_id,
                          ActiveDirectoryUserStatus* user_status = nullptr)
      WARN_UNUSED_RESULT {
    int32_t error = ERROR_NONE;
    std::vector<uint8_t> user_status_blob;
    expected_error_reports[ERROR_OF_GET_USER_STATUS]++;
    GetUserStatusRequest request;
    request.set_user_principal_name(user_principal);
    request.set_account_id(account_id);
    std::vector<uint8_t> blob(request.ByteSizeLong());
    request.SerializeToArray(blob.data(), blob.size());
    authpolicy_->GetUserStatus(blob, &error, &user_status_blob);
    MaybeParseProto(error, user_status_blob, user_status);
    return CastError(error);
  }

  ErrorType GetUserKerberosFiles(const std::string& account_id,
                                 KerberosFiles* kerberos_files = nullptr)
      WARN_UNUSED_RESULT {
    int32_t error = ERROR_NONE;
    std::vector<uint8_t> kerberos_files_blob;
    expected_error_reports[ERROR_OF_GET_USER_KERBEROS_FILES]++;
    authpolicy_->GetUserKerberosFiles(account_id, &error, &kerberos_files_blob);
    MaybeParseProto(error, kerberos_files_blob, kerberos_files);
    return CastError(error);
  }

  // Authenticates to a (stub) Active Directory domain with default credentials.
  // Returns the account id.
  std::string DefaultAuth() {
    ActiveDirectoryAccountInfo account_info;
    EXPECT_EQ(ERROR_NONE,
              Auth(kUserPrincipal, "", MakePasswordFd(), &account_info));
    return account_info.account_id();
  }

  // Calls AuthPolicy::RefreshUserPolicy(). Verifies that
  // StubCallStorePolicyMethod() and validate_user_policy_ are called as
  // expected. These callbacks verify that the policy protobuf is valid and
  // validate the contents.
  void FetchAndValidateUserPolicy(const std::string& account_id,
                                  ErrorType expected_error) {
    dbus::MethodCall method_call(kAuthPolicyInterface,
                                 kRefreshUserPolicyMethod);
    method_call.SetSerial(kDBusSerial);
    store_policy_called_ = false;
    user_policy_validated_ = false;
    device_policy_validated_ = false;
    validated_extension_ids_.clear();
    deleted_extension_ids_.clear();
    bool callback_was_called = false;
    AuthPolicy::PolicyResponseCallback callback =
        std::make_unique<brillo::dbus_utils::DBusMethodResponse<int32_t>>(
            &method_call,
            base::Bind(&CheckError, expected_error, &callback_was_called));
    expected_error_reports[ERROR_OF_REFRESH_USER_POLICY]++;
    authpolicy_->RefreshUserPolicy(std::move(callback), account_id);

    // If policy fetch succeeds, authpolicy_ makes a D-Bus call to Session
    // Manager to store policy. We intercept this call and point it to
    // StubCallStorePolicyMethod(), which validates policy and calls CheckError.
    // If policy fetch fails, StubCallStorePolicyMethod() is not called, but
    // authpolicy calls CheckError directly.
    EXPECT_EQ(expected_error == ERROR_NONE, store_policy_called_);
    EXPECT_EQ(expected_error == ERROR_NONE, user_policy_validated_);
    EXPECT_FALSE(expected_error != ERROR_NONE &&
                 validated_extension_ids_.size() > 0);
    EXPECT_FALSE(device_policy_validated_);
    EXPECT_TRUE(callback_was_called);  // Make sure CheckError() was called.
  }

  // Calls AuthPolicy::RefreshDevicePolicy(). Verifies that
  // StubCallStorePolicyMethod() and validate_device_policy_ are called as
  // expected. These callbacks verify that the policy protobuf is valid and
  // validate the contents.
  void FetchAndValidateDevicePolicy(ErrorType expected_error) {
    dbus::MethodCall method_call(kAuthPolicyInterface,
                                 kRefreshDevicePolicyMethod);
    method_call.SetSerial(kDBusSerial);
    store_policy_called_ = false;
    user_policy_validated_ = false;
    device_policy_validated_ = false;
    validated_extension_ids_.clear();
    deleted_extension_ids_.clear();
    bool callback_was_called = false;
    AuthPolicy::PolicyResponseCallback callback =
        std::make_unique<brillo::dbus_utils::DBusMethodResponse<int32_t>>(
            &method_call,
            base::Bind(&CheckError, expected_error, &callback_was_called));
    expected_error_reports[ERROR_OF_REFRESH_DEVICE_POLICY]++;
    authpolicy_->RefreshDevicePolicy(std::move(callback));

    // If policy fetch succeeds, authpolicy_ makes a D-Bus call to Session
    // Manager to store policy. We intercept this call and point it to
    // StubCallStorePolicyMethod(), which validates policy and calls CheckError.
    // If policy fetch fails, StubCallStorePolicyMethod() is not called, but
    // authpolicy calls CheckError directly.
    EXPECT_EQ(expected_error == ERROR_NONE, store_policy_called_);
    EXPECT_EQ(expected_error == ERROR_NONE, device_policy_validated_);
    EXPECT_FALSE(expected_error != ERROR_NONE &&
                 validated_extension_ids_.size());
    EXPECT_FALSE(user_policy_validated_);
    EXPECT_TRUE(callback_was_called);  // Make sure CheckError() was called.
  }

  // Runs the policy stored in |policy_data| through the validator function
  // for the corresponding policy type.
  void ValidatePolicy(const PolicyDescriptor& descriptor,
                      const em::PolicyData& policy_data) {
    if (policy_data.policy_type() == kChromeUserPolicyType) {
      EXPECT_EQ(descriptor.account_type(), login_manager::ACCOUNT_TYPE_USER);
      EXPECT_FALSE(descriptor.account_id().empty());
      EXPECT_EQ(descriptor.domain(), login_manager::POLICY_DOMAIN_CHROME);
      EXPECT_TRUE(descriptor.component_id().empty());
      em::CloudPolicySettings policy;
      EXPECT_TRUE(policy.ParseFromString(policy_data.policy_value()));
      if (validate_user_policy_) {
        validate_user_policy_(policy);
        user_policy_validated_ = true;
      }
      user_affiliation_marker_set_ =
          policy_data.user_affiliation_ids_size() == 1 &&
          policy_data.user_affiliation_ids(0) == kAffiliationMarker;
    } else if (policy_data.policy_type() == kChromeDevicePolicyType) {
      EXPECT_EQ(descriptor.account_type(), login_manager::ACCOUNT_TYPE_DEVICE);
      EXPECT_TRUE(descriptor.account_id().empty());
      EXPECT_EQ(descriptor.domain(), login_manager::POLICY_DOMAIN_CHROME);
      EXPECT_TRUE(descriptor.component_id().empty());
      em::ChromeDeviceSettingsProto policy;
      EXPECT_TRUE(policy.ParseFromString(policy_data.policy_value()));
      if (validate_device_policy_) {
        validate_device_policy_(policy);
        device_policy_validated_ = true;
      }
      EXPECT_EQ(1, policy_data.device_affiliation_ids_size());
      EXPECT_EQ(kAffiliationMarker, policy_data.device_affiliation_ids(0));
    } else if (policy_data.policy_type() == kChromeExtensionPolicyType) {
      EXPECT_EQ(descriptor.domain(), login_manager::POLICY_DOMAIN_EXTENSIONS);
      EXPECT_EQ(descriptor.component_id(), policy_data.settings_entity_id());
      if (validate_extension_policy_) {
        // policy_value() is the raw JSON string here.
        validate_extension_policy_(descriptor.component_id(),
                                   policy_data.policy_value());
        validated_extension_ids_.insert(descriptor.component_id());
      }
    }
  }

  // Checks whether the user |policy| is empty.
  static void CheckUserPolicyEmpty(const em::CloudPolicySettings& policy) {
    em::CloudPolicySettings empty_policy;
    EXPECT_EQ(policy.ByteSizeLong(), empty_policy.ByteSizeLong());
  }

  // Does not validate user policy. Use if you're testing something unrelated.
  static void DontValidateUserPolicy(
      const em::CloudPolicySettings& /* policy */) {}

  // Checks whether the device |policy| is empty.
  static void CheckDevicePolicyEmpty(
      const em::ChromeDeviceSettingsProto& policy) {
    em::ChromeDeviceSettingsProto empty_policy;
    EXPECT_EQ(policy.ByteSizeLong(), empty_policy.ByteSizeLong());
  }

  // Does not validate device policy. Use if you're testing something unrelated.
  static void DontValidateDevicePolicy(
      const em::ChromeDeviceSettingsProto& /* policy */) {}

  // Checks whether the extension |policy_json| is empty.
  static void CheckExtensionPolicyEmpty(const std::string& /* extension_id */,
                                        const std::string& policy_json) {
    EXPECT_TRUE(policy_json.empty());
  }

  // Writes some default extension to the given writer.
  static void WriteDefaultExtensionPolicy(policy::PRegPolicyWriter* writer) {
    writer->SetKeysForExtensionPolicy(kExtensionId);
    writer->AppendString(kExtensionPolicy1, kPolicyStr);
    writer->SetKeysForExtensionPolicy(kOtherExtensionId);
    writer->AppendBoolean(kExtensionPolicy2, kPolicyBool,
                          policy::POLICY_LEVEL_RECOMMENDED);
  }

  // Checks some default extension |policy_json| we're using for this test.
  static void CheckDefaultExtensionPolicy(const std::string& extension_id,
                                          const std::string& policy_json) {
    std::string expected_policy_json;
    if (extension_id == kExtensionId) {
      expected_policy_json =
          base::StringPrintf("{\"%s\":{\"%s\":\"%s\"}}", kMandatoryKey,
                             kExtensionPolicy1, kPolicyStr);
    } else if (extension_id == kOtherExtensionId) {
      expected_policy_json = base::StringPrintf(
          "{\"%s\":{\"%s\":1}}", kRecommendedKey, kExtensionPolicy2);
    } else {
      FAIL() << "Unexpected extension id " << extension_id;
    }
    EXPECT_EQ(policy_json, expected_policy_json);
  }

  // Authpolicyd revokes write permissions on config.dat. Some tests perform two
  // domain joins, though, and need to overwrite the previously generated config
  // file.
  bool MakeConfigWriteable() {
    const base::FilePath config_path(paths_->Get(Path::CONFIG_DAT));
    const int mode = base::FILE_PERMISSION_READ_BY_USER |
                     base::FILE_PERMISSION_WRITE_BY_USER;
    return base::SetPosixFilePermissions(config_path, mode);
  }

  SambaInterface& samba() { return authpolicy_->GetSambaInterfaceForTesting(); }
  void MarkDeviceAsLocked() { authpolicy_->SetDeviceIsLockedForTesting(); }

  // Writes one file to |gpo_path| with a few policies. Sets up
  // |validate_device_policy_| callback with corresponding expectations.
  void SetupDeviceOneGpo(const base::FilePath& gpo_path) {
    policy::PRegUserDevicePolicyWriter writer;
    writer.AppendBoolean(policy::key::kDeviceGuestModeEnabled, kPolicyBool);
    writer.AppendInteger(policy::key::kDevicePolicyRefreshRate, kPolicyInt);
    writer.AppendString(policy::key::kSystemTimezone, kPolicyStr);
    const std::vector<std::string> str_list = {"str1", "str2"};
    writer.AppendStringList(policy::key::kDeviceUserWhitelist, str_list);
    writer.WriteToFile(gpo_path);

    validate_device_policy_ = [str_list](
                                  const em::ChromeDeviceSettingsProto& policy) {
      EXPECT_EQ(kPolicyBool, policy.guest_mode_enabled().guest_mode_enabled());
      EXPECT_EQ(
          kPolicyInt,
          policy.device_policy_refresh_rate().device_policy_refresh_rate());
      EXPECT_EQ(kPolicyStr, policy.system_timezone().timezone());
      const em::UserWhitelistProto& str_list_proto = policy.user_whitelist();
      EXPECT_EQ(str_list_proto.user_whitelist_size(),
                static_cast<int>(str_list.size()));
      for (int n = 0; n < str_list_proto.user_whitelist_size(); ++n)
        EXPECT_EQ(str_list_proto.user_whitelist(n), str_list.at(n));
    };
  }

  // Writes a device policy file to |policy_path|. The file can be read with
  // libpolicy.
  void WriteDevicePolicyFile(const base::FilePath& policy_path,
                             const em::ChromeDeviceSettingsProto& policy) {
    em::PolicyData policy_data;
    policy_data.set_policy_value(policy.SerializeAsString());
    em::PolicyFetchResponse policy_fetch_response;
    policy_fetch_response.set_policy_data(policy_data.SerializeAsString());
    std::string policy_blob = policy_fetch_response.SerializeAsString();
    brillo::WriteStringToFile(policy_path, policy_blob);
  }

  // Writes |device_policy| to a file, points samba() to it and reinitializes
  // samba(). This simulates a restart of authpolicyd with given device policy.
  void WritePolicyAndRestartAuthpolicy(
      const em::ChromeDeviceSettingsProto& device_policy) {
    const base::FilePath policy_path = base_path_.Append("policy");
    WriteDevicePolicyFile(policy_path, device_policy);

    // Set up a device policy instance that reads from our fake file.
    // Verification has to be disabled since MarkDeviceAsLocked() applies to
    // authpolicy only, but doesn't actually set the real install attributes
    // read by the impl.
    auto policy_impl = std::make_unique<policy::DevicePolicyImpl>();
    policy_impl->set_policy_path_for_testing(policy_path);
    policy_impl->set_verify_policy_for_testing(false);

    // Initialize again. This should load the device policy file.
    samba().ResetForTesting();
    samba().SetDevicePolicyImplForTesting(std::move(policy_impl));
    EXPECT_EQ(ERROR_NONE, samba().Initialize(true /* expect_config */));
  }

  // Returns the modification time of the file at |path|.
  base::Time GetLastModified(const base::FilePath& path) {
    base::File::Info file_info;
    EXPECT_TRUE(GetFileInfo(path, &file_info));
    return file_info.last_modified;
  }

  // Returns the modification time of the file at |path|.
  base::Time GetLastModified(Path path) {
    return GetLastModified(base::FilePath(paths_->Get(path)));
  }

  void SetLastModified(Path path, const base::Time& last_modified) {
    const base::FilePath filepath(paths_->Get(path));
    base::File::Info file_info;
    EXPECT_TRUE(GetFileInfo(filepath, &file_info));
    EXPECT_TRUE(
        base::TouchFile(filepath, file_info.last_accessed, last_modified));
  }

  // Returns the contents of the file at |path|.
  std::string ReadFile(Path path) {
    std::string str;
    EXPECT_TRUE(
        base::ReadFileToString(base::FilePath(paths_->Get(path)), &str));
    return str;
  }

  // Sends the session started signal to authpolicyd.
  void NotifySessionStarted() {
    // Tell authpolicyd that the session started.
    dbus::Signal signal(login_manager::kSessionManagerInterface,
                        login_manager::kSessionStateChangedSignal);
    dbus::MessageWriter writer(&signal);
    writer.AppendString("started");
    session_state_changed_callback_.Run(&signal);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};

  scoped_refptr<MockBus> mock_bus_ = new MockBus(dbus::Bus::Options());
  scoped_refptr<MockExportedObject> mock_exported_object_;
  scoped_refptr<MockObjectProxy> mock_session_manager_proxy_;
  scoped_refptr<MockObjectProxy> mock_cryptohome_proxy_;

  // Notifies authpolicy that the session state changed (e.g. "started").
  base::Callback<void(dbus::Signal* signal)> session_state_changed_callback_;

  // Keep this order! |authpolicy_| must be last as it depends on the other two.
  std::unique_ptr<TestMetrics> metrics_;
  std::unique_ptr<TestPathService> paths_;
  std::unique_ptr<AuthPolicy> authpolicy_;

  base::FilePath base_path_;
  base::FilePath stub_gpo1_path_;
  base::FilePath stub_gpo2_path_;
  base::FilePath user_daemon_store_path_;
  base::FilePath backup_path_;

  // Markers to check whether various callbacks are actually called.
  bool store_policy_called_ = false;      // StubCallStorePolicyMethod()
  bool user_policy_validated_ = false;    // Policy validation
  bool device_policy_validated_ = false;  //   callbacks below.

  // IDs of extensions for which policy was validated.
  std::multiset<std::string> validated_extension_ids_;
  // IDs of extensions for which policy was deleted.
  std::multiset<std::string> deleted_extension_ids_;
  // IDs returned from the stub Session Manager for ListStoredComponentPolicies.
  std::vector<std::string> stored_extension_ids_;

  // Set by ValidatePolicy during user policy validation if the affiliation
  // marker is set.
  bool user_affiliation_marker_set_ = false;

  // How often the UserKerberosFilesChanged signal was fired.
  int user_kerberos_files_changed_count_ = 0;

  // Must be set in unit tests to validate policy protos which authpolicy_ sends
  // to Session Manager via D-Bus (resp. to StubCallStorePolicyMethod() in these
  // tests).
  std::function<void(const em::CloudPolicySettings&)> validate_user_policy_;
  std::function<void(const em::ChromeDeviceSettingsProto&)>
      validate_device_policy_;
  std::function<void(const std::string&, const std::string&)>
      validate_extension_policy_;

 private:
  void HandleUserKerberosFilesChanged(dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), "org.chromium.AuthPolicy");
    EXPECT_EQ(signal->GetMember(), "UserKerberosFilesChanged");
    user_kerberos_files_changed_count_++;
  }

  // Expected calls of metrics reporting functions, set and checked internally.
  std::map<ErrorMetricType, int> expected_error_reports;
};

// Can't fetch user policy if the user is not logged in.
TEST_F(AuthPolicyTest, UserPolicyFailsNotLoggedIn) {
  FetchAndValidateUserPolicy("account_id", ERROR_NOT_LOGGED_IN);
}

// Can't fetch device policy if the device is not joined.
TEST_F(AuthPolicyTest, DevicePolicyFailsNotJoined) {
  FetchAndValidateDevicePolicy(ERROR_NOT_JOINED);
}

// Authentication fails if the machine is not joined.
TEST_F(AuthPolicyTest, AuthFailsNotJoined) {
  EXPECT_EQ(ERROR_NOT_JOINED, Auth(kUserPrincipal, "", MakePasswordFd()));
}

// Successful domain join. The machine should join the user's domain since
// Join() doesn't specify machine domain.
TEST_F(AuthPolicyTest, JoinSucceeds) {
  JoinDomainRequest request;
  request.set_machine_name(kMachineName);
  request.set_user_principal_name(kUserPrincipal);
  std::string joined_realm;
  EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd(), &joined_realm));
  SmbConf conf;
  ReadSmbConf(paths_->Get(Path::DEVICE_SMB_CONF), &conf);
  EXPECT_EQ(base::ToUpperASCII(kMachineName), conf.machine_name);
  EXPECT_EQ(kUserRealm, conf.realm);
  EXPECT_EQ(kUserRealm, joined_realm);
  EXPECT_EQ(kEncTypesStrong, conf.kerberos_encryption_types);
}

// Successful domain join with separate machine domain specified.
TEST_F(AuthPolicyTest, JoinSucceedsWithDifferentDomain) {
  JoinDomainRequest request;
  request.set_machine_name(kMachineName);
  request.set_machine_domain(kMachineRealm);
  request.set_user_principal_name(kUserPrincipal);
  std::string joined_realm;
  EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd(), &joined_realm));
  SmbConf conf;
  ReadSmbConf(paths_->Get(Path::DEVICE_SMB_CONF), &conf);
  EXPECT_EQ(base::ToUpperASCII(kMachineName), conf.machine_name);
  EXPECT_EQ(kMachineRealm, conf.realm);
  EXPECT_EQ(kMachineRealm, joined_realm);
}

// Successful domain join with organizational unit (OU).
TEST_F(AuthPolicyTest, JoinSucceedsWithOrganizationalUnit) {
  JoinDomainRequest request;
  request.set_machine_name(kMachineName);
  request.set_user_principal_name(kExpectOuUserPrincipal);
  for (size_t n = 0; n < kExpectedOuPartsSize; ++n)
    *request.add_machine_ou() = kExpectedOuParts[n];
  EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd()));
  // Note: We can't test directly whether the computer was put into the right OU
  // because there's no state for that in authpolicy. The only indicator is the
  // 'createcomputer' parameter to net ads join, but that can only be tested in
  // stub_net, see kExpectOuUserPrincipal.
}

// Encryption types are written properly to smb.conf.
TEST_F(AuthPolicyTest, JoinSetsProperEncTypes) {
  std::pair<KerberosEncryptionTypes, const char*> enc_types_list[] = {
      {ENC_TYPES_ALL, kEncTypesAll},
      {ENC_TYPES_STRONG, kEncTypesStrong},
      {ENC_TYPES_LEGACY, kEncTypesLegacy}};

  for (const auto& enc_types : enc_types_list) {
    JoinDomainRequest request;
    request.set_machine_name(kMachineName);
    request.set_user_principal_name(kUserPrincipal);
    request.set_kerberos_encryption_types(enc_types.first);
    EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd()));
    CheckSmbEncTypes(paths_->Get(Path::DEVICE_SMB_CONF), enc_types.second);
    EXPECT_TRUE(MakeConfigWriteable());
    samba().ResetForTesting();
  }
}

// The encryption types reset to strong after device policy fetch.
TEST_F(AuthPolicyTest, EncTypesResetAfterDevicePolicyFetch) {
  // Disable machine password change, because the password check runs
  // immediately and wipes smb.conf (to get server time) with ENC_TYPES_STRONG,
  // so the check below for kEncTypesAll fails.
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendInteger(policy::key::kDeviceMachinePasswordChangeRate, 0);
  writer.WriteToFile(stub_gpo1_path_);

  JoinDomainRequest request;
  request.set_machine_name(kOneGpoMachineName);
  request.set_user_principal_name(kUserPrincipal);
  request.set_kerberos_encryption_types(ENC_TYPES_ALL);
  EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd()));
  MarkDeviceAsLocked();

  // After the first device policy fetch, the enc types should be 'strong'
  // internally, but the conf files used should still contain 'all' types.
  validate_device_policy_ = &DontValidateDevicePolicy;
  FetchAndValidateDevicePolicy(ERROR_NONE);
  CheckSmbEncTypes(paths_->Get(Path::DEVICE_SMB_CONF), kEncTypesAll);
  CheckKrb5EncTypes(paths_->Get(Path::DEVICE_KRB5_CONF), kKrb5EncTypesAll);

  // After the second device policy fetch, the conf files should contain
  // 'strong' enc types.
  FetchAndValidateDevicePolicy(ERROR_NONE);
  CheckSmbEncTypes(paths_->Get(Path::DEVICE_SMB_CONF), kEncTypesStrong);
  CheckKrb5EncTypes(paths_->Get(Path::DEVICE_KRB5_CONF), kKrb5EncTypesStrong);

  // Likewise, auth should only use 'strong' types.
  validate_user_policy_ = &CheckUserPolicyEmpty;
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
  CheckSmbEncTypes(paths_->Get(Path::USER_SMB_CONF), kEncTypesStrong);
  CheckKrb5EncTypes(paths_->Get(Path::USER_KRB5_CONF), kKrb5EncTypesStrong);
}

// If The encryption types reset to strong after device policy fetch.
TEST_F(AuthPolicyTest, LoadsDevicePolicyOnStartup) {
  // Join to bootstrap a config file.
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();

  // Set a device policy file with Kerberos encryption types set to 'all' and
  // restart authpolicy, so that it loads this file on startup.
  em::ChromeDeviceSettingsProto device_policy;
  device_policy.mutable_device_kerberos_encryption_types()->set_types(
      em::DeviceKerberosEncryptionTypesProto::ENC_TYPES_ALL);
  WritePolicyAndRestartAuthpolicy(device_policy);

  // Now an auth operation should use the loaded encryption types.
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  CheckKrb5EncTypes(paths_->Get(Path::USER_KRB5_CONF), kKrb5EncTypesAll);
}

// Both Samba commands (smb) and kinit (krb5) use the encryption types from the
// previous device policy fetch.
TEST_F(AuthPolicyTest, UsesEncTypesFromDevicePolicy) {
  // Write a GPO with DeviceKerberosEncryptionTypes set to 'all'.
  auto enc_types_all = em::DeviceKerberosEncryptionTypesProto::ENC_TYPES_ALL;
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendInteger(policy::key::kDeviceKerberosEncryptionTypes,
                       enc_types_all);
  writer.WriteToFile(stub_gpo1_path_);
  validate_device_policy_ = &DontValidateDevicePolicy;

  // Join and fetch device policy. This should set encryption types to 'all' in
  // Samba.
  EXPECT_EQ(ERROR_NONE,
            Join(kOneGpoMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();
  FetchAndValidateDevicePolicy(ERROR_NONE);

  // Now subsequent calls should use encryption types 'all', both for stuff
  // using smb.conf (policy fetch) as well as stuff using Kerberos tickets (user
  // auth, device policy fetch).
  validate_user_policy_ = &CheckUserPolicyEmpty;
  FetchAndValidateDevicePolicy(ERROR_NONE);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);

  // User and device smb.conf has enc types 'all'.
  CheckSmbEncTypes(paths_->Get(Path::USER_SMB_CONF), kEncTypesAll);
  CheckSmbEncTypes(paths_->Get(Path::DEVICE_SMB_CONF), kEncTypesAll);

  // User and device krb5.conf has aes_* + rc4_hmac enc types.
  CheckKrb5EncTypes(paths_->Get(Path::USER_KRB5_CONF), kKrb5EncTypesAll);
  CheckKrb5EncTypes(paths_->Get(Path::DEVICE_KRB5_CONF), kKrb5EncTypesAll);
}

// By default, the user's and device's krb5.conf files only have strong crypto.
TEST_F(AuthPolicyTest, TgtsUseStrongEncTypesByDefault) {
  JoinAndFetchDevicePolicy(kMachineName);
  validate_user_policy_ = &CheckUserPolicyEmpty;
  CheckKrb5EncTypes(paths_->Get(Path::DEVICE_KRB5_CONF), kKrb5EncTypesStrong);
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  CheckKrb5EncTypes(paths_->Get(Path::USER_KRB5_CONF), kKrb5EncTypesStrong);
}

// The password check runs when device policy is fetched.
TEST_F(AuthPolicyTest, ChecksMachinePasswordOnDevicePolicyFetch) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_FALSE(samba().DidPasswordChangeCheckRunForTesting());
  MarkDeviceAsLocked();
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_TRUE(samba().DidPasswordChangeCheckRunForTesting());
}

// The password check can be toggled by fetched device policy.
TEST_F(AuthPolicyTest, FetchedPolicyTogglesMachinePassword) {
  // Join domain.
  EXPECT_EQ(ERROR_NONE,
            Join(kOneGpoMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();
  validate_device_policy_ = &DontValidateDevicePolicy;

  // Turn off password change in policy.
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendInteger(policy::key::kDeviceMachinePasswordChangeRate, 0);
  writer.WriteToFile(stub_gpo1_path_);
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_FALSE(samba().DidPasswordChangeCheckRunForTesting());

  // Turn password change back on in policy.
  policy::PRegUserDevicePolicyWriter writer2;
  writer2.AppendInteger(policy::key::kDeviceMachinePasswordChangeRate, 1);
  writer2.WriteToFile(stub_gpo1_path_);
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_TRUE(samba().DidPasswordChangeCheckRunForTesting());
}

// The password check can be toggled by device policy loaded from disk.
TEST_F(AuthPolicyTest, PolicyOnDiskTogglesMachinePasswordChangeCheck) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();

  // Write a policy to disk that turns off password change and restart.
  em::ChromeDeviceSettingsProto device_policy;
  device_policy.mutable_device_machine_password_change_rate()->set_rate_days(0);
  WritePolicyAndRestartAuthpolicy(device_policy);
  EXPECT_FALSE(samba().DidPasswordChangeCheckRunForTesting());

  // Write a policy to disk that turns password change back on and restart.
  device_policy.mutable_device_machine_password_change_rate()->set_rate_days(1);
  WritePolicyAndRestartAuthpolicy(device_policy);
  EXPECT_TRUE(samba().DidPasswordChangeCheckRunForTesting());
}

// The password actually gets reset once it exceeds the max age.
TEST_F(AuthPolicyTest, MachinePasswordChangesWhenMaxAgeIsReached) {
  EXPECT_EQ(ERROR_NONE,
            Join(kChangePasswordMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();

  // Device policy fetch should trigger a password age check.
  // kChangePasswordMachineName should trigger a server time that's bigger than
  // initial_password_time + 30 days, so that the password should change.
  const std::string initial_password = ReadFile(Path::MACHINE_PASS);
  const base::Time initial_password_time = GetLastModified(Path::MACHINE_PASS);
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  FetchAndValidateDevicePolicy(ERROR_NONE);
  const std::string current_password = ReadFile(Path::MACHINE_PASS);
  const base::Time current_password_time = GetLastModified(Path::MACHINE_PASS);
  EXPECT_NE(initial_password, current_password);
  EXPECT_NE(initial_password_time, current_password_time);
  EXPECT_GE(current_password_time - initial_password_time,
            kDefaultMachinePasswordChangeRate);

  // Authpolicy should also keep the prev password around.
  const std::string previous_password = ReadFile(Path::PREV_MACHINE_PASS);
  const base::Time previous_password_time =
      GetLastModified(Path::PREV_MACHINE_PASS);
  EXPECT_EQ(initial_password, previous_password);
  EXPECT_EQ(initial_password_time, previous_password_time);
}

// If the current machine password has just been changed, it might not have
// propagated through Active Directory yet. In that case, kinit should fail and
// authpolicy should retry with the previous machine password.
TEST_F(AuthPolicyTest, DevicePolicyFetchUsesPrevMachinePassword) {
  JoinAndFetchDevicePolicy(kMachineName);

  // Create an expected password. stub_kinit will compare the expected password
  // with the actual password and cause device policy fetch to fail.
  const base::FilePath prev_password_path(paths_->Get(Path::PREV_MACHINE_PASS));
  const base::FilePath expected_password_path =
      base::FilePath(paths_->Get(Path::DEVICE_KRB5_CONF))
          .DirName()
          .Append(kExpectedMachinePassFilename);
  const std::string expected_password = GenerateRandomMachinePassword();
  brillo::WriteStringToFile(expected_password_path, expected_password);
  EXPECT_FALSE(base::PathExists(prev_password_path));
  FetchAndValidateDevicePolicy(ERROR_BAD_PASSWORD);

  // Write the expected password at PREV_MACHINE_PASS and verify fetch works.
  brillo::WriteStringToFile(prev_password_path, expected_password);
  FetchAndValidateDevicePolicy(ERROR_NONE);

  // kinit should be called 1x from 1st fetch, 1x from 2nd fetch and 2x from 3rd
  // fetch (for current and prev machine password).
  EXPECT_EQ(4, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
}

// The password check runs on startup on an enrolled device.
TEST_F(AuthPolicyTest, ChecksMachinePasswordOnStartup) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_FALSE(samba().DidPasswordChangeCheckRunForTesting());

  // Make password old enough for a password change.
  SetLastModified(Path::MACHINE_PASS, base::Time());

  // Restart with empty device policy. This should trigger it as well.
  samba().ResetForTesting();
  EXPECT_FALSE(samba().DidPasswordChangeCheckRunForTesting());
  EXPECT_EQ(ERROR_NONE, samba().Initialize(true /* expect_config */));
  EXPECT_TRUE(samba().DidPasswordChangeCheckRunForTesting());

  // Check that SMB conf contains KDC IP (regression test for crbug.com/815139).
  Krb5Conf conf;
  ReadKrb5Conf(paths_->Get(Path::DEVICE_KRB5_CONF), &conf);
  EXPECT_FALSE(conf.kdc.empty());
}

// Successful user authentication.
TEST_F(AuthPolicyTest, AuthSucceeds) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  EXPECT_EQ(1, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
}

// Successful user authentication with given account id.
TEST_F(AuthPolicyTest, AuthSucceedsWithKnownAccountId) {
  ActiveDirectoryAccountInfo account_info;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE,
            Auth(kUserPrincipal, kAccountId, MakePasswordFd(), &account_info));
  EXPECT_EQ(kAccountId, account_info.account_id());
}

// Program should die if trying to auth with different account ids.
TEST_F(AuthPolicyTest, AuthFailsDifferentAccountIds) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, kAccountId, MakePasswordFd()));
  EXPECT_DEATH((void)Auth(kUserPrincipal, kAltAccountId, MakePasswordFd()),
               kMultiUserNotSupported);
}

// User authentication fails with bad (non-existent) account id.
TEST_F(AuthPolicyTest, AuthFailsWithBadAccountId) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_BAD_USER_NAME,
            Auth(kUserPrincipal, kBadAccountId, MakePasswordFd()));
}

// Successful user authentication.
TEST_F(AuthPolicyTest, AuthSetsAccountInfo) {
  ActiveDirectoryAccountInfo account_info;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE,
            Auth(kUserPrincipal, "", MakePasswordFd(), &account_info));
  EXPECT_EQ(kAccountId, account_info.account_id());
  EXPECT_EQ(kDisplayName, account_info.display_name());
  EXPECT_EQ(kGivenName, account_info.given_name());
  EXPECT_EQ(kUserName, account_info.sam_account_name());
  EXPECT_EQ(kCommonName, account_info.common_name());
  EXPECT_EQ(kPwdLastSet, account_info.pwd_last_set());
  EXPECT_EQ(kUserAccountControl, account_info.user_account_control());
}

// Authentication fails for badly formatted user principal name.
TEST_F(AuthPolicyTest, AuthFailsInvalidUpn) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_PARSE_UPN_FAILED,
            Auth(kInvalidUserPrincipal, "", MakePasswordFd()));
}

// Authentication fails for non-existing user principal name.
TEST_F(AuthPolicyTest, AuthFailsBadUpn) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_BAD_USER_NAME,
            Auth(kNonExistingUserPrincipal, "", MakePasswordFd()));
}

// Authentication fails for wrong password.
TEST_F(AuthPolicyTest, AuthFailsBadPassword) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_BAD_PASSWORD,
            Auth(kUserPrincipal, "", MakeFileDescriptor(kWrongPassword)));
  // During PODs auth it is possible that the user gets into a session despite
  // the ERROR_BAD_PASSWORD error. This must mean that the password changed on
  // the server and the user entered the old password, which successfully
  // unlocked Cryptohome.
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, GetUserStatus(kUserPrincipal, kAccountId, &status));
  EXPECT_EQ(ActiveDirectoryUserStatus::PASSWORD_CHANGED,
            status.password_status());
}

// Authentication fails for expired password.
TEST_F(AuthPolicyTest, AuthFailsExpiredPassword) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_PASSWORD_EXPIRED,
            Auth(kUserPrincipal, "", MakeFileDescriptor(kExpiredPassword)));
}

// Authentication fails for rejected password.
TEST_F(AuthPolicyTest, AuthFailsRejectedPassword) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_PASSWORD_REJECTED,
            Auth(kUserPrincipal, "", MakeFileDescriptor(kRejectedPassword)));
}

// Authentication succeeds if the "password will expire" warning is shown.
TEST_F(AuthPolicyTest, AuthSucceedsPasswordWillExpire) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE,
            Auth(kUserPrincipal, "", MakeFileDescriptor(kWillExpirePassword)));
}

// Authentication fails if there's a network issue.
TEST_F(AuthPolicyTest, AuthFailsNetworkProblem) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NETWORK_PROBLEM,
            Auth(kNetworkErrorUserPrincipal, "", MakePasswordFd()));
}

// Authentication fails with unsupported encryption type.
TEST_F(AuthPolicyTest, AuthFailsEncTypeNotSupported) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_KDC_DOES_NOT_SUPPORT_ENCRYPTION_TYPE,
            Auth(kEncTypeNotSupportedUserPrincipal, "", MakePasswordFd()));
}

// Authentication retries without KDC if it fails the first time.
TEST_F(AuthPolicyTest, AuthSucceedsKdcRetry) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kKdcRetryUserPrincipal, "", MakePasswordFd()));
  EXPECT_EQ(2, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
}

// Can't get user status before domain join.
TEST_F(AuthPolicyTest, GetUserStatusFailsNotJoined) {
  EXPECT_EQ(ERROR_NOT_JOINED, GetUserStatus(kUserPrincipal, kAccountId));
  EXPECT_EQ(0, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
}

// Program should die if trying to get user status with different account ids
// than what was used for auth.
TEST_F(AuthPolicyTest, GetUserStatusFailsDifferentAccountId) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, kAccountId, MakePasswordFd()));
  EXPECT_DEATH((void)GetUserStatus(kUserPrincipal, kAltAccountId),
               kMultiUserNotSupported);
}

// GetUserStatus succeeds without auth, reporting TGT_NOT_FOUND and lacking
// account info and password status.
TEST_F(AuthPolicyTest, GetUserStatusSucceedsTgtNotFound) {
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, GetUserStatus(kUserPrincipal, kAccountId, &status));
  EXPECT_EQ(ActiveDirectoryUserStatus::TGT_NOT_FOUND, status.tgt_status());
  EXPECT_FALSE(status.has_account_info());
  EXPECT_FALSE(status.has_password_status());
}

// GetUserStatus succeeds with join and auth, but with an expired TGT and
// available server.
TEST_F(AuthPolicyTest, GetUserStatusSucceedsTgtExpiredServerAvailable) {
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kExpiredTgtUserPrincipal, "", MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, GetUserStatus(kUserPrincipal, kAccountId, &status));
  EXPECT_EQ(ActiveDirectoryUserStatus::TGT_EXPIRED, status.tgt_status());
}

// GetUserStatus succeeds with join and auth, but with an expired TGT and
// unavailable server.
TEST_F(AuthPolicyTest, GetUserStatusFailsTgtExpiredServerUnavailable) {
  EXPECT_EQ(ERROR_NONE,
            Join(kPingServerFailMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kExpiredTgtUserPrincipal, "", MakePasswordFd()));
  EXPECT_EQ(ERROR_NETWORK_PROBLEM, GetUserStatus(kUserPrincipal, kAccountId));
}

// GetUserStatus succeeds with join and auth.
TEST_F(AuthPolicyTest, GetUserStatusSucceeds) {
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  EXPECT_TRUE(authpolicy_->IsUserTgtAutoRenewalEnabledForTesting());
  EXPECT_EQ(ERROR_NONE, GetUserStatus(kUserPrincipal, kAccountId, &status));

  ActiveDirectoryUserStatus expected_status;
  ActiveDirectoryAccountInfo& expected_account_info =
      *expected_status.mutable_account_info();
  expected_account_info.set_account_id(kAccountId);
  expected_account_info.set_display_name(kDisplayName);
  expected_account_info.set_given_name(kGivenName);
  expected_account_info.set_sam_account_name(kUserName);
  expected_account_info.set_common_name(kCommonName);
  expected_account_info.set_pwd_last_set(kPwdLastSet);
  expected_account_info.set_user_account_control(kUserAccountControl);
  expected_status.set_tgt_status(ActiveDirectoryUserStatus::TGT_VALID);
  expected_status.set_password_status(
      ActiveDirectoryUserStatus::PASSWORD_VALID);

  // Note that protobuf equality comparison is not supported.
  std::string status_blob, expected_status_blob;
  EXPECT_TRUE(status.SerializeToString(&status_blob));
  EXPECT_TRUE(expected_status.SerializeToString(&expected_status_blob));
  EXPECT_EQ(expected_status_blob, status_blob);

  EXPECT_EQ(1, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
}

// GetUserStatus actually contains the last auth error.
TEST_F(AuthPolicyTest, GetUserStatusReportsLastAuthError) {
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_PASSWORD_EXPIRED,
            Auth(kUserPrincipal, "", MakeFileDescriptor(kExpiredPassword)));
  EXPECT_EQ(ERROR_NONE, GetUserStatus(kUserPrincipal, kAccountId, &status));
  EXPECT_EQ(ActiveDirectoryUserStatus::PASSWORD_EXPIRED,
            status.password_status());
}

// GetUserStatus reports to expire the password.
TEST_F(AuthPolicyTest, GetUserStatusReportsExpiredPasswords) {
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE,
            Auth(kUserPrincipal, kExpiredPasswordAccountId, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE,
            GetUserStatus(kUserPrincipal, kExpiredPasswordAccountId, &status));
  EXPECT_EQ(ActiveDirectoryUserStatus::PASSWORD_EXPIRED,
            status.password_status());
}

// GetUserStatus does not report expired passwords if UF_DONT_EXPIRE_PASSWD is
// set.
TEST_F(AuthPolicyTest, GetUserStatusDontReportNeverExpirePasswords) {
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, kNeverExpirePasswordAccountId,
                             MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, GetUserStatus(kUserPrincipal,
                                      kNeverExpirePasswordAccountId, &status));
  EXPECT_EQ(ActiveDirectoryUserStatus::PASSWORD_VALID,
            status.password_status());
}

// GetUserStatus reports password changes.
TEST_F(AuthPolicyTest, GetUserStatusReportChangedPasswords) {
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE,
            Auth(kPasswordChangedUserPrincipal, "", MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE,
            GetUserStatus(kUserPrincipal, kPasswordChangedAccountId, &status));
  EXPECT_EQ(ActiveDirectoryUserStatus::PASSWORD_CHANGED,
            status.password_status());
}

// GetUserStatus reports valid password if the LDAP attributes pwdLastSet or
// userAccountControl are missing for some reason.
TEST_F(AuthPolicyTest, GetUserStatusReportValidPasswordsWithoutPwdFields) {
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kNoPwdFieldsUserPrincipal, "", MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE,
            GetUserStatus(kUserPrincipal, kNoPwdFieldsAccountId, &status));
  EXPECT_EQ(ActiveDirectoryUserStatus::PASSWORD_VALID,
            status.password_status());
  EXPECT_FALSE(status.account_info().has_pwd_last_set());
  EXPECT_FALSE(status.account_info().has_user_account_control());
  EXPECT_FALSE(authpolicy_->IsUserTgtAutoRenewalEnabledForTesting());
}

// GetUserKerberosFiles succeeds with empty files if not joined.
TEST_F(AuthPolicyTest, GetUserKerberosFilesEmptyNotJoined) {
  KerberosFiles files;
  EXPECT_EQ(ERROR_NONE, GetUserKerberosFiles(kAccountId, &files));
  EXPECT_EQ(0, user_kerberos_files_changed_count_);
  EXPECT_FALSE(files.has_krb5cc());
  EXPECT_FALSE(files.has_krb5conf());
}

// GetUserKerberosFiles succeeds with empty files if not logged in.
TEST_F(AuthPolicyTest, GetUserKerberosFilesEmptyNotLoggedIn) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  KerberosFiles files;
  EXPECT_EQ(ERROR_NONE, GetUserKerberosFiles(kAccountId, &files));
  EXPECT_EQ(0, user_kerberos_files_changed_count_);
  EXPECT_FALSE(files.has_krb5cc());
  EXPECT_FALSE(files.has_krb5conf());
}

// Authenticating with different id after GetUserKerberosFiles dies.
TEST_F(AuthPolicyTest, GetUserKerberosFilesBeforeAuthWithAltIdDies) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, GetUserKerberosFiles(kAccountId));
  EXPECT_DEATH((void)Auth(kUserPrincipal, kAltAccountId, MakePasswordFd()),
               kMultiUserNotSupported);
}

// GetUserKerberosFiles succeeds with actual files if logged in.
TEST_F(AuthPolicyTest, GetUserKerberosFilesSucceeds) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  KerberosFiles files;
  EXPECT_EQ(ERROR_NONE, GetUserKerberosFiles(kAccountId, &files));
  EXPECT_EQ(1, user_kerberos_files_changed_count_);
  EXPECT_TRUE(files.has_krb5cc());
  EXPECT_TRUE(files.has_krb5conf());
  EXPECT_EQ(kValidKrb5CCData, files.krb5cc());
  EXPECT_NE(std::string::npos, files.krb5conf().find("allow_weak_crypto"));
}

// Changes of krb5.conf should trigger the UserKerberosFilesChanged signal. This
// is tested by retrying kinit without kdc ip causes a config change and should
// result in a UserKerberosFilesChanged signal.
TEST_F(AuthPolicyTest, ConfigChangeTriggersFilesChangedSignal) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  // Do a normal auth first to bootstrap Kerberos files, but generate an expired
  // TGT, so that the last step won't change the TGT.
  EXPECT_EQ(ERROR_NONE, Auth(kExpiredTgtUserPrincipal, "", MakePasswordFd()));
  EXPECT_EQ(1, user_kerberos_files_changed_count_);
  // 1x user TGT.
  EXPECT_EQ(1, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));

  // Try to auth again, but trigger a KDC retry to change JUST the config.
  EXPECT_EQ(ERROR_CONTACTING_KDC_FAILED,
            Auth(kKdcRetryFailsUserPrincipal, "", MakePasswordFd()));
  EXPECT_EQ(2, user_kerberos_files_changed_count_);
  // 2x user TGT because of KDC retry.
  EXPECT_EQ(2, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));

  // Once the config is changed, it shouldn't change again.
  EXPECT_EQ(ERROR_KERBEROS_TICKET_EXPIRED, samba().RenewUserTgtForTesting());
  EXPECT_EQ(2, user_kerberos_files_changed_count_);
  // 1x user TGT.
  EXPECT_EQ(1, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
}

// TGT renewal should trigger a KerberosFilesChanged signal.
TEST_F(AuthPolicyTest, RenewTriggersFilesChangedSignal) {
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  EXPECT_EQ(1, user_kerberos_files_changed_count_);
  EXPECT_EQ(ERROR_NONE, samba().RenewUserTgtForTesting());
  EXPECT_EQ(2, user_kerberos_files_changed_count_);
}

// Join fails if there's a network issue.
TEST_F(AuthPolicyTest, JoinFailsNetworkProblem) {
  EXPECT_EQ(ERROR_NETWORK_PROBLEM,
            Join(kMachineName, kNetworkErrorUserPrincipal, MakePasswordFd()));
}

// Join fails for badly formatted user principal name.
TEST_F(AuthPolicyTest, JoinFailsInvalidUpn) {
  EXPECT_EQ(ERROR_PARSE_UPN_FAILED,
            Join(kMachineName, kInvalidUserPrincipal, MakePasswordFd()));
}

// Join fails for non-existing user principal name, but the error message is the
// same as for wrong password.
TEST_F(AuthPolicyTest, JoinFailsBadUpn) {
  EXPECT_EQ(ERROR_BAD_PASSWORD,
            Join(kMachineName, kNonExistingUserPrincipal, MakePasswordFd()));
}

// Join fails for wrong password.
TEST_F(AuthPolicyTest, JoinFailsBadPassword) {
  EXPECT_EQ(ERROR_BAD_PASSWORD, Join(kMachineName, kUserPrincipal,
                                     MakeFileDescriptor(kWrongPassword)));
}

// Join fails with expired password.
TEST_F(AuthPolicyTest, JoinFailsPasswordExpired) {
  EXPECT_EQ(ERROR_PASSWORD_EXPIRED, Join(kMachineName, kUserPrincipal,
                                         MakeFileDescriptor(kExpiredPassword)));
}

// Join fails if user can't join a machine to the domain.
TEST_F(AuthPolicyTest, JoinFailsAccessDenied) {
  EXPECT_EQ(ERROR_JOIN_ACCESS_DENIED,
            Join(kMachineName, kAccessDeniedUserPrincipal, MakePasswordFd()));
}

// Join fails if the machine name is too long.
TEST_F(AuthPolicyTest, JoinFailsMachineNameTooLong) {
  EXPECT_EQ(ERROR_MACHINE_NAME_TOO_LONG,
            Join(kTooLongMachineName, kUserPrincipal, MakePasswordFd()));
}

// Join fails if the machine name contains invalid characters.
TEST_F(AuthPolicyTest, JoinFailsInvalidMachineName) {
  EXPECT_EQ(ERROR_INVALID_MACHINE_NAME,
            Join(kInvalidMachineName, kUserPrincipal, MakePasswordFd()));
}

// Join fails if the user can't join additional machines.
TEST_F(AuthPolicyTest, JoinFailsInsufficientQuota) {
  EXPECT_EQ(
      ERROR_USER_HIT_JOIN_QUOTA,
      Join(kMachineName, kInsufficientQuotaUserPrincipal, MakePasswordFd()));
}

// Join fails with unsupported encryption type.
TEST_F(AuthPolicyTest, JoinFailsEncTypeNotSupported) {
  EXPECT_EQ(
      ERROR_KDC_DOES_NOT_SUPPORT_ENCRYPTION_TYPE,
      Join(kMachineName, kEncTypeNotSupportedUserPrincipal, MakePasswordFd()));
}

// A second domain join is blocked.
TEST_F(AuthPolicyTest, JoinFailsAlreadyJoined) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_ALREADY_JOINED,
            Join(kMachineName, kUserPrincipal, MakePasswordFd()));
}

// Successful user policy fetch with empty policy.
TEST_F(AuthPolicyTest, UserPolicyFetchSucceeds) {
  validate_user_policy_ = &CheckUserPolicyEmpty;
  JoinAndFetchDevicePolicy(kMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
  EXPECT_EQ(2, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
  EXPECT_EQ(2, metrics_->GetNumMetricReports(METRIC_DOWNLOAD_GPO_COUNT));
}

// For affiliated users, the affiliation marker should be set during user policy
// fetch.
TEST_F(AuthPolicyTest, AffiliationMarkerSetForAffiliatedUsers) {
  validate_user_policy_ = &CheckUserPolicyEmpty;
  JoinAndFetchDevicePolicy(kMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
  EXPECT_TRUE(user_affiliation_marker_set_);
}

// For unaffiliated users, the affiliation marker should not be set during user
// policy fetch.
TEST_F(AuthPolicyTest, AffiliationMarkerNotSetForUnaffiliatedUsers) {
  // Users in the same realm as the device are always affiliated, so pick a
  // different realm.
  JoinDomainRequest request;
  request.set_machine_name(kUnaffiliatedMachineName);
  request.set_machine_domain(kMachineRealm);
  request.set_user_principal_name(kUserPrincipal);
  EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd()));
  MarkDeviceAsLocked();
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  FetchAndValidateDevicePolicy(ERROR_NONE);

  validate_user_policy_ = &CheckUserPolicyEmpty;
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
  EXPECT_FALSE(user_affiliation_marker_set_);
}

// Successful user policy fetch with actual data.
TEST_F(AuthPolicyTest, UserPolicyFetchSucceedsWithData) {
  // Write a preg file with all basic data types. The file is picked up by
  // stub_net and "downloaded" by stub_smbclient.
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendBoolean(policy::key::kSearchSuggestEnabled, kPolicyBool);
  writer.AppendInteger(policy::key::kPolicyRefreshRate, kPolicyInt);
  writer.AppendString(policy::key::kHomepageLocation, kPolicyStr);
  const std::vector<std::string> apps = {"App1", "App2"};
  writer.AppendStringList(policy::key::kPinnedLauncherApps, apps);
  writer.WriteToFile(stub_gpo1_path_);

  // Validate that the protobufs sent from authpolicy to Session Manager
  // actually contain the policies set above. This validator is called by
  // FetchAndValidateUserPolicy below.
  validate_user_policy_ = [apps](const em::CloudPolicySettings& policy) {
    EXPECT_EQ(kPolicyBool, policy.searchsuggestenabled().value());
    EXPECT_EQ(kPolicyInt, policy.policyrefreshrate().value());
    EXPECT_EQ(kPolicyStr, policy.homepagelocation().value());
    const em::StringList& apps_proto = policy.pinnedlauncherapps().value();
    EXPECT_EQ(apps_proto.entries_size(), static_cast<int>(apps.size()));
    for (int n = 0; n < apps_proto.entries_size(); ++n)
      EXPECT_EQ(apps_proto.entries(n), apps.at(n));
  };
  JoinAndFetchDevicePolicy(kOneGpoMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
  EXPECT_EQ(2,
            metrics_->GetNumMetricReports(METRIC_SMBCLIENT_FAILED_TRY_COUNT));
}

// Successful user policy fetch that also contains extension policy.
TEST_F(AuthPolicyTest, UserPolicyFetchSucceedsWithDataAndExtensions) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  policy::PRegPolicyWriter writer;
  writer.SetKeysForUserDevicePolicy();
  writer.AppendBoolean(policy::key::kSearchSuggestEnabled, kPolicyBool);
  WriteDefaultExtensionPolicy(&writer);
  writer.WriteToFile(stub_gpo1_path_);

  validate_user_policy_ = [](const em::CloudPolicySettings& policy) {
    EXPECT_EQ(kPolicyBool, policy.searchsuggestenabled().value());
  };
  validate_extension_policy_ = &CheckDefaultExtensionPolicy;
  JoinAndFetchDevicePolicy(kOneGpoMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
  EXPECT_EQ(2, validated_extension_ids_.size());
}

// Successful user policy fetch that also contains extension policy.
TEST_F(AuthPolicyTest, StaleExtensionPoliciesAreDeleted) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  policy::PRegPolicyWriter writer;
  writer.SetKeysForExtensionPolicy(kExtensionId);
  writer.AppendString(kExtensionPolicy1, kPolicyStr);
  writer.WriteToFile(stub_gpo1_path_);

  // Pretend that Session Manager has stored policy for these two extensions.
  stored_extension_ids_.push_back(kExtensionId);
  stored_extension_ids_.push_back(kOtherExtensionId);

  // Fetch and validate. This should trigger policy kOtherExtensionId to be
  // deleted because the GPO only contains kExtensionId.
  validate_user_policy_ = &CheckUserPolicyEmpty;
  validate_extension_policy_ = &CheckDefaultExtensionPolicy;
  JoinAndFetchDevicePolicy(kOneGpoMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
  EXPECT_EQ(validated_extension_ids_,
            std::multiset<std::string>({kExtensionId}));
  EXPECT_EQ(deleted_extension_ids_,
            std::multiset<std::string>({kOtherExtensionId}));
}

// Verify that PolicyLevel is encoded properly.
TEST_F(AuthPolicyTest, UserPolicyFetchSucceedsWithPolicyLevel) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendBoolean(policy::key::kSearchSuggestEnabled, kPolicyBool,
                       policy::POLICY_LEVEL_RECOMMENDED);
  writer.AppendInteger(policy::key::kPolicyRefreshRate, kPolicyInt);
  writer.WriteToFile(stub_gpo1_path_);

  validate_user_policy_ = [](const em::CloudPolicySettings& policy) {
    EXPECT_TRUE(policy.searchsuggestenabled().has_policy_options());
    EXPECT_EQ(em::PolicyOptions_PolicyMode_RECOMMENDED,
              policy.searchsuggestenabled().policy_options().mode());

    EXPECT_TRUE(policy.policyrefreshrate().has_policy_options());
    EXPECT_EQ(em::PolicyOptions_PolicyMode_MANDATORY,
              policy.policyrefreshrate().policy_options().mode());
  };
  JoinAndFetchDevicePolicy(kOneGpoMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
}

// Verifies that a POLICY_LEVEL_MANDATORY policy is not overwritten by a
// POLICY_LEVEL_RECOMMENDED policy.
TEST_F(AuthPolicyTest, UserPolicyFetchMandatoryTakesPreference) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  policy::PRegUserDevicePolicyWriter writer1;
  writer1.AppendBoolean(policy::key::kSearchSuggestEnabled, kPolicyBool,
                        policy::POLICY_LEVEL_MANDATORY);
  writer1.WriteToFile(stub_gpo1_path_);

  // Normally, the latter GPO file overrides the former
  // (DevicePolicyFetchGposOverride), but POLICY_LEVEL_RECOMMENDED does not
  // beat POLICY_LEVEL_MANDATORY.
  policy::PRegUserDevicePolicyWriter writer2;
  writer2.AppendBoolean(policy::key::kSearchSuggestEnabled, kOtherPolicyBool,
                        policy::POLICY_LEVEL_RECOMMENDED);
  writer2.WriteToFile(stub_gpo2_path_);

  validate_user_policy_ = [](const em::CloudPolicySettings& policy) {
    EXPECT_TRUE(policy.searchsuggestenabled().has_value());
    EXPECT_EQ(kPolicyBool, policy.searchsuggestenabled().value());
    EXPECT_TRUE(policy.searchsuggestenabled().has_policy_options());
    EXPECT_EQ(em::PolicyOptions_PolicyMode_MANDATORY,
              policy.searchsuggestenabled().policy_options().mode());
  };
  JoinAndFetchDevicePolicy(kTwoGposMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
}

// Verify that GPO containing policies with the wrong data type are not set.
// An exception is bool and int. Internally, bools are handled like ints with
// value {0,1}, so that setting kPolicyRefreshRate to true is actually
// interpreted as int 1.
TEST_F(AuthPolicyTest, UserPolicyFetchIgnoreBadDataType) {
  // Set policies with wrong data type, e.g. kPinnedLauncherApps is a string
  // list, but it is set as a string. See UserPolicyFetchSucceedsWithData for
  // the logic of policy testing.
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendBoolean(policy::key::kPolicyRefreshRate, kPolicyBool);
  writer.AppendInteger(policy::key::kHomepageLocation, kPolicyInt);
  writer.AppendString(policy::key::kPinnedLauncherApps, kPolicyStr);
  const std::vector<std::string> apps = {"App1", "App2"};
  writer.AppendStringList(policy::key::kSearchSuggestEnabled, apps);
  writer.WriteToFile(stub_gpo1_path_);

  validate_user_policy_ = [](const em::CloudPolicySettings& policy) {
    EXPECT_FALSE(policy.has_searchsuggestenabled());
    EXPECT_FALSE(policy.has_pinnedlauncherapps());
    EXPECT_FALSE(policy.has_homepagelocation());
    EXPECT_TRUE(policy.has_policyrefreshrate());  // Interpreted as int 1.
  };
  JoinAndFetchDevicePolicy(kOneGpoMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
}

// GPOs with version 0 should be ignored.
TEST_F(AuthPolicyTest, UserPolicyFetchIgnoreZeroVersion) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendBoolean(policy::key::kSearchSuggestEnabled, kPolicyBool);
  writer.WriteToFile(stub_gpo1_path_);

  validate_user_policy_ = [](const em::CloudPolicySettings& policy) {
    EXPECT_FALSE(policy.has_searchsuggestenabled());
  };
  JoinAndFetchDevicePolicy(kZeroUserVersionMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);

  // Validate the validation. GPO is actually taken if user version is > 0.
  validate_user_policy_ = [](const em::CloudPolicySettings& policy) {
    EXPECT_TRUE(policy.has_searchsuggestenabled());
  };
  EXPECT_TRUE(MakeConfigWriteable());
  samba().ResetForTesting();
  JoinAndFetchDevicePolicy(kOneGpoMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
}

// GPOs with an ignore flag set should be ignored. Sounds reasonable, hmm?
TEST_F(AuthPolicyTest, UserPolicyFetchIgnoreFlagSet) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendBoolean(policy::key::kSearchSuggestEnabled, kPolicyBool);
  writer.WriteToFile(stub_gpo1_path_);

  validate_user_policy_ = [](const em::CloudPolicySettings& policy) {
    EXPECT_FALSE(policy.has_searchsuggestenabled());
  };
  JoinAndFetchDevicePolicy(kDisableUserFlagMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);

  // Validate the validation. GPO is taken if the ignore flag is not set.
  validate_user_policy_ = [](const em::CloudPolicySettings& policy) {
    EXPECT_TRUE(policy.has_searchsuggestenabled());
  };
  EXPECT_TRUE(MakeConfigWriteable());
  samba().ResetForTesting();
  JoinAndFetchDevicePolicy(kOneGpoMachineName);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
}

// User policy fetch fails if there's no device policy (since the way user
// policy is fetched depends on device policy).
TEST_F(AuthPolicyTest, UserPolicyFetchFailsNoDevicePolicy) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NO_DEVICE_POLICY);
}

// User policy fetch works properly with loopback processing.
TEST_F(AuthPolicyTest, UserPolicyFetchObeysLoopbackProcessing) {
  // Write 2 GPO files with 2 policies each:
  //
  //   GPO  Policy 1 2 3
  //   GPO1        x y
  //   GPO2        A   B
  //
  // 'x' is the value of policy 1 in GPO1 etc. Now for the test,
  //   GPO1 is used as user GPO and
  //   GPO2 is used as device GPO.
  //
  // Depending on the loopback processing mode, this should result in
  //
  //   Mode    Policy 1 2 3
  //   Default        x y    <-- Only take user GPO1
  //   Merge          A y B  <-- Merge device GPO2 on top of user GPO1
  //   Replace        A   B  <-- Only take device GPO2

  const char* policy1 = policy::key::kSearchSuggestEnabled;
  const char* policy2 = policy::key::kPolicyRefreshRate;
  const char* policy3 = policy::key::kHomepageLocation;

  const bool value_x = kPolicyBool;
  const int value_y = kPolicyInt;
  const bool value_A = kOtherPolicyBool;
  const char* value_B = kOtherPolicyStr;

  policy::PRegUserDevicePolicyWriter writer1;
  writer1.AppendBoolean(policy1, value_x);
  writer1.AppendInteger(policy2, value_y);
  writer1.WriteToFile(stub_gpo1_path_);

  policy::PRegUserDevicePolicyWriter writer2;
  writer2.AppendBoolean(policy1, value_A);
  writer2.AppendString(policy3, value_B);
  writer2.WriteToFile(stub_gpo2_path_);

  // |kLoopbackGpoMachineName| triggers stub_net to
  //   - return GPO1 for net ads gpo list <user_principal> and
  //   - return GPO2 for net ads gpo list <device_principal>.
  JoinAndFetchDevicePolicy(kLoopbackGpoMachineName);

  const int mode_min =
      em::DeviceUserPolicyLoopbackProcessingModeProto::Mode_MIN;
  const int mode_max =
      em::DeviceUserPolicyLoopbackProcessingModeProto::Mode_MAX;
  for (int int_mode = mode_min; int_mode <= mode_max; ++int_mode) {
    const auto mode =
        static_cast<em::DeviceUserPolicyLoopbackProcessingModeProto::Mode>(
            int_mode);
    samba().SetUserPolicyModeForTesting(mode);

    validate_user_policy_ = [value_x, value_y, value_A, value_B,
                             mode](const em::CloudPolicySettings& policy) {
      const bool has_policy1 = policy.has_searchsuggestenabled();
      const bool has_policy2 = policy.has_policyrefreshrate();
      const bool has_policy3 = policy.has_homepagelocation();

      const bool policy1_value = policy.searchsuggestenabled().value();
      const int policy2_value = policy.policyrefreshrate().value();
      const std::string& policy3_value = policy.homepagelocation().value();

      EXPECT_TRUE(has_policy1);
      switch (mode) {
        case em::DeviceUserPolicyLoopbackProcessingModeProto::
            USER_POLICY_MODE_DEFAULT:
          EXPECT_TRUE(has_policy2);
          EXPECT_FALSE(has_policy3);
          EXPECT_EQ(value_x, policy1_value);
          EXPECT_EQ(value_y, policy2_value);
          break;

        case em::DeviceUserPolicyLoopbackProcessingModeProto::
            USER_POLICY_MODE_MERGE:
          EXPECT_TRUE(has_policy2);
          EXPECT_TRUE(has_policy3);
          EXPECT_EQ(value_A, policy1_value);
          EXPECT_EQ(value_y, policy2_value);
          EXPECT_EQ(value_B, policy3_value);
          break;

        case em::DeviceUserPolicyLoopbackProcessingModeProto::
            USER_POLICY_MODE_REPLACE:
          EXPECT_FALSE(has_policy2);
          EXPECT_TRUE(has_policy3);
          EXPECT_EQ(value_A, policy1_value);
          EXPECT_EQ(value_B, policy3_value);
          break;
      }
    };
    FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
  }
  // 3x for user TGT during auth, 1 for device policy fetch, 2x for device TGT
  // for MERGE and REPLACE.
  EXPECT_EQ(6, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
  // 1x for device policy fetch, 1x for DEFAULT, 2x for MERGE, 1x for REPLACE.
  EXPECT_EQ(5,
            metrics_->GetNumMetricReports(METRIC_SMBCLIENT_FAILED_TRY_COUNT));
  // 1x for device policy fetch, 1x for DEFAULT, 2x for MERGE, 1x for REPLACE.
  EXPECT_EQ(5, metrics_->GetNumMetricReports(METRIC_DOWNLOAD_GPO_COUNT));
}

// Successful device policy fetch with empty policy.
TEST_F(AuthPolicyTest, DevicePolicyFetchSucceeds) {
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(1, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
  EXPECT_EQ(1, metrics_->GetNumMetricReports(METRIC_DOWNLOAD_GPO_COUNT));
}

// Device policy fetch fails if the machine account doesn't exist.
TEST_F(AuthPolicyTest, DevicePolicyFetchFailsBadMachineName) {
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  EXPECT_EQ(ERROR_NONE,
            Join(kNonExistingMachineName, kUserPrincipal, MakePasswordFd()));
  FetchAndValidateDevicePolicy(ERROR_BAD_MACHINE_NAME);
  EXPECT_EQ(1, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
  EXPECT_EQ(0, metrics_->GetNumMetricReports(METRIC_DOWNLOAD_GPO_COUNT));
}

// Policy fetch should ignore GPO files that are missing on the server. This
// test looks for stub_gpo1_path_ and won't find it because we don't create it.
TEST_F(AuthPolicyTest, DevicePolicyFetchSucceedsMissingFile) {
  validate_user_policy_ = &CheckUserPolicyEmpty;
  JoinAndFetchDevicePolicy(kOneGpoMachineName);
}

// Successful device policy fetch with keytab file. This tests backwards
// compatibility with old clients that used a keytab file instead of a password.
TEST_F(AuthPolicyTest, DevicePolicyFetchSucceedsWithKeytab) {
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  EXPECT_EQ(ERROR_NONE,
            Join(kExpectKeytabMachineName, kUserPrincipal, MakePasswordFd()));

  // Replace the machine password by a keytab file. Authpolicy should use that
  // instead.
  const base::FilePath password_path(paths_->Get(Path::MACHINE_PASS));
  const base::FilePath keytab_path(paths_->Get(Path::MACHINE_KEYTAB));
  EXPECT_TRUE(base::Move(password_path, keytab_path));

  MarkDeviceAsLocked();
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(1, metrics_->GetNumMetricReports(METRIC_KINIT_FAILED_TRY_COUNT));
  EXPECT_EQ(1, metrics_->GetNumMetricReports(METRIC_DOWNLOAD_GPO_COUNT));
}

// Policy fetch fails if a file fails to download (unless it's missing, see
// DevicePolicyFetchSucceedsMissingFile).
TEST_F(AuthPolicyTest, DevicePolicyFetchFailsDownloadError) {
  EXPECT_EQ(ERROR_NONE, Join(kGpoDownloadErrorMachineName, kUserPrincipal,
                             MakePasswordFd()));
  FetchAndValidateDevicePolicy(ERROR_SMBCLIENT_FAILED);
}

// Successful device policy fetch with a few kinit retries because the machine
// account hasn't propagated yet.
TEST_F(AuthPolicyTest, DevicePolicyFetchSucceedsPropagationRetry) {
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  EXPECT_EQ(ERROR_NONE, Join(kPropagationRetryMachineName, kUserPrincipal,
                             MakePasswordFd()));
  MarkDeviceAsLocked();
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(kNumPropagationRetries,
            metrics_->GetLastMetricSample(METRIC_KINIT_FAILED_TRY_COUNT));
}

// Successful device policy fetch with actual data.
TEST_F(AuthPolicyTest, DevicePolicyFetchSucceedsWithData) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  SetupDeviceOneGpo(stub_gpo1_path_);
  EXPECT_EQ(ERROR_NONE,
            Join(kOneGpoMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();
  FetchAndValidateDevicePolicy(ERROR_NONE);
}

// Authpolicy caches device policy when device is not locked.
TEST_F(AuthPolicyTest, CachesDevicePolicyWhenDeviceIsNotLocked) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  SetupDeviceOneGpo(stub_gpo1_path_);
  EXPECT_EQ(ERROR_NONE,
            Join(kOneGpoMachineName, kUserPrincipal, MakePasswordFd()));
  FetchAndValidateDevicePolicy(ERROR_DEVICE_POLICY_CACHED_BUT_NOT_SENT);
  EXPECT_TRUE(base::DeleteFile(stub_gpo1_path_, /* recursive */ false));
  MarkDeviceAsLocked();
  FetchAndValidateDevicePolicy(ERROR_NONE);
}

// Successful device policy fetch that also contains extension policy.
TEST_F(AuthPolicyTest, DevicePolicyFetchSucceedsWithDataAndExtensions) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  policy::PRegPolicyWriter writer;
  writer.SetKeysForUserDevicePolicy();
  writer.AppendBoolean(policy::key::kDeviceGuestModeEnabled, kPolicyBool);
  WriteDefaultExtensionPolicy(&writer);
  writer.WriteToFile(stub_gpo1_path_);

  validate_device_policy_ = [](const em::ChromeDeviceSettingsProto& policy) {
    EXPECT_EQ(kPolicyBool, policy.guest_mode_enabled().guest_mode_enabled());
  };
  validate_extension_policy_ = &CheckDefaultExtensionPolicy;
  EXPECT_EQ(ERROR_NONE,
            Join(kOneGpoMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(2, validated_extension_ids_.size());
}

// Completely empty GPO list fails. GPO lists should always contain at least
// a "local policy" created by the Samba tool, independently of the server.
TEST_F(AuthPolicyTest, DevicePolicyFetchFailsEmptyGpoList) {
  EXPECT_EQ(ERROR_NONE,
            Join(kEmptyGpoMachineName, kUserPrincipal, MakePasswordFd()));
  FetchAndValidateDevicePolicy(ERROR_PARSE_FAILED);
}

// A GPO later in the list overrides prior GPOs.
TEST_F(AuthPolicyTest, DevicePolicyFetchGposOverride) {
  // See UserPolicyFetchSucceedsWithData for the logic of policy testing.
  policy::PRegUserDevicePolicyWriter writer1;
  writer1.AppendBoolean(policy::key::kDeviceGuestModeEnabled, kOtherPolicyBool);
  writer1.AppendInteger(policy::key::kDevicePolicyRefreshRate, kPolicyInt);
  writer1.AppendString(policy::key::kSystemTimezone, kPolicyStr);
  const std::vector<std::string> str_list1 = {"str1", "str2", "str3"};
  writer1.AppendStringList(policy::key::kDeviceUserWhitelist, str_list1);
  writer1.WriteToFile(stub_gpo1_path_);

  policy::PRegUserDevicePolicyWriter writer2;
  writer2.AppendBoolean(policy::key::kDeviceGuestModeEnabled, kPolicyBool);
  writer2.AppendInteger(policy::key::kDevicePolicyRefreshRate, kOtherPolicyInt);
  writer2.AppendString(policy::key::kSystemTimezone, kOtherPolicyStr);
  const std::vector<std::string> str_list2 = {"str4", "str5"};
  writer2.AppendStringList(policy::key::kDeviceUserWhitelist, str_list2);
  writer2.WriteToFile(stub_gpo2_path_);

  validate_device_policy_ = [str_list2](
                                const em::ChromeDeviceSettingsProto& policy) {
    EXPECT_EQ(kPolicyBool, policy.guest_mode_enabled().guest_mode_enabled());
    EXPECT_EQ(kOtherPolicyInt,
              policy.device_policy_refresh_rate().device_policy_refresh_rate());
    EXPECT_EQ(kOtherPolicyStr, policy.system_timezone().timezone());
    const em::UserWhitelistProto& str_list_proto = policy.user_whitelist();
    EXPECT_EQ(str_list_proto.user_whitelist_size(),
              static_cast<int>(str_list2.size()));
    for (int n = 0; n < str_list_proto.user_whitelist_size(); ++n)
      EXPECT_EQ(str_list_proto.user_whitelist(n), str_list2.at(n));
  };
  EXPECT_EQ(ERROR_NONE,
            Join(kTwoGposMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();
  FetchAndValidateDevicePolicy(ERROR_NONE);
}

// Make sure cleaning state works.
TEST_F(AuthPolicyTest, CleanStateDir) {
  const base::FilePath state_path =
      base::FilePath(paths_->Get(Path::STATE_DIR));
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  EXPECT_FALSE(base::IsDirectoryEmpty(state_path));
  EXPECT_TRUE(AuthPolicy::CleanState(paths_.get()));
  EXPECT_TRUE(base::IsDirectoryEmpty(state_path));
}

// Authentication doesn't back up auth state if Cryptohome is not mounted.
TEST_F(AuthPolicyTest, DoesNotBackUpOnAuthIfCryptohomeIsNotMounted) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  EXPECT_FALSE(base::PathExists(backup_path_));
}

// Authentication backs up auth state if Cryptohome is already mounted.
TEST_F(AuthPolicyTest, BacksUpOnAuthIfCryptohomeIsMounted) {
  samba().OnSessionStateChanged(kSessionStarted);
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_FALSE(base::PathExists(backup_path_));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  EXPECT_TRUE(base::PathExists(backup_path_));
}

// The session state change signal handler triggers a backup of user auth state.
TEST_F(AuthPolicyTest, BacksUpOnSessionStarted) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  EXPECT_FALSE(base::PathExists(backup_path_));
  NotifySessionStarted();
  EXPECT_TRUE(base::PathExists(backup_path_));
}

// Kerberos ticket renewal triggers a backup of user auth state.
TEST_F(AuthPolicyTest, BacksUpOnTgtAutoRenewal) {
  // Join and authenticate with Cryptohome mounted, so that a backup is written.
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  NotifySessionStarted();

  // Trigger TGT renewal and check if the backup file got re-written.
  // Modify timestamps of the existing backup file to detect overwrite.
  base::Time impossible_time = base::Time();
  ASSERT_TRUE(base::TouchFile(backup_path_, impossible_time, impossible_time));
  EXPECT_EQ(ERROR_NONE, samba().RenewUserTgtForTesting());
  base::Time backup_time = GetLastModified(backup_path_);
  EXPECT_LT(impossible_time, backup_time);
}

// Restarting authpolicy reloads the backup data and user-specific calls work
// without another AuthenticateUser() call.
TEST_F(AuthPolicyTest, LoadsBackupAndRestoresState) {
  // Join and authenticate with Cryptohome mounted, so that a backup is written.
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  NotifySessionStarted();
  EXPECT_TRUE(base::PathExists(backup_path_));

  // Restart authpolicyd.
  samba().ResetForTesting();
  EXPECT_EQ(ERROR_NONE, samba().Initialize(true /* expect_config */));

  // GetUserKerberosFiles should restore the backup including the Kerberos
  // ticket, so the Kerberos files changed signal should be called.
  KerberosFiles files;
  EXPECT_EQ(1, user_kerberos_files_changed_count_);
  EXPECT_EQ(ERROR_NONE, GetUserKerberosFiles(kAccountId, &files));
  EXPECT_EQ(2, user_kerberos_files_changed_count_);
  EXPECT_TRUE(files.has_krb5cc());
  EXPECT_TRUE(files.has_krb5conf());
  EXPECT_FALSE(files.krb5cc().empty());
  EXPECT_FALSE(files.krb5conf().empty());

  // The state should look like as if the user was logged in with valid TGT.
  ActiveDirectoryUserStatus status;
  EXPECT_EQ(ERROR_NONE, GetUserStatus(kUserPrincipal, kAccountId, &status));
  EXPECT_TRUE(status.has_tgt_status());
  EXPECT_EQ(ActiveDirectoryUserStatus::TGT_VALID, status.tgt_status());
  EXPECT_TRUE(status.has_password_status());
  EXPECT_EQ(ActiveDirectoryUserStatus::PASSWORD_VALID,
            status.password_status());
  EXPECT_TRUE(status.has_account_info());
  EXPECT_TRUE(status.account_info().has_pwd_last_set());
  EXPECT_TRUE(status.account_info().has_user_account_control());

  // TGT renewal still works. Do this before auth to check all state for renewal
  // got properly restored (auth overrides this).
  EXPECT_EQ(ERROR_NONE, samba().RenewUserTgtForTesting());

  // User policy fetch still works and that the affiliation state has been
  // properly restored from backup.
  validate_user_policy_ = &CheckUserPolicyEmpty;
  EXPECT_FALSE(user_affiliation_marker_set_);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);
  EXPECT_TRUE(user_affiliation_marker_set_);

  // Can also authenticate again to fetch a new TGT.
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, kAccountId, MakePasswordFd()));
}

// Policy fetch after a restart recovers successfully from backup (see
// https://crbug.com/908772).
TEST_F(AuthPolicyTest, LoadsBackupOnPolicyFetch) {
  // Join and authenticate with Cryptohome mounted, so that a backup is written.
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  NotifySessionStarted();

  // Restart authpolicyd.
  samba().ResetForTesting();
  EXPECT_EQ(ERROR_NONE, samba().Initialize(true /* expect_config */));

  // User policy fetch still works, even without auth.
  validate_user_policy_ = &CheckUserPolicyEmpty;
  FetchAndValidateUserPolicy(kAccountId, ERROR_NONE);
}

// By default, nothing should call the (expensive) anonymizer since no sensitive
// data is logged. Only if logging is enabled it should be called.
TEST_F(AuthPolicyTest, AnonymizerNotCalledWithoutLogging) {
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();

  validate_device_policy_ = &CheckDevicePolicyEmpty;
  FetchAndValidateDevicePolicy(ERROR_NONE);

  validate_user_policy_ = &CheckUserPolicyEmpty;
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_NONE);

  EXPECT_FALSE(samba().GetAnonymizerForTesting()->process_called_for_testing());
}

// If log output is requested, the logs should be anonymized.
TEST_F(AuthPolicyTest, AnonymizerCalledWithLogging) {
  // Turn on max logging and trigger an error. This triggers debug logging
  // which should be anonymized.
  samba().SetDefaultLogLevel(AuthPolicyFlags::kMaxLevel);
  ignore_result(Join(kTooLongMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_TRUE(samba().GetAnonymizerForTesting()->process_called_for_testing());
}

// Disable SeccompFiltersEnabled under ASAN. Minijail does not enable seccomp
// filtering when running under ASAN, so the test fails.
// https://crbug.com/908140
#ifndef BRILLO_ASAN_BUILD

// TODO(https://crbug.com/994814): This is currently broken under llvm-next
// since unexpected parts of the code trigger seccomp failures. The fix could be
// to refactor the code so that seccomp filtering is only enabled for the one
// process that is expected to trigger a seccomp failure. For example, if
// ERROR_NET_FAILED is expected, JailHelper::SetupJailAndRun should only enable
// seccomp filtering if |seccomp_path_key| is NET_ADS_SECCOMP etc.

// Re-enable seccomp filters and check that they are actually in effect.
TEST_F(AuthPolicyTest, DISABLED_SeccompFiltersEnabled) {
  // Re-enable seccomp filtering and trigger it in net ads join.
  samba().DisableSeccompForTesting(false);
  EXPECT_EQ(ERROR_NET_FAILED,
            Join(kSeccompMachineName, kUserPrincipal, MakePasswordFd()));

  // Disable seccomp filtering again, make sure net ads join works this time.
  samba().DisableSeccompForTesting(true);
  EXPECT_EQ(ERROR_NONE,
            Join(kSeccompMachineName, kUserPrincipal, MakePasswordFd()));

  MarkDeviceAsLocked();

  // Same with kinit. Check whether kinit can trigger seccomp failures.
  samba().DisableSeccompForTesting(false);
  EXPECT_EQ(ERROR_KINIT_FAILED,
            Auth(kSeccompUserPrincipal, "", MakePasswordFd()));
  samba().DisableSeccompForTesting(true);
  EXPECT_EQ(ERROR_NONE, Auth(kSeccompUserPrincipal, "", MakePasswordFd()));

  // Finally, check whether smbclient can trigger seccomp failures.
  samba().DisableSeccompForTesting(false);
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  FetchAndValidateDevicePolicy(ERROR_SMBCLIENT_FAILED);
  samba().DisableSeccompForTesting(true);
  FetchAndValidateDevicePolicy(ERROR_NONE);
}

#endif  // #ifndef BRILLO_ASAN_BUILD

// GPO files are cached and not re-downloaded if the version didn't change.
TEST_F(AuthPolicyTest, UsesCachedGposForSameVersion) {
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendBoolean(policy::key::kDeviceGuestModeEnabled, kPolicyBool);
  writer.WriteToFile(stub_gpo1_path_);

  validate_device_policy_ = [](const em::ChromeDeviceSettingsProto& policy) {
    EXPECT_EQ(kPolicyBool, policy.guest_mode_enabled().guest_mode_enabled());
  };

  const GpoVersionCache* cache = samba().GetGpoVersionCacheForTesting();

  // Join domain. |kOneGpoKeepVersionMachineName| freezes the GPO version at 1.
  EXPECT_EQ(ERROR_NONE, Join(kOneGpoKeepVersionMachineName, kUserPrincipal,
                             MakePasswordFd()));
  MarkDeviceAsLocked();

  // First policy fetch should be a cache miss with expected version 1.
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(1, cache->cache_misses_for_testing());
  EXPECT_EQ(0, cache->cache_hits_for_testing());

  // Next time should be a cache hit since the version is still 1.
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(1, cache->cache_misses_for_testing());
  EXPECT_EQ(1, cache->cache_hits_for_testing());
}

// GPO files are re-downloaded if the version changed.
TEST_F(AuthPolicyTest, DoesNotUseCachedGposForDifferentVersion) {
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendBoolean(policy::key::kDeviceGuestModeEnabled, kPolicyBool);
  writer.WriteToFile(stub_gpo1_path_);

  validate_device_policy_ = [](const em::ChromeDeviceSettingsProto& policy) {
    EXPECT_EQ(kPolicyBool, policy.guest_mode_enabled().guest_mode_enabled());
  };

  GpoVersionCache* cache = samba().GetGpoVersionCacheForTesting();

  // Join domain.
  EXPECT_EQ(ERROR_NONE,
            Join(kOneGpoMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();

  // First policy fetch should be a cache miss with expected version 1.
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(1, cache->cache_misses_for_testing());
  EXPECT_EQ(0, cache->cache_hits_for_testing());

  // Next time should be a cache miss again since |kOneGpoMachineName| caused
  // the version to increase on every FetchAndValidateDevicePolicy() call.
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(2, cache->cache_misses_for_testing());
  EXPECT_EQ(0, cache->cache_hits_for_testing());
}

// Caching should not complain about GPOs with missing files.
TEST_F(AuthPolicyTest, CachesGposWithMissingFile) {
  const GpoVersionCache* cache = samba().GetGpoVersionCacheForTesting();

  // Write file 1, but omit file 2.
  policy::PRegUserDevicePolicyWriter writer;
  writer.WriteToFile(stub_gpo1_path_);
  EXPECT_FALSE(base::PathExists(stub_gpo2_path_));

  // First time should be a cache miss for both.
  // Note that with each FetchAndValidateDevicePolicy() call, the effect of
  // |kTwoGposKeepVersionMachineName| is
  // - an increase of the version of the first GPO, so that the download code is
  //   triggered, but
  // - a freeze of the version of the second GPO, so that it's cached.

  // First time should be a cache miss.
  JoinAndFetchDevicePolicy(kTwoGposKeepVersionMachineName);
  EXPECT_EQ(2, cache->cache_misses_for_testing());
  EXPECT_EQ(0, cache->cache_hits_for_testing());

  // Second time should be a cache miss for the first file (since its version
  // increased) and a cache hit for the missing second file.
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(3, cache->cache_misses_for_testing());
  EXPECT_EQ(1, cache->cache_hits_for_testing());
}

// The GPO cache is purged occasionally.
TEST_F(AuthPolicyTest, PurgesGpoCacheOccasionally) {
  // Set a testing clock to stub out cache TTL.
  GpoVersionCache* cache = samba().GetGpoVersionCacheForTesting();
  auto clock_ptr = std::make_unique<base::SimpleTestClock>();
  base::SimpleTestClock* clock = clock_ptr.get();
  cache->SetClockForTesting(std::move(clock_ptr));

  // Pick delta so that one Advance() call won't purge the cache, but two will.
  const base::TimeDelta delta =
      samba().GetGpoVersionCacheTTLForTesting() * 2 / 3;

  JoinAndFetchDevicePolicy(kOneGpoKeepVersionMachineName);
  EXPECT_EQ(1, cache->cache_misses_for_testing());
  EXPECT_EQ(0, cache->cache_hits_for_testing());

  // First Advance() should NOT cause a cache purge, so we get a cache hit.
  clock->Advance(delta);
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(1, cache->cache_misses_for_testing());
  EXPECT_EQ(1, cache->cache_hits_for_testing());

  // Second Advance() should cause a cache purge, so we get a cache miss.
  clock->Advance(delta);
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_EQ(2, cache->cache_misses_for_testing());
  EXPECT_EQ(1, cache->cache_hits_for_testing());
}

// The lifetime of the GPO cache can be changed by the DeviceGpoCacheLifetime
// policy. It can also be turned off by that policy.
TEST_F(AuthPolicyTest, GpoCacheTTLCanBeSetByPolicy) {
  // Join domain.
  EXPECT_EQ(ERROR_NONE, Join(kOneGpoKeepVersionMachineName, kUserPrincipal,
                             MakePasswordFd()));
  MarkDeviceAsLocked();
  validate_device_policy_ = &DontValidateDevicePolicy;
  EXPECT_TRUE(samba().GetGpoVersionCacheForTesting()->IsEnabled());

  // Setting the policy to 0 should turn the cache off and set the TTL to 0.
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendInteger(policy::key::kDeviceGpoCacheLifetime, kZeroHours);
  writer.WriteToFile(stub_gpo1_path_);
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_FALSE(samba().GetGpoVersionCacheForTesting()->IsEnabled());
  EXPECT_EQ(base::TimeDelta::FromHours(kZeroHours),
            samba().GetGpoVersionCacheTTLForTesting());

  // Setting the policy to > 0 should turn the cache on again and set the TTL.
  // Note that this part would fail (policy wouldn't reload) if the cache was
  // still on, see below.
  policy::PRegUserDevicePolicyWriter writer2;
  writer2.AppendInteger(policy::key::kDeviceGpoCacheLifetime, kNonZeroHours);
  writer2.WriteToFile(stub_gpo1_path_);
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_TRUE(samba().GetGpoVersionCacheForTesting()->IsEnabled());
  EXPECT_EQ(base::TimeDelta::FromHours(kNonZeroHours),
            samba().GetGpoVersionCacheTTLForTesting());

  // Fetch again to fill the cache.
  FetchAndValidateDevicePolicy(ERROR_NONE);

  // Rewriting the policy should be ignored now since a cached version is used.
  // (A rewrite in this test doesn't change the GPO version since we're using
  // kOneGpoKeepVersionMachineName).
  policy::PRegUserDevicePolicyWriter writer3;
  writer3.AppendInteger(policy::key::kDeviceGpoCacheLifetime, kZeroHours);
  writer3.WriteToFile(stub_gpo1_path_);
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_TRUE(samba().GetGpoVersionCacheForTesting()->IsEnabled());
}

// Authpolicyd writes an auth data cache file on domain join and after auth.
TEST_F(AuthPolicyTest, AuthDataCacheWrittenOnJoinAndAuth) {
  base::FilePath cache_path(paths_->Get(Path::AUTH_DATA_CACHE));

  // Cache file should be written after domain join.
  EXPECT_FALSE(base::PathExists(cache_path));
  EXPECT_EQ(ERROR_NONE, Join(kMachineName, kUserPrincipal, MakePasswordFd()));
  EXPECT_TRUE(base::PathExists(cache_path));

  // Cache file should be written by user auth as well.
  EXPECT_TRUE(base::DeleteFile(cache_path, false /* recursive */));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));
  EXPECT_TRUE(base::PathExists(cache_path));

  // The file should NOT be written on failed auth (because at the point of
  // failure we don't know yet whether the realm is affiliated or not).
  EXPECT_TRUE(base::DeleteFile(cache_path, false /* recursive */));
  EXPECT_EQ(ERROR_PARSE_UPN_FAILED,
            Auth(kInvalidUserPrincipal, "", MakePasswordFd()));
  EXPECT_FALSE(base::PathExists(cache_path));
}

// Checks whether the auth data cache contains all the data that we'd expect.
TEST_F(AuthPolicyTest, AuthDataCacheContainsExpectedData) {
  // Join domain with machine domain != user domain.
  JoinDomainRequest request;
  request.set_machine_name(kMachineName);
  request.set_machine_domain(kMachineRealm);
  request.set_user_principal_name(kUserPrincipal);
  EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd()));

  // Load the cache and check values.
  AuthDataCache* cache = samba().GetAuthDataCacheForTesting();

  ASSERT_TRUE(cache->GetWorkgroup(kMachineRealm));
  ASSERT_TRUE(cache->GetKdcIp(kMachineRealm));
  ASSERT_TRUE(cache->GetDcName(kMachineRealm));
  ASSERT_TRUE(cache->GetIsAffiliated(kMachineRealm));

  EXPECT_EQ(kDefaultWorkgroup, *cache->GetWorkgroup(kMachineRealm));
  EXPECT_EQ(kDefaultKdcIp, *cache->GetKdcIp(kMachineRealm));
  EXPECT_EQ(kDefaultDcName, *cache->GetDcName(kMachineRealm));
  // The machine realm is always affiliated with itself!
  EXPECT_TRUE(*cache->GetIsAffiliated(kMachineRealm));

  EXPECT_FALSE(cache->GetWorkgroup(kUserRealm));
  EXPECT_FALSE(cache->GetKdcIp(kUserRealm));
  EXPECT_FALSE(cache->GetDcName(kUserRealm));
  EXPECT_FALSE(cache->GetIsAffiliated(kUserRealm));

  // Auth should fill the user realm data.
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));

  ASSERT_TRUE(cache->GetWorkgroup(kUserRealm));
  ASSERT_TRUE(cache->GetKdcIp(kUserRealm));
  ASSERT_TRUE(cache->GetDcName(kUserRealm));
  ASSERT_TRUE(cache->GetIsAffiliated(kUserRealm));

  EXPECT_EQ(kDefaultWorkgroup, *cache->GetWorkgroup(kUserRealm));
  EXPECT_EQ(kDefaultKdcIp, *cache->GetKdcIp(kUserRealm));
  EXPECT_EQ(kDefaultDcName, *cache->GetDcName(kUserRealm));
  EXPECT_TRUE(*cache->GetIsAffiliated(kUserRealm));
}

// Authpolicyd loads auth data cache on startup and uses the data for workgroup
// and KDC IP.
TEST_F(AuthPolicyTest, AuthDataCacheLoadsAndWorksForWorkgroupAndKdcIp) {
  // Join domain, so that a config file is written. Authpolicyd only loads a
  // auth data cache file if a config is expected.
  JoinDomainRequest request;
  request.set_machine_name(kMachineName);
  request.set_machine_domain(kMachineRealm);
  request.set_user_principal_name(kUserPrincipal);
  EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd()));
  MarkDeviceAsLocked();
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  FetchAndValidateDevicePolicy(ERROR_NONE);

  // Create a cache file with stub workgroup and KDC IP data. Note that machine
  // KDC IP is not checked. The cached KDC IP is overwritten during the machine
  // password check, which happens immediately on startup. The check needs the
  // server time and the KDC IP is fetched along with it, immediately
  // overwriting the cached value.
  protos::DebugFlags flags;
  AuthDataCache cache(&flags);
  cache.SetWorkgroup(kUserRealm, kCacheTestUserWorkgroup);
  cache.SetKdcIp(kUserRealm, kCacheTestUserKdcIp);
  cache.SetWorkgroup(kMachineRealm, kCacheTestMachineWorkgroup);
  base::FilePath cache_path(paths_->Get(Path::AUTH_DATA_CACHE));
  EXPECT_TRUE(cache.Save(cache_path));

  // Restart authpolicyd. That should load the cache file.
  samba().ResetForTesting();
  EXPECT_EQ(ERROR_NONE, samba().Initialize(true /* expect_config */));
  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));

  // Check that the device smb.conf contains the workgroup from the cache.
  SmbConf device_smb_conf;
  ReadSmbConf(paths_->Get(Path::DEVICE_SMB_CONF), &device_smb_conf);
  EXPECT_EQ(kCacheTestMachineWorkgroup, device_smb_conf.workgroup);

  // Check that the user smb.conf contains the workgroup from the cache.
  SmbConf user_smb_conf;
  ReadSmbConf(paths_->Get(Path::USER_SMB_CONF), &user_smb_conf);
  EXPECT_EQ(kCacheTestUserWorkgroup, user_smb_conf.workgroup);

  // Check that the user krb5.conf contains the KDC IP from the cache.
  Krb5Conf user_krb5_conf;
  ReadKrb5Conf(paths_->Get(Path::USER_KRB5_CONF), &user_krb5_conf);
  EXPECT_EQ(EmbraceIp(kCacheTestUserKdcIp), user_krb5_conf.kdc);
}

// Authpolicyd loads auth data cache on startup and uses the data for DC name.
TEST_F(AuthPolicyTest, AuthDataCacheWorksForDcName) {
  // Join domain, so that a config file is written. Authpolicyd only loads a
  // auth data cache file if a config is expected.
  // Note that |kOneGpoMachineName| is used here. This makes sure that
  // stub_smbclient is called with the DC name in the URL of the GPO to
  // download. Since stub_smbclient doesn't accept the DC name we set here
  // (|kCacheTest*DcName|, see |kHostAndShare|), it should respond  with a
  // failed connection error and we should see ERROR_SMBCLIENT_FAILED as a
  // result. There's no other artifact of DC name that could be tested.
  JoinDomainRequest request;
  request.set_machine_name(kOneGpoMachineName);
  request.set_machine_domain(kMachineRealm);
  request.set_user_principal_name(kUserPrincipal);
  EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd()));
  MarkDeviceAsLocked();
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  FetchAndValidateDevicePolicy(ERROR_NONE);

  // Create a cache file with stub DC name.
  protos::DebugFlags flags;
  AuthDataCache cache(&flags);
  cache.SetDcName(kUserRealm, kCacheTestUserDcName);
  cache.SetDcName(kMachineRealm, kCacheTestMachineDcName);
  base::FilePath cache_path(paths_->Get(Path::AUTH_DATA_CACHE));
  EXPECT_TRUE(cache.Save(cache_path));

  // Restart authpolicyd, so that the cache file gets loaded.
  samba().ResetForTesting();
  EXPECT_EQ(ERROR_NONE, samba().Initialize(true /* expect_config */));

  // Since stub_smbclient doesn't accept the DC names from the cache, we should
  // get an error here (best we can do to verify cached DC name was used).
  FetchAndValidateDevicePolicy(ERROR_SMBCLIENT_FAILED);
  FetchAndValidateUserPolicy(DefaultAuth(), ERROR_SMBCLIENT_FAILED);
}

// Authpolicyd loads auth data cache on startup and uses the data for DC name.
TEST_F(AuthPolicyTest, AuthDataCacheDiesForUnaffiliatedUsers) {
  JoinAndFetchDevicePolicy(kMachineName);

  // Set affiliation flag to false for the user realm. Note that this shouldn't
  // be possible, authpolicy shouldn't cache any data for unaffiliated realms.
  // See AuthDataCacheTurnedOffForUnaffiliatedUsers.
  samba().GetAuthDataCacheForTesting()->SetIsAffiliated(kUserRealm, false);

  // SambaInterface::IsUserAffiliated() should bail on Auth() because data for
  // an unaffiliated realm is cached.
  EXPECT_DEATH((void)Auth(kUserPrincipal, "", MakePasswordFd()),
               "Caching for unaffiliated realms not supported");
}

// Authpolicyd loads auth data cache on startup and uses the data for DC name.
TEST_F(AuthPolicyTest, AuthDataCacheTurnedOffForUnaffiliatedUsers) {
  // |kUnaffiliatedMachineName| causes the user realm to be unaffiliated.
  JoinDomainRequest request;
  request.set_machine_name(kUnaffiliatedMachineName);
  request.set_machine_domain(kMachineRealm);
  request.set_user_principal_name(kUserPrincipal);
  EXPECT_EQ(ERROR_NONE, JoinEx(request, MakePasswordFd()));
  MarkDeviceAsLocked();
  validate_device_policy_ = &CheckDevicePolicyEmpty;
  FetchAndValidateDevicePolicy(ERROR_NONE);

  EXPECT_EQ(ERROR_NONE, Auth(kUserPrincipal, "", MakePasswordFd()));

  // The cache should still exist from domain join, but it shouldn't contain the
  // user realm.
  AuthDataCache* cache = samba().GetAuthDataCacheForTesting();
  EXPECT_FALSE(cache->GetWorkgroup(kUserRealm));
  EXPECT_FALSE(cache->GetKdcIp(kUserRealm));
  EXPECT_FALSE(cache->GetDcName(kUserRealm));
  EXPECT_FALSE(cache->GetIsAffiliated(kUserRealm));
}

// The auth data cache cache is purged occasionally.
TEST_F(AuthPolicyTest, PurgesAuthDataCacheOccasionally) {
  // Set a testing clock to stub out cache TTL.
  auto clock_ptr = std::make_unique<base::SimpleTestClock>();
  base::SimpleTestClock* clock = clock_ptr.get();
  samba().GetAuthDataCacheForTesting()->SetClockForTesting(
      std::move(clock_ptr));

  JoinAndFetchDevicePolicy(kMachineName);

  // Save a cache file that contains a different workgroup.
  protos::DebugFlags flags;
  AuthDataCache cache(&flags);
  cache.SetClockForTesting(std::make_unique<base::SimpleTestClock>());
  cache.SetWorkgroup(kUserRealm, kCacheTestUserWorkgroup);
  base::FilePath cache_path(paths_->Get(Path::AUTH_DATA_CACHE));
  EXPECT_TRUE(cache.Save(cache_path));

  // Restart authpolicy to make sure that the cached workgroup is used.
  samba().ResetForTesting();
  EXPECT_EQ(ERROR_NONE, samba().Initialize(true /* expect_config */));
  DefaultAuth();
  SmbConf user_smb_conf;
  ReadSmbConf(paths_->Get(Path::USER_SMB_CONF), &user_smb_conf);
  EXPECT_EQ(kCacheTestUserWorkgroup, user_smb_conf.workgroup);

  // Restart authpolicy again and call auth, but this time advance the clock
  // enough to clear the cache, so that the default workgroup is used.
  samba().ResetForTesting();
  EXPECT_EQ(ERROR_NONE, samba().Initialize(true /* expect_config */));
  clock->Advance(samba().GetAuthDataCacheTTLForTesting() * 2);
  DefaultAuth();
  ReadSmbConf(paths_->Get(Path::USER_SMB_CONF), &user_smb_conf);
  EXPECT_EQ(kDefaultWorkgroup, user_smb_conf.workgroup);
}

// The lifetime of the auth data cache can be changed by the
// DeviceAuthDataCacheLifetime policy. It can also be turned off by that policy.
TEST_F(AuthPolicyTest, AuthDataCacheTTLCanBeSetByPolicy) {
  base::FilePath cache_path(paths_->Get(Path::AUTH_DATA_CACHE));

  // Join domain.
  EXPECT_EQ(ERROR_NONE,
            Join(kOneGpoMachineName, kUserPrincipal, MakePasswordFd()));
  MarkDeviceAsLocked();
  validate_device_policy_ = &DontValidateDevicePolicy;
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_TRUE(base::PathExists(cache_path));

  // Setting the policy to 0 should turn the cache off, clear the file and set
  // the TTL to 0.
  policy::PRegUserDevicePolicyWriter writer;
  writer.AppendInteger(policy::key::kDeviceAuthDataCacheLifetime, kZeroHours);
  writer.WriteToFile(stub_gpo1_path_);
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_FALSE(samba().GetAuthDataCacheForTesting()->IsEnabled());
  EXPECT_EQ(base::TimeDelta::FromHours(kZeroHours),
            samba().GetAuthDataCacheTTLForTesting());
  EXPECT_FALSE(base::PathExists(cache_path));

  // Setting the policy to > 0 should turn the cache on again and set the TTL.
  policy::PRegUserDevicePolicyWriter writer2;
  writer2.AppendInteger(policy::key::kDeviceAuthDataCacheLifetime,
                        kNonZeroHours);
  writer2.WriteToFile(stub_gpo1_path_);
  FetchAndValidateDevicePolicy(ERROR_NONE);
  EXPECT_TRUE(samba().GetAuthDataCacheForTesting()->IsEnabled());
  EXPECT_EQ(base::TimeDelta::FromHours(kNonZeroHours),
            samba().GetAuthDataCacheTTLForTesting());
  EXPECT_FALSE(base::PathExists(cache_path));
}

}  // namespace authpolicy
