// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/system_proxy_adaptor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <list>
#include <map>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/stringprintf.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/message_loops/base_message_loop.h>
#include <dbus/object_path.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/kerberos/dbus-constants.h>
#include <dbus/system_proxy/dbus-constants.h>

#include "bindings/worker_common.pb.h"
#include "system-proxy/kerberos_client.h"
#include "system_proxy/proto_bindings/system_proxy_service.pb.h"
#include "system-proxy/protobuf_util.h"
#include "system-proxy/sandboxed_worker.h"

using testing::_;
using testing::Return;

namespace system_proxy {
namespace {
const char kUser[] = "proxy_user";
const char kPassword[] = "proxy_password";
const char kPrincipalName[] = "user@TEST";
const char kLocalProxyHostPort[] = "local.proxy.url:3128";
const char kObjectPath[] = "/object/path";

// Stub completion callback for RegisterAsync().
void DoNothing(bool /* unused */) {}

}  // namespace

class FakeSandboxedWorker : public SandboxedWorker {
 public:
  explicit FakeSandboxedWorker(base::WeakPtr<SystemProxyAdaptor> adaptor)
      : SandboxedWorker(adaptor) {}
  FakeSandboxedWorker(const FakeSandboxedWorker&) = delete;
  FakeSandboxedWorker& operator=(const FakeSandboxedWorker&) = delete;
  ~FakeSandboxedWorker() override = default;

  bool Start() override { return is_running_ = true; }
  bool Stop() override { return is_running_ = false; }
  bool IsRunning() override { return is_running_; }

  std::string local_proxy_host_and_port() override {
    return kLocalProxyHostPort;
  }

 private:
  bool is_running_;
};

class FakeSystemProxyAdaptor : public SystemProxyAdaptor {
 public:
  FakeSystemProxyAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
      : SystemProxyAdaptor(std::move(dbus_object)), weak_ptr_factory_(this) {}
  FakeSystemProxyAdaptor(const FakeSystemProxyAdaptor&) = delete;
  FakeSystemProxyAdaptor& operator=(const FakeSystemProxyAdaptor&) = delete;
  ~FakeSystemProxyAdaptor() override = default;

 protected:
  std::unique_ptr<SandboxedWorker> CreateWorker() override {
    return std::make_unique<FakeSandboxedWorker>(
        weak_ptr_factory_.GetWeakPtr());
  }
  void ConnectNamespace(SandboxedWorker* worker, bool user_traffic) override {
    OnNamespaceConnected(worker, user_traffic);
  }

 private:
  FRIEND_TEST(SystemProxyAdaptorTest, KerberosEnabled);
  FRIEND_TEST(SystemProxyAdaptorTest, ConnectNamespace);
  FRIEND_TEST(SystemProxyAdaptorTest, ProxyResolutionFilter);
  FRIEND_TEST(SystemProxyAdaptorTest, ProtectionSpaceAuthenticationRequired);
  FRIEND_TEST(SystemProxyAdaptorTest, ProtectionSpaceNoCredentials);

  base::WeakPtrFactory<FakeSystemProxyAdaptor> weak_ptr_factory_;
};

class SystemProxyAdaptorTest : public ::testing::Test {
 public:
  SystemProxyAdaptorTest() {
    const dbus::ObjectPath object_path(kObjectPath);

    // Mock out D-Bus initialization.
    mock_exported_object_ =
        base::MakeRefCounted<dbus::MockExportedObject>(bus_.get(), object_path);

    EXPECT_CALL(*bus_, GetExportedObject(_))
        .WillRepeatedly(Return(mock_exported_object_.get()));

    EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
        .Times(testing::AnyNumber());

    mock_kerberos_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        bus_.get(), kerberos::kKerberosServiceName,
        dbus::ObjectPath(kerberos::kKerberosServicePath));
    EXPECT_CALL(*bus_, GetObjectProxy(kerberos::kKerberosServiceName, _))
        .WillRepeatedly(Return(mock_kerberos_proxy_.get()));

    adaptor_.reset(new FakeSystemProxyAdaptor(
        std::make_unique<brillo::dbus_utils::DBusObject>(nullptr, bus_,
                                                         object_path)));
    adaptor_->RegisterAsync(base::BindRepeating(&DoNothing));
    mock_patchpanel_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        bus_.get(), patchpanel::kPatchPanelServiceName,
        dbus::ObjectPath(patchpanel::kPatchPanelServicePath));
    brillo_loop_.SetAsCurrent();
  }
  SystemProxyAdaptorTest(const SystemProxyAdaptorTest&) = delete;
  SystemProxyAdaptorTest& operator=(const SystemProxyAdaptorTest&) = delete;
  ~SystemProxyAdaptorTest() override = default;

  void AddCredentialsToAuthCache(
      const worker::ProtectionSpace& protection_space,
      const std::string& username,
      const std::string& password) {
    Credentials credentials;
    credentials.set_username(username);
    credentials.set_password(password);
    mock_auth_cache_[protection_space.SerializeAsString()] = credentials;
  }

  void OnWorkerActive(dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), "org.chromium.SystemProxy");
    EXPECT_EQ(signal->GetMember(), "WorkerActive");
    active_worker_signal_called_ = true;

    dbus::MessageReader signal_reader(signal);
    system_proxy::WorkerActiveSignalDetails details;
    EXPECT_TRUE(signal_reader.PopArrayOfBytesAsProto(&details));
    EXPECT_EQ(kLocalProxyHostPort, details.local_proxy_url());
  }

  void OnAuthenticationRequired(dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), "org.chromium.SystemProxy");
    EXPECT_EQ(signal->GetMember(), "AuthenticationRequired");

    dbus::MessageReader signal_reader(signal);
    system_proxy::AuthenticationRequiredDetails details;
    EXPECT_TRUE(signal_reader.PopArrayOfBytesAsProto(&details));

    Credentials credentials;

    auto it = mock_auth_cache_.find(
        details.proxy_protection_space().SerializeAsString());

    if (it != mock_auth_cache_.end()) {
      credentials = it->second;
    } else {
      credentials.set_username("");
      credentials.set_password("");
    }

    SetAuthenticationDetailsRequest request;
    *request.mutable_credentials() = credentials;
    *request.mutable_protection_space() = details.proxy_protection_space();
    request.set_traffic_type(TrafficOrigin::SYSTEM);

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());

    adaptor_->SetAuthenticationDetails(proto_blob);
    ASSERT_TRUE(brillo_loop_.RunOnce(/*may_block=*/false));
  }

 protected:
  bool active_worker_signal_called_ = false;
  std::map<std::string, Credentials> mock_auth_cache_;
  scoped_refptr<dbus::MockBus> bus_ = new dbus::MockBus(dbus::Bus::Options());
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  // SystemProxyAdaptor instance that creates fake worker processes.
  std::unique_ptr<FakeSystemProxyAdaptor> adaptor_;

  scoped_refptr<dbus::MockObjectProxy> mock_patchpanel_proxy_;
  scoped_refptr<dbus::MockObjectProxy> mock_kerberos_proxy_;

  base::MessageLoopForIO loop_;
  brillo::BaseMessageLoop brillo_loop_{&loop_};
};

TEST_F(SystemProxyAdaptorTest, SetAuthenticationDetails) {
  EXPECT_CALL(*bus_, GetObjectProxy(patchpanel::kPatchPanelServiceName, _))
      .WillOnce(Return(mock_patchpanel_proxy_.get()));

  EXPECT_FALSE(adaptor_->system_services_worker_.get());
  SetAuthenticationDetailsRequest request;
  Credentials credentials;
  credentials.set_username(kUser);
  credentials.set_password(kPassword);
  *request.mutable_credentials() = credentials;
  request.set_traffic_type(TrafficOrigin::SYSTEM);

  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  // First create a worker object.
  adaptor_->SetAuthenticationDetails(proto_blob);
  ASSERT_TRUE(brillo_loop_.RunOnce(/*may_block=*/false));

  ASSERT_TRUE(adaptor_->system_services_worker_.get());
  EXPECT_TRUE(adaptor_->system_services_worker_->IsRunning());

  int fds[2];
  EXPECT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);

  adaptor_->SetAuthenticationDetails(proto_blob);
  ASSERT_TRUE(brillo_loop_.RunOnce(/*may_block=*/false));

  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_credentials());
  EXPECT_EQ(config.credentials().username(), kUser);
  EXPECT_EQ(config.credentials().password(), kPassword);
}

TEST_F(SystemProxyAdaptorTest, KerberosEnabled) {
  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  ASSERT_TRUE(adaptor_->system_services_worker_.get());

  int fds[2];
  ASSERT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);

  SetAuthenticationDetailsRequest request;
  request.set_kerberos_enabled(true);
  request.set_active_principal_name(kPrincipalName);
  request.set_traffic_type(TrafficOrigin::SYSTEM);

  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  // First create a worker object.
  adaptor_->SetAuthenticationDetails(proto_blob);
  brillo_loop_.RunOnce(false);

  // Expect that the availability of kerberos auth has been sent to the worker.
  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_kerberos_config());
  EXPECT_TRUE(config.kerberos_config().enabled());
  EXPECT_EQ(config.kerberos_config().krb5cc_path(), "/tmp/ccache");
  EXPECT_EQ(config.kerberos_config().krb5conf_path(), "/tmp/krb5.conf");

  // Expect that the availability of kerberos auth has been sent to the kerberos
  // client.
  EXPECT_TRUE(adaptor_->kerberos_client_->kerberos_enabled_);
  EXPECT_EQ(adaptor_->kerberos_client_->principal_name_, kPrincipalName);
}

TEST_F(SystemProxyAdaptorTest, ShutDown) {
  EXPECT_CALL(*bus_, GetObjectProxy(patchpanel::kPatchPanelServiceName, _))
      .WillOnce(Return(mock_patchpanel_proxy_.get()));
  EXPECT_FALSE(adaptor_->system_services_worker_.get());
  SetAuthenticationDetailsRequest request;
  Credentials credentials;
  credentials.set_username(kUser);
  credentials.set_password(kPassword);
  *request.mutable_credentials() = credentials;
  request.set_traffic_type(TrafficOrigin::SYSTEM);
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  // First create a worker object.
  adaptor_->SetAuthenticationDetails(proto_blob);
  brillo_loop_.RunOnce(false);

  EXPECT_TRUE(adaptor_->system_services_worker_.get());
  EXPECT_TRUE(adaptor_->system_services_worker_->IsRunning());

  adaptor_->ShutDown();
  EXPECT_FALSE(adaptor_->system_services_worker_->IsRunning());
}

TEST_F(SystemProxyAdaptorTest, ConnectNamespace) {
  EXPECT_FALSE(active_worker_signal_called_);
  EXPECT_CALL(*mock_exported_object_, SendSignal(_))
      .WillOnce(Invoke(this, &SystemProxyAdaptorTest::OnWorkerActive));

  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  adaptor_->ConnectNamespace(adaptor_->system_services_worker_.get(),
                             /* user_traffic= */ false);
  EXPECT_TRUE(active_worker_signal_called_);
}

TEST_F(SystemProxyAdaptorTest, ProxyResolutionFilter) {
  std::vector<std::string> proxy_list = {
      base::StringPrintf("%s%s", "http://", kLocalProxyHostPort),
      "http://test.proxy.com", "https://test.proxy.com", "direct://"};

  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  int fds[2];
  ASSERT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);
  adaptor_->system_services_worker_->OnProxyResolved("target_url", true,
                                                     proxy_list);

  brillo_loop_.RunOnce(false);

  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_proxy_resolution_reply());
  std::list<std::string> proxies;
  const worker::ProxyResolutionReply& reply = config.proxy_resolution_reply();
  for (auto const& proxy : reply.proxy_servers())
    proxies.push_back(proxy);

  EXPECT_EQ("target_url", reply.target_url());
  EXPECT_EQ(2, proxies.size());
  EXPECT_EQ("http://test.proxy.com", proxies.front());
}

// Test that verifies that authentication requests are result in sending a
// signal to notify credentials are missing and credentials and protection space
// if correctly forwarded to the worker processes.
TEST_F(SystemProxyAdaptorTest, ProtectionSpaceAuthenticationRequired) {
  EXPECT_CALL(*mock_exported_object_, SendSignal(_))
      .WillOnce(
          Invoke(this, &SystemProxyAdaptorTest::OnAuthenticationRequired));

  worker::ProtectionSpace protection_space;
  protection_space.set_origin("http://test.proxy.com");
  protection_space.set_realm("my realm");
  protection_space.set_scheme("basic");
  std::string msg;
  protection_space.SerializeToString(&msg);
  AddCredentialsToAuthCache(protection_space, kUser, kPassword);

  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  int fds[2];
  ASSERT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);
  adaptor_->RequestAuthenticationCredentials(protection_space);

  brillo_loop_.RunOnce(false);

  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_credentials());

  const worker::Credentials& reply = config.credentials();
  EXPECT_TRUE(reply.has_protection_space());
  EXPECT_EQ(reply.username(), kUser);
  EXPECT_EQ(reply.password(), kPassword);
  EXPECT_EQ(reply.protection_space().SerializeAsString(),
            protection_space.SerializeAsString());
}

// Test that verifies that authentication requests that resolve to an empty
// credentials set are forwarded to the worker processes.
TEST_F(SystemProxyAdaptorTest, ProtectionSpaceNoCredentials) {
  EXPECT_CALL(*mock_exported_object_, SendSignal(_))
      .WillOnce(
          Invoke(this, &SystemProxyAdaptorTest::OnAuthenticationRequired));

  worker::ProtectionSpace protection_space;
  protection_space.set_origin("http://test.proxy.com");
  protection_space.set_realm("my realm");
  protection_space.set_scheme("basic");
  std::string msg;
  protection_space.SerializeToString(&msg);

  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  int fds[2];
  ASSERT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);
  adaptor_->RequestAuthenticationCredentials(protection_space);

  brillo_loop_.RunOnce(false);

  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_credentials());

  const worker::Credentials& reply = config.credentials();
  EXPECT_TRUE(reply.has_protection_space());
  EXPECT_EQ(reply.username(), "");
  EXPECT_EQ(reply.password(), "");
  EXPECT_EQ(reply.protection_space().SerializeAsString(),
            protection_space.SerializeAsString());
}

}  // namespace system_proxy
