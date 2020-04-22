// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/system_proxy_adaptor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/message_loops/base_message_loop.h>
#include <dbus/object_path.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <chromeos/dbus/service_constants.h>

#include "bindings/worker_common.pb.h"
#include "system_proxy/proto_bindings/system_proxy_service.pb.h"
#include "system-proxy/protobuf_util.h"
#include "system-proxy/sandboxed_worker.h"

using testing::_;
using testing::Return;

namespace system_proxy {
namespace {
const char kUser[] = "proxy_user";
const char kPassword[] = "proxy_password";
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
  bool ConnectNamespace(SandboxedWorker* worker, bool user_traffic) override {
    return true;
  }

 private:
  base::WeakPtrFactory<FakeSystemProxyAdaptor> weak_ptr_factory_;
};

class SystemProxyAdaptorTest : public ::testing::Test {
 public:
  SystemProxyAdaptorTest() {
    const dbus::ObjectPath object_path("/object/path");

    adaptor_.reset(new FakeSystemProxyAdaptor(
        std::make_unique<brillo::dbus_utils::DBusObject>(nullptr, bus_,
                                                         object_path)));
    mock_patchpanel_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        bus_.get(), patchpanel::kPatchPanelServiceName,
        dbus::ObjectPath(patchpanel::kPatchPanelServicePath));
    brillo_loop_.SetAsCurrent();
  }
  SystemProxyAdaptorTest(const SystemProxyAdaptorTest&) = delete;
  SystemProxyAdaptorTest& operator=(const SystemProxyAdaptorTest&) = delete;
  ~SystemProxyAdaptorTest() override = default;

 protected:
  // SystemProxyAdaptor instance that creates fake worker processes.
  std::unique_ptr<FakeSystemProxyAdaptor> adaptor_;
  scoped_refptr<dbus::MockBus> bus_ = new dbus::MockBus(dbus::Bus::Options());
  scoped_refptr<dbus::MockObjectProxy> mock_patchpanel_proxy_;
  base::MessageLoopForIO loop_;
  brillo::BaseMessageLoop brillo_loop_{&loop_};
};

TEST_F(SystemProxyAdaptorTest, SetSystemTrafficCredentials) {
  EXPECT_CALL(*bus_, GetObjectProxy(patchpanel::kPatchPanelServiceName, _))
      .WillOnce(Return(mock_patchpanel_proxy_.get()));

  EXPECT_FALSE(adaptor_->system_services_worker_.get());
  SetSystemTrafficCredentialsRequest request;
  request.set_system_services_username(kUser);
  request.set_system_services_password(kPassword);
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  // First create a worker object.
  adaptor_->SetSystemTrafficCredentials(proto_blob);
  brillo_loop_.RunOnce(false);

  EXPECT_TRUE(adaptor_->system_services_worker_.get());
  EXPECT_TRUE(adaptor_->system_services_worker_->IsRunning());

  int fds[2];
  EXPECT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);

  adaptor_->SetSystemTrafficCredentials(proto_blob);
  brillo_loop_.RunOnce(false);

  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_credentials());
  EXPECT_EQ(config.credentials().username(), kUser);
  EXPECT_EQ(config.credentials().password(), kPassword);
}

TEST_F(SystemProxyAdaptorTest, ShutDown) {
  EXPECT_CALL(*bus_, GetObjectProxy(patchpanel::kPatchPanelServiceName, _))
      .WillOnce(Return(mock_patchpanel_proxy_.get()));
  EXPECT_FALSE(adaptor_->system_services_worker_.get());
  SetSystemTrafficCredentialsRequest request;
  request.set_system_services_username(kUser);
  request.set_system_services_password(kPassword);
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  // First create a worker object.
  adaptor_->SetSystemTrafficCredentials(proto_blob);
  brillo_loop_.RunOnce(false);

  EXPECT_TRUE(adaptor_->system_services_worker_.get());
  EXPECT_TRUE(adaptor_->system_services_worker_->IsRunning());

  adaptor_->ShutDown();
  EXPECT_FALSE(adaptor_->system_services_worker_->IsRunning());
}

}  // namespace system_proxy
