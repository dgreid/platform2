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

#include "diagnostics/common/system/fake_powerd_adapter.h"
#include "diagnostics/cros_healthd/events/lid_events_impl.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

using ::testing::Invoke;
using ::testing::StrictMock;

class MockCrosHealthdLidObserver : public mojo_ipc::CrosHealthdLidObserver {
 public:
  explicit MockCrosHealthdLidObserver(
      mojo_ipc::CrosHealthdLidObserverRequest request)
      : binding_{this /* impl */, std::move(request)} {
    DCHECK(binding_.is_bound());
  }
  MockCrosHealthdLidObserver(const MockCrosHealthdLidObserver&) = delete;
  MockCrosHealthdLidObserver& operator=(const MockCrosHealthdLidObserver&) =
      delete;

  MOCK_METHOD(void, OnLidClosed, (), (override));
  MOCK_METHOD(void, OnLidOpened, (), (override));

 private:
  mojo::Binding<mojo_ipc::CrosHealthdLidObserver> binding_;
};

}  // namespace

// Tests for the LidEventsImpl class.
class LidEventsImplTest : public testing::Test {
 protected:
  LidEventsImplTest() = default;
  LidEventsImplTest(const LidEventsImplTest&) = delete;
  LidEventsImplTest& operator=(const LidEventsImplTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());

    // Before any observers have been added, we shouldn't have subscribed to
    // powerd_adapter.
    ASSERT_FALSE(fake_adapter()->HasLidObserver(&lid_events_impl_));

    mojo_ipc::CrosHealthdLidObserverPtr observer_ptr;
    mojo_ipc::CrosHealthdLidObserverRequest observer_request(
        mojo::MakeRequest(&observer_ptr));
    observer_ = std::make_unique<StrictMock<MockCrosHealthdLidObserver>>(
        std::move(observer_request));
    lid_events_impl_.AddObserver(std::move(observer_ptr));
    // Now that an observer has been added, we should have subscribed to
    // powerd_adapter.
    ASSERT_TRUE(fake_adapter()->HasLidObserver(&lid_events_impl_));
  }

  LidEventsImpl* lid_events_impl() { return &lid_events_impl_; }

  FakePowerdAdapter* fake_adapter() {
    return mock_context_.fake_powerd_adapter();
  }

  MockCrosHealthdLidObserver* mock_observer() { return observer_.get(); }

  void DestroyMojoObserver() {
    observer_.reset();

    // Make sure |lid_events_impl_| gets a chance to observe the connection
    // error.
    task_environment_.RunUntilIdle();
  }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<StrictMock<MockCrosHealthdLidObserver>> observer_;
  LidEventsImpl lid_events_impl_{&mock_context_};
};

// Test that we can receive lid closed events.
TEST_F(LidEventsImplTest, ReceiveLidClosedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnLidClosed()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_adapter()->EmitLidClosedSignal();

  run_loop.Run();
}

// Test that we can receive lid opened events.
TEST_F(LidEventsImplTest, ReceiveLidOpenedEvent) {
  base::RunLoop run_loop;
  EXPECT_CALL(*mock_observer(), OnLidOpened()).WillOnce(Invoke([&]() {
    run_loop.Quit();
  }));

  fake_adapter()->EmitLidOpenedSignal();

  run_loop.Run();
}

// Test that LidEvents unsubscribes to PowerdAdapter when LidEvents loses all of
// its mojo observers.
TEST_F(LidEventsImplTest, UnsubscribeFromPowerdAdapterWhenAllObserversLost) {
  DestroyMojoObserver();

  // Emit an event, so that LidEventsImpl has a chance to check for any
  // remaining mojo observers.
  fake_adapter()->EmitLidClosedSignal();

  EXPECT_FALSE(fake_adapter()->HasLidObserver(lid_events_impl()));
}

}  // namespace diagnostics
