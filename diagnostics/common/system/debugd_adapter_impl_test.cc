// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/macros.h>
#include <brillo/errors/error.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "debugd/dbus-proxy-mocks.h"
#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/common/system/debugd_adapter_impl.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrictMock;
using ::testing::WithArg;

namespace diagnostics {

namespace {

constexpr char kSmartAttributes[] = "attributes";
constexpr char kNvmeIdentity[] = "identify_controller";

class MockCallback {
 public:
  MOCK_METHOD(void,
              OnStringResultCallback,
              (const std::string&, brillo::Error*));
};

}  // namespace

class DebugdAdapterImplTest : public ::testing::Test {
 public:
  DebugdAdapterImplTest()
      : debugd_proxy_mock_(new StrictMock<org::chromium::debugdProxyMock>()),
        debugd_adapter_(std::make_unique<DebugdAdapterImpl>(
            std::unique_ptr<org::chromium::debugdProxyMock>(
                debugd_proxy_mock_))) {}

 protected:
  StrictMock<MockCallback> callback_;

  // Owned by |debugd_adapter_|.
  StrictMock<org::chromium::debugdProxyMock>* debugd_proxy_mock_;

  std::unique_ptr<DebugdAdapter> debugd_adapter_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DebugdAdapterImplTest);
};

// Tests that GetSmartAttributes calls callback with output on success.
TEST_F(DebugdAdapterImplTest, GetSmartAttributes) {
  constexpr char kResult[] = "S.M.A.R.T. status";
  EXPECT_CALL(*debugd_proxy_mock_, SmartctlAsync(kSmartAttributes, _, _, _))
      .WillOnce(WithArg<1>(Invoke(
          [kResult](const base::Callback<void(const std::string& /* result */)>&
                        success_callback) { success_callback.Run(kResult); })));
  EXPECT_CALL(callback_, OnStringResultCallback(kResult, nullptr));
  debugd_adapter_->GetSmartAttributes(base::Bind(
      &MockCallback::OnStringResultCallback, base::Unretained(&callback_)));
}

// Tests that GetSmartAttributes calls callback with error on failure.
TEST_F(DebugdAdapterImplTest, GetSmartAttributesError) {
  const brillo::ErrorPtr kError = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(*debugd_proxy_mock_, SmartctlAsync(kSmartAttributes, _, _, _))
      .WillOnce(WithArg<2>(Invoke(
          [error = kError.get()](
              const base::Callback<void(brillo::Error*)>& error_callback) {
            error_callback.Run(error);
          })));
  EXPECT_CALL(callback_, OnStringResultCallback("", kError.get()));
  debugd_adapter_->GetSmartAttributes(base::Bind(
      &MockCallback::OnStringResultCallback, base::Unretained(&callback_)));
}

// Tests that GetNvmeIdentity calls callback with output on success.
TEST_F(DebugdAdapterImplTest, GetNvmeIdentity) {
  constexpr char kResult[] = "NVMe identity data";
  EXPECT_CALL(*debugd_proxy_mock_, NvmeAsync(kNvmeIdentity, _, _, _))
      .WillOnce(WithArg<1>(Invoke(
          [kResult](const base::Callback<void(const std::string& /* result */)>&
                        success_callback) { success_callback.Run(kResult); })));
  EXPECT_CALL(callback_, OnStringResultCallback(kResult, nullptr));
  debugd_adapter_->GetNvmeIdentity(base::Bind(
      &MockCallback::OnStringResultCallback, base::Unretained(&callback_)));
}

// Tests that GetNvmeIdentity calls callback with error on failure.
TEST_F(DebugdAdapterImplTest, GetNvmeIdentityError) {
  const brillo::ErrorPtr kError = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(*debugd_proxy_mock_, NvmeAsync(kNvmeIdentity, _, _, _))
      .WillOnce(WithArg<2>(Invoke(
          [error = kError.get()](
              const base::Callback<void(brillo::Error*)>& error_callback) {
            error_callback.Run(error);
          })));
  EXPECT_CALL(callback_, OnStringResultCallback("", kError.get()));
  debugd_adapter_->GetNvmeIdentity(base::Bind(
      &MockCallback::OnStringResultCallback, base::Unretained(&callback_)));
}

}  // namespace diagnostics
