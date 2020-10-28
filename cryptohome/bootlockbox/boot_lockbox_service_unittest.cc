// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for BootLockboxDbusAdaptor

#include <utility>

#include <gtest/gtest.h>

#include "bootlockbox/proto_bindings/boot_lockbox_rpc.pb.h"
#include "cryptohome/bootlockbox/boot_lockbox_dbus_adaptor.h"
#include "cryptohome/bootlockbox/mock_nvram_boot_lockbox.h"
#include "cryptohome/mock_tpm_init.h"

#include "dbus_adaptors/org.chromium.BootLockboxInterface.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {

// DBus Mock
class MockDBusBus : public dbus::Bus {
 public:
  MockDBusBus() : dbus::Bus(dbus::Bus::Options()) {}

  MOCK_METHOD(bool, Connect, (), (override));
  MOCK_METHOD(void, ShutdownAndBlock, (), (override));
  MOCK_METHOD(std::string,
              GetServiceOwnerAndBlock,
              (const std::string&, dbus::Bus::GetServiceOwnerOption),
              (override));
  MOCK_METHOD(dbus::ObjectProxy*,
              GetObjectProxy,
              (const std::string&, const dbus::ObjectPath&),
              (override));
};

// Captures the D-Bus Response object passed via DBusMethodResponse via
// ResponseSender.
//
// Example Usage:
//   ResponseCapturer capture;
//   SomeAsyncDBusMethod(capturer.CreateMethodResponse(), ...);
//   EXPECT_EQ(SomeErrorName, capture.response()->GetErrorName());
class ResponseCapturer {
 public:
  ResponseCapturer()
      : call_("org.chromium.BootLockboxInterfaceInterface",
              "PlaceholderDbusMethod"),
        weak_ptr_factory_(this) {
    call_.SetSerial(1);  // Placeholder serial is needed.
  }

  ~ResponseCapturer() = default;

  // Needs to be non-const, because some accessors like GetErrorName() are
  // non-const.
  dbus::Response* response() { return response_.get(); }

  template <typename... Types>
  std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<Types...>>
  CreateMethodResponse() {
    return std::make_unique<brillo::dbus_utils::DBusMethodResponse<Types...>>(
        &call_,
        base::Bind(&ResponseCapturer::Capture, weak_ptr_factory_.GetWeakPtr()));
  }

 private:
  void Capture(std::unique_ptr<dbus::Response> response) {
    DCHECK(!response_);
    response_ = std::move(response);
  }

  dbus::MethodCall call_;
  std::unique_ptr<dbus::Response> response_;
  base::WeakPtrFactory<ResponseCapturer> weak_ptr_factory_;
  DISALLOW_COPY_AND_ASSIGN(ResponseCapturer);
};

class BootLockboxDBusAdaptorTest : public ::testing::Test {
 public:
  BootLockboxDBusAdaptorTest() = default;
  ~BootLockboxDBusAdaptorTest() = default;

  void SetUp() override {
    scoped_refptr<MockDBusBus> bus(new MockDBusBus());
    boot_lockbox_dbus_adaptor_.reset(
        new BootLockboxDBusAdaptor(bus, &boot_lockbox_));
  }

 protected:
  NiceMock<MockTpmInit> tpm_init_;
  NiceMock<MockNVRamBootLockbox> boot_lockbox_;
  std::unique_ptr<BootLockboxDBusAdaptor> boot_lockbox_dbus_adaptor_;
};

TEST_F(BootLockboxDBusAdaptorTest, StoreBootLockbox) {
  cryptohome::StoreBootLockboxRequest store_request;
  store_request.set_key("test_key");
  store_request.set_data("test_data");

  EXPECT_CALL(boot_lockbox_, Store("test_key", "test_data", _))
      .WillOnce(Return(true));
  ResponseCapturer capturer;
  boot_lockbox_dbus_adaptor_->StoreBootLockbox(
      capturer.CreateMethodResponse<cryptohome::StoreBootLockboxReply>(),
      store_request);
}

TEST_F(BootLockboxDBusAdaptorTest, ReadBootLockbox) {
  // Read the data back.
  cryptohome::ReadBootLockboxRequest read_request;
  read_request.set_key("test_key");

  EXPECT_CALL(boot_lockbox_, Read("test_key", _, _)).WillOnce(Return(true));
  ResponseCapturer capturer;
  boot_lockbox_dbus_adaptor_->ReadBootLockbox(
      capturer.CreateMethodResponse<cryptohome::ReadBootLockboxReply>(),
      read_request);
}

TEST_F(BootLockboxDBusAdaptorTest, FinalizeBootLockbox) {
  cryptohome::FinalizeNVRamBootLockboxRequest request;
  EXPECT_CALL(boot_lockbox_, Finalize());
  ResponseCapturer capturer;
  boot_lockbox_dbus_adaptor_->FinalizeBootLockbox(
      capturer.CreateMethodResponse<cryptohome::FinalizeBootLockboxReply>(),
      request);
}

}  // namespace cryptohome
