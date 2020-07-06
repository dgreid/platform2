// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/bindings/interface_request.h>

#include "diagnostics/common/system/bluetooth_client.h"
#include "diagnostics/common/system/fake_bluetooth_client.h"
#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

using ::testing::Invoke;
using ::testing::StrictMock;

void PropertyChanged(const std::string& property_name) {}

std::unique_ptr<BluetoothClient::AdapterProperties> CreateAdapterProperties() {
  auto properties = std::make_unique<BluetoothClient::AdapterProperties>(
      nullptr, base::Bind(&PropertyChanged));
  properties->name.ReplaceValue("hci0");
  properties->address.ReplaceValue("aa:bb:cc:dd:ee:ff");
  properties->powered.ReplaceValue(true);
  return properties;
}

std::unique_ptr<BluetoothClient::DeviceProperties> CreateDeviceProperties() {
  auto properties = std::make_unique<BluetoothClient::DeviceProperties>(
      nullptr, base::Bind(&PropertyChanged));
  properties->name.ReplaceValue("keyboard");
  properties->address.ReplaceValue("70:88:6B:92:34:70");
  properties->connected.ReplaceValue(true);
  properties->adapter.ReplaceValue(dbus::ObjectPath("/org/bluez/hci0"));
  return properties;
}

class MockCrosHealthdBluetoothObserver
    : public mojo_ipc::CrosHealthdBluetoothObserver {
 public:
  MockCrosHealthdBluetoothObserver(
      mojo_ipc::CrosHealthdBluetoothObserverRequest request)
      : binding_{this /* impl */, std::move(request)} {
    DCHECK(binding_.is_bound());
  }
  MockCrosHealthdBluetoothObserver(const MockCrosHealthdBluetoothObserver&) =
      delete;
  MockCrosHealthdBluetoothObserver& operator=(
      const MockCrosHealthdBluetoothObserver&) = delete;

  MOCK_METHOD(void, OnAdapterAdded, (), (override));
  MOCK_METHOD(void, OnAdapterRemoved, (), (override));
  MOCK_METHOD(void, OnAdapterPropertyChanged, (), (override));
  MOCK_METHOD(void, OnDeviceAdded, (), (override));
  MOCK_METHOD(void, OnDeviceRemoved, (), (override));
  MOCK_METHOD(void, OnDevicePropertyChanged, (), (override));

 private:
  mojo::Binding<mojo_ipc::CrosHealthdBluetoothObserver> binding_;
};

}  // namespace

// Tests for the BluetoothEventsImpl class.
class BluetoothEventsImplTest : public testing::Test {
 protected:
  BluetoothEventsImplTest() { mojo::core::Init(); }
  BluetoothEventsImplTest(const BluetoothEventsImplTest&) = delete;
  BluetoothEventsImplTest& operator=(const BluetoothEventsImplTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());

    // Before any observers have been added, we shouldn't have subscribed to
    // BluetoothClient.
    ASSERT_FALSE(fake_bluetooth_client()->HasObserver(&bluetooth_events_impl_));

    mojo_ipc::CrosHealthdBluetoothObserverPtr observer_ptr;
    mojo_ipc::CrosHealthdBluetoothObserverRequest observer_request(
        mojo::MakeRequest(&observer_ptr));
    observer_ = std::make_unique<StrictMock<MockCrosHealthdBluetoothObserver>>(
        std::move(observer_request));
    bluetooth_events_impl_.AddObserver(std::move(observer_ptr));
    // Now that an observer has been added, we should have subscribed to
    // BluetoothClient.
    ASSERT_TRUE(fake_bluetooth_client()->HasObserver(&bluetooth_events_impl_));
  }

  BluetoothEventsImpl* bluetooth_events_impl() {
    return &bluetooth_events_impl_;
  }

  FakeBluetoothClient* fake_bluetooth_client() {
    return mock_context_.fake_bluetooth_client();
  }

  MockCrosHealthdBluetoothObserver* mock_observer() { return observer_.get(); }

  dbus::ObjectPath adapter_path() {
    return dbus::ObjectPath("/org/bluez/hci0");
  }

  dbus::ObjectPath device_path() {
    return dbus::ObjectPath("/org/bluez/hci0/dev_70_88_6B_92_34_70");
  }

  void DestroyMojoObserver() {
    observer_.reset();

    // Make sure |bluetooth_events_impl_| gets a chance to observe the
    // connection error.
    task_environment_.RunUntilIdle();
  }

 private:
  base::test::TaskEnvironment task_environment_;

  MockContext mock_context_;
  BluetoothEventsImpl bluetooth_events_impl_{&mock_context_};
  std::unique_ptr<StrictMock<MockCrosHealthdBluetoothObserver>> observer_;
};

// Test that we can receive an adapter added event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterAddedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdapterAdded()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_bluetooth_client()->EmitAdapterAdded(adapter_path(),
                                            *CreateAdapterProperties());

  run_loop.Run();
}

// Test that we can receive an adapter removed event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterRemovedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdapterRemoved()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_bluetooth_client()->EmitAdapterRemoved(adapter_path());

  run_loop.Run();
}

// Test that we can receive an adapter property changed event.
TEST_F(BluetoothEventsImplTest, ReceiveAdapterPropertyChangedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAdapterPropertyChanged())
      .WillOnce(Invoke([&]() { run_loop.Quit(); }));

  fake_bluetooth_client()->EmitAdapterPropertyChanged(
      adapter_path(), *CreateAdapterProperties());

  run_loop.Run();
}

// Test that we can receive a device added event.
TEST_F(BluetoothEventsImplTest, ReceiveDeviceAddedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnDeviceAdded()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_bluetooth_client()->EmitDeviceAdded(device_path(),
                                           *CreateDeviceProperties());

  run_loop.Run();
}

// Test that we can receive a device removed event.
TEST_F(BluetoothEventsImplTest, ReceiveDeviceRemovedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnDeviceRemoved()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_bluetooth_client()->EmitDeviceRemoved(device_path());

  run_loop.Run();
}

// Test that we can receive a device property changed event.
TEST_F(BluetoothEventsImplTest, ReceiveDevicePropertyChangedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnDevicePropertyChanged())
      .WillOnce(Invoke([&]() { run_loop.Quit(); }));

  fake_bluetooth_client()->EmitDevicePropertyChanged(device_path(),
                                                     *CreateDeviceProperties());

  run_loop.Run();
}

// Test that BluetoothEvents unsubscribes from BluetoothClient when
// BluetoothEvents loses all of its Mojo observers.
TEST_F(BluetoothEventsImplTest,
       UnsubscribeFromBluetoothClientWhenAllObserversLost) {
  DestroyMojoObserver();

  // Emit an event so that BluetoothEventsImpl has a chance to check for any
  // remaining Mojo observers.
  fake_bluetooth_client()->EmitAdapterRemoved(adapter_path());

  EXPECT_FALSE(fake_bluetooth_client()->HasObserver(bluetooth_events_impl()));
}

}  // namespace diagnostics
