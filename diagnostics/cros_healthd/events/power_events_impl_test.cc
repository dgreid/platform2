// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/bindings/interface_request.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>

#include "diagnostics/common/system/fake_powerd_adapter.h"
#include "diagnostics/cros_healthd/events/power_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

using ::testing::Invoke;
using ::testing::StrictMock;

class MockCrosHealthdPowerObserver : public mojo_ipc::CrosHealthdPowerObserver {
 public:
  MockCrosHealthdPowerObserver(
      mojo_ipc::CrosHealthdPowerObserverRequest request)
      : binding_{this /* impl */, std::move(request)} {
    DCHECK(binding_.is_bound());
  }
  MockCrosHealthdPowerObserver(const MockCrosHealthdPowerObserver&) = delete;
  MockCrosHealthdPowerObserver& operator=(const MockCrosHealthdPowerObserver&) =
      delete;

  MOCK_METHOD(void, OnAcInserted, (), (override));
  MOCK_METHOD(void, OnAcRemoved, (), (override));
  MOCK_METHOD(void, OnOsSuspend, (), (override));
  MOCK_METHOD(void, OnOsResume, (), (override));

 private:
  mojo::Binding<mojo_ipc::CrosHealthdPowerObserver> binding_;
};

}  // namespace

// Tests for the PowerEventsImpl class.
class PowerEventsImplTest : public testing::Test {
 protected:
  PowerEventsImplTest() { mojo::core::Init(); }
  PowerEventsImplTest(const PowerEventsImplTest&) = delete;
  PowerEventsImplTest& operator=(const PowerEventsImplTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());

    // Before any observers have been added, we shouldn't have subscribed to
    // powerd_adapter.
    ASSERT_FALSE(fake_adapter()->HasPowerObserver(&power_events_impl_));

    mojo_ipc::CrosHealthdPowerObserverPtr observer_ptr;
    mojo_ipc::CrosHealthdPowerObserverRequest observer_request(
        mojo::MakeRequest(&observer_ptr));
    observer_ = std::make_unique<StrictMock<MockCrosHealthdPowerObserver>>(
        std::move(observer_request));
    power_events_impl_.AddObserver(std::move(observer_ptr));
    // Now that an observer has been added, we should have subscribed to
    // powerd_adapter.
    ASSERT_TRUE(fake_adapter()->HasPowerObserver(&power_events_impl_));
  }

  PowerEventsImpl* power_events_impl() { return &power_events_impl_; }

  FakePowerdAdapter* fake_adapter() {
    return mock_context_.fake_powerd_adapter();
  }

  MockCrosHealthdPowerObserver* mock_observer() { return observer_.get(); }

  base::test::TaskEnvironment* task_environment() { return &task_environment_; }

  void DestroyMojoObserver() {
    observer_.reset();

    // Make sure |power_events_impl_| gets a chance to observe the connection
    // error.
    task_environment_.RunUntilIdle();
  }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<StrictMock<MockCrosHealthdPowerObserver>> observer_;
  PowerEventsImpl power_events_impl_{&mock_context_};
};

// Test that we can receive AC inserted events from powerd's AC proto.
TEST_F(PowerEventsImplTest, ReceiveAcInsertedEventFromAcProto) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAcInserted()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  power_manager::PowerSupplyProperties power_supply;
  power_supply.set_external_power(power_manager::PowerSupplyProperties::AC);
  fake_adapter()->EmitPowerSupplyPollSignal(power_supply);

  run_loop.Run();
}

// Test that we can receive AC inserted events from powerd's USB proto.
TEST_F(PowerEventsImplTest, ReceiveAcInsertedEventFromUsbProto) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAcInserted()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  power_manager::PowerSupplyProperties power_supply;
  power_supply.set_external_power(power_manager::PowerSupplyProperties::USB);
  fake_adapter()->EmitPowerSupplyPollSignal(power_supply);

  run_loop.Run();
}

// Test that we can receive AC removed events.
TEST_F(PowerEventsImplTest, ReceiveAcRemovedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAcRemoved()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  power_manager::PowerSupplyProperties power_supply;
  power_supply.set_external_power(
      power_manager::PowerSupplyProperties::DISCONNECTED);
  fake_adapter()->EmitPowerSupplyPollSignal(power_supply);

  run_loop.Run();
}

// Test that we can receive OS suspend events from suspend imminent signals.
TEST_F(PowerEventsImplTest, ReceiveOsSuspendEventFromSuspendImminent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnOsSuspend()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  power_manager::SuspendImminent suspend_imminent;
  fake_adapter()->EmitSuspendImminentSignal(suspend_imminent);

  run_loop.Run();
}

// Test that we can receive OS suspend events from dark suspend imminent
// signals.
TEST_F(PowerEventsImplTest, ReceiveOsSuspendEventFromDarkSuspendImminent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnOsSuspend()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  power_manager::SuspendImminent suspend_imminent;
  fake_adapter()->EmitDarkSuspendImminentSignal(suspend_imminent);

  run_loop.Run();
}

// Test that we can receive OS resume events.
TEST_F(PowerEventsImplTest, ReceiveOsResumeEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnOsResume()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  power_manager::SuspendDone suspend_done;
  fake_adapter()->EmitSuspendDoneSignal(suspend_done);

  run_loop.Run();
}

// Test that powerd events without external power are ignored.
TEST_F(PowerEventsImplTest, IgnorePayloadWithoutExternalPower) {
  power_manager::PowerSupplyProperties power_supply;
  fake_adapter()->EmitPowerSupplyPollSignal(power_supply);

  task_environment()->RunUntilIdle();
}

// Test that multiple of the same powerd events in a row are only reported once.
TEST_F(PowerEventsImplTest, MultipleIdenticalPayloadsReportedOnlyOnce) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnAcRemoved()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  // Make the first call, which should be reported.
  power_manager::PowerSupplyProperties power_supply;
  power_supply.set_external_power(
      power_manager::PowerSupplyProperties::DISCONNECTED);
  fake_adapter()->EmitPowerSupplyPollSignal(power_supply);

  run_loop.Run();

  // A second identical call should be ignored.
  fake_adapter()->EmitPowerSupplyPollSignal(power_supply);

  task_environment()->RunUntilIdle();

  // Changing the type of external power should again be reported.
  base::RunLoop run_loop2;
  EXPECT_CALL(*mock_observer(), OnAcInserted()).WillOnce(Invoke([&]() {
    run_loop2.Quit();
  }));

  power_supply.set_external_power(power_manager::PowerSupplyProperties::AC);
  fake_adapter()->EmitPowerSupplyPollSignal(power_supply);

  run_loop2.Run();
}

// Test that PowerEvents unsubscribes to PowerdAdapter when PowerEvents loses
// all of its mojo observers.
TEST_F(PowerEventsImplTest, UnsubscribeFromPowerdAdapterWhenAllObserversLost) {
  DestroyMojoObserver();

  // Emit an event, so that PowerEventsImpl has a chance to check for any
  // remaining mojo observers.
  power_manager::SuspendDone suspend_done;
  fake_adapter()->EmitSuspendDoneSignal(suspend_done);

  EXPECT_FALSE(fake_adapter()->HasPowerObserver(power_events_impl()));
}

}  // namespace diagnostics
