// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/test/bind_test_util.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "biod/dbus/biometrics_manager_proxy_base.h"

namespace biod {

using testing::ByMove;
using testing::Return;

class BiometricsManagerProxyBaseTest : public testing::Test {
 public:
  BiometricsManagerProxyBaseTest() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = new dbus::MockBus(options);

    mock_object_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), kBiodServiceName, dbus::ObjectPath(kBiodServicePath));

    // Set an expectation so that the MockBus will return our mock proxy.
    EXPECT_CALL(*mock_bus_, GetObjectProxy(kBiodServiceName,
                                           dbus::ObjectPath(kBiodServicePath)))
        .WillOnce(Return(mock_object_proxy_.get()));

    proxy_base_ = std::make_unique<BiometricsManagerProxyBase>(
        mock_bus_.get(), dbus::ObjectPath(kBiodServicePath));
  }

  void CallFinish(bool success) { proxy_base_->OnFinish(success); }

  void CallOnSignalConnected(bool success) {
    proxy_base_->OnSignalConnected("unused interface", "unused signal",
                                   success);
  }

  void CallOnSessionFailed() { proxy_base_->OnSessionFailed(nullptr); }

  std::unique_ptr<BiometricsManagerProxyBase> proxy_base_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  bool status_ = false;
};

namespace {

// Test that we can install and exercise a custom finish handler.
TEST_F(BiometricsManagerProxyBaseTest, RunFinishHandlerWithTrue) {
  status_ = false;
  proxy_base_->SetFinishHandler(
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  CallFinish(true);
  EXPECT_TRUE(status_);
}

// Test that we can install and exercise a custom finish handler.
TEST_F(BiometricsManagerProxyBaseTest, RunFinishHandlerWithFalse) {
  status_ = true;
  proxy_base_->SetFinishHandler(
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  CallFinish(false);
  EXPECT_FALSE(status_);
}

// Test that StartAuthSession returns nullptr if no dbus response.
TEST_F(BiometricsManagerProxyBaseTest, StartAuthSessionNoResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock)
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  EXPECT_EQ(proxy_base_->StartAuthSession(), nullptr);
}

// Test that StartAuthSession succeeds and the object proxy saved by
// StartAuthSession is what the mock provides.
TEST_F(BiometricsManagerProxyBaseTest, StartAuthSessionGetSessionProxy) {
  // The path must be correctly formatted for the writer to accept it.
  const dbus::ObjectPath auth_session_path("/org/chromium/Foo/AuthSession");
  auto fake_response = dbus::Response::CreateEmpty();
  dbus::MessageWriter writer(fake_response.get());
  writer.AppendObjectPath(auth_session_path);

  scoped_refptr<dbus::MockObjectProxy> auth_session_proxy =
      new dbus::MockObjectProxy(mock_bus_.get(), kBiodServiceName,
                                auth_session_path);

  // Set the underlying mock proxy to return our fake_response, and set the
  // mock bus to return the predefined ObjectProxy once it sees that path,
  // which the class under test will extract from the fake_response.
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock)
      .WillOnce(Return(ByMove(std::move(fake_response))));
  EXPECT_CALL(*mock_bus_, GetObjectProxy(kBiodServiceName, auth_session_path))
      .WillOnce(Return(auth_session_proxy.get()));

  EXPECT_EQ(proxy_base_->StartAuthSession(), auth_session_proxy.get());
}

// Test that OnSessionFailed will call on_finish_ with false
TEST_F(BiometricsManagerProxyBaseTest, OnSessionFailed) {
  status_ = true;
  proxy_base_->SetFinishHandler(
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  CallOnSessionFailed();
  EXPECT_FALSE(status_);
}

// Test that OnSignalConnected if failed will call on_finish_ with false
TEST_F(BiometricsManagerProxyBaseTest, OnSignalConnectFailed) {
  status_ = true;
  proxy_base_->SetFinishHandler(
      base::BindLambdaForTesting([this](bool success) { status_ = success; }));
  CallOnSignalConnected(false);
  EXPECT_FALSE(status_);
}

}  // namespace
}  // namespace biod
