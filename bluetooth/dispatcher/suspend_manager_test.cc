// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>

#include "bluetooth/common/runtime_flags.h"
#include "bluetooth/dispatcher/suspend_manager.h"
#include "power_manager/proto_bindings/suspend.pb.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;

ACTION_TEMPLATE(MovePointee,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(pointer)) {
  *pointer = std::move(*(::std::get<k>(args)));
}

namespace bluetooth {

namespace {

// Some arbitrary D-Bus message serial number. Required for mocking D-Bus calls.
const int kDBusSerial = 111;

// Some constants for power manager suspend delay.
const int kDelayId = 222;
const int kSuspendId = 333;

}  // namespace

class SuspendManagerTest : public ::testing::Test {
 public:
  SuspendManagerTest() = default;

  void SetUp() override {
    flags_ = std::make_unique<RuntimeFlags>();
    flags_->Init();
    if (!flags_->Get("enable-suspend-management")) {
      GTEST_SKIP();
    }
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::MockBus(options);
    suspend_manager_ = std::make_unique<SuspendManager>(bus_);

    SetupMockBus();
  }

  void SetupMockBus() {
    // Mock power manager and bluez D-Bus proxy.
    power_manager_proxy_ = new dbus::MockObjectProxy(
        bus_.get(), power_manager::kPowerManagerServiceName,
        dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    bluez_proxy_ = new dbus::MockObjectProxy(
        bus_.get(), bluez_object_manager::kBluezObjectManagerServiceName,
        dbus::ObjectPath(SuspendManager::kBluetoothAdapterObjectPath));
    EXPECT_CALL(*bus_,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(Return(power_manager_proxy_.get()));
    EXPECT_CALL(
        *bus_,
        GetObjectProxy(
            bluez_object_manager::kBluezObjectManagerServiceName,
            dbus::ObjectPath(SuspendManager::kBluetoothAdapterObjectPath)))
        .WillOnce(Return(bluez_proxy_.get()));

    // Save the callbacks of various power manager events so we can call them
    // to test later.
    EXPECT_CALL(*power_manager_proxy_, DoWaitForServiceToBeAvailable(_))
        .WillOnce(MovePointee<0>(&power_manager_available_callback_));
    EXPECT_CALL(*power_manager_proxy_, SetNameOwnerChangedCallback(_))
        .WillOnce(SaveArg<0>(&power_manager_name_owner_changed_callback_));
    EXPECT_CALL(*power_manager_proxy_,
                DoConnectToSignal(power_manager::kPowerManagerInterface,
                                  power_manager::kSuspendImminentSignal, _, _))
        .WillOnce(SaveArg<2>(&suspend_imminent_signal_callback_));
    EXPECT_CALL(*power_manager_proxy_,
                DoConnectToSignal(power_manager::kPowerManagerInterface,
                                  power_manager::kSuspendDoneSignal, _, _))
        .WillOnce(SaveArg<2>(&suspend_done_signal_callback_));

    // Initialize the suspend manager. This should trigger it to register
    // callbacks to power manager events.
    suspend_manager_->Init();
    // Check that it really has registered the callbacks and we saved them.
    ASSERT_FALSE(power_manager_available_callback_.is_null());
    ASSERT_FALSE(suspend_imminent_signal_callback_.is_null());
    ASSERT_FALSE(suspend_done_signal_callback_.is_null());
  }

  // This stub responds to any D-Bus method call to bluez. At this moment it
  // does not need to do anything other than immediately calling the success
  // callback if the method name is expected. It will immediately fail the test
  // if it receives unexpected method name.
  void StubBluezCallMethod(dbus::MethodCall* method_call,
                           int timeout_ms,
                           dbus::ObjectProxy::ResponseCallback* callback) {
    if (!expected_bluez_method_call_)
      FAIL() << "Bluez shouldn't receive any method call.";

    // Set any fake message serial.
    method_call->SetSerial(kDBusSerial);

    if (method_call->GetInterface() ==
            bluetooth_adapter::kBluetoothAdapterInterface &&
        method_call->GetMember() == *expected_bluez_method_call_) {
      if (simulates_bluez_long_return_) {
        // Pretend that bluez can't call the callback now.
        bluez_callback_ =
            base::BindOnce(std::move(*callback),
                           dbus::Response::FromMethodCall(method_call).get());
      } else {
        std::move(*callback).Run(
            dbus::Response::FromMethodCall(method_call).get());
      }
      return;
    }

    FAIL() << "Bluez doesn't expect method call " << method_call->GetInterface()
           << "." << method_call->GetMember();
  }

  // Triggers the callback of the last saved bluez in-progress call.
  void CallBluezCallback() { std::move(bluez_callback_).Run(); }

  // This stub responds to any D-Bus method call to power manager. It handles
  // fake implementations of RegisterSuspendDelay and HandleSuspendReadiness.
  // It fails the test for other unimplemented methods.
  void StubPowerManagerCallMethod(
      dbus::MethodCall* method_call,
      int timeout_ms,
      dbus::ObjectProxy::ResponseCallback* callback) {
    if (!is_power_manager_available_)
      FAIL() << "Power manager is not available.";

    // Set any fake message serial.
    method_call->SetSerial(kDBusSerial);

    if (method_call->GetInterface() == power_manager::kPowerManagerInterface) {
      if (method_call->GetMember() ==
          power_manager::kRegisterSuspendDelayMethod) {
        StubPowerManagerCallRegisterSuspendDelay(method_call,
                                                 std::move(*callback));
        return;
      }
      if (method_call->GetMember() ==
          power_manager::kHandleSuspendReadinessMethod) {
        StubPowerManagerCallHandleSuspendReadiness(method_call,
                                                   std::move(*callback));
        return;
      }
    }

    // Other method calls are not implemented by this stub.
    FAIL() << "Stub doesn't implement method " << method_call->GetInterface()
           << "." << method_call->GetMember();
  }

  void StubPowerManagerCallMethodWithErrorCallback(
      dbus::MethodCall* method_call,
      int timeout_ms,
      dbus::ObjectProxy::ResponseCallback* callback,
      dbus::ObjectProxy::ErrorCallback* error_callback) {
    StubPowerManagerCallMethod(method_call, timeout_ms, callback);
  }

 protected:
  // A fake implementation of power manager's RegisterSuspendDelay.
  // It returns an arbitrary delay id to be verified later at
  // HandleSuspendReadiness.
  void StubPowerManagerCallRegisterSuspendDelay(
      dbus::MethodCall* method_call,
      dbus::ObjectProxy::ResponseCallback callback) {
    power_manager::RegisterSuspendDelayRequest request;
    dbus::MessageReader reader(method_call);
    reader.PopArrayOfBytesAsProto(&request);

    std::unique_ptr<dbus::Response> response =
        dbus::Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());
    power_manager::RegisterSuspendDelayReply reply;
    reply.set_delay_id(kDelayId);
    writer.AppendProtoAsArrayOfBytes(reply);

    std::move(callback).Run(response.get());
  }

  // A fake implementation of power manager's HandleSuspendReadiness.
  // It fails the test if it doesn't receive the expected SuspendReadinessInfo.
  void StubPowerManagerCallHandleSuspendReadiness(
      dbus::MethodCall* method_call,
      dbus::ObjectProxy::ResponseCallback callback) {
    power_manager::SuspendReadinessInfo suspend_readiness;
    dbus::MessageReader reader(method_call);
    reader.PopArrayOfBytesAsProto(&suspend_readiness);
    if (expected_suspend_readiness_) {
      EXPECT_EQ(expected_suspend_readiness_->delay_id(),
                suspend_readiness.delay_id());
      EXPECT_EQ(expected_suspend_readiness_->suspend_id(),
                suspend_readiness.suspend_id());
    } else {
      FAIL() << "HandleSuspendReadiness shouldn't be reached";
    }

    std::move(callback).Run(dbus::Response::FromMethodCall(method_call).get());
  }

  // Simulates SuspendImminent signal to suspend manager.
  void EmitSuspendImminentSignal(int suspend_id) {
    dbus::Signal signal(power_manager::kPowerManagerInterface,
                        power_manager::kSuspendImminentSignal);
    dbus::MessageWriter writer(&signal);
    power_manager::SuspendImminent suspend_imminent;
    suspend_imminent.set_suspend_id(suspend_id);
    writer.AppendProtoAsArrayOfBytes(suspend_imminent);
    suspend_imminent_signal_callback_.Run(&signal);
  }

  // Simulates SuspendDone signal to suspend manager.
  void EmitSuspendDoneSignal(int suspend_id) {
    dbus::Signal signal(power_manager::kPowerManagerInterface,
                        power_manager::kSuspendDoneSignal);
    dbus::MessageWriter writer(&signal);
    power_manager::SuspendDone suspend_done;
    suspend_done.set_suspend_id(suspend_id);
    writer.AppendProtoAsArrayOfBytes(suspend_done);
    suspend_done_signal_callback_.Run(&signal);
  }

  // Simulates power manager becoming available.
  void TriggerPowerManagerAvailable(bool is_available) {
    is_power_manager_available_ = is_available;
    std::move(power_manager_available_callback_).Run(is_available);
  }

  // Simulates power manager D-Bus name owner changed.
  void TriggerPowerManagerNameOwnerChanged(const std::string& old_owner,
                                           const std::string& new_owner) {
    is_power_manager_available_ = !new_owner.empty();
    power_manager_name_owner_changed_callback_.Run(old_owner, new_owner);
  }

  std::unique_ptr<RuntimeFlags> flags_;

  // Keeps track whether we have simulated power manager available event.
  // Needed to decide if stub power manager should reject any method calls.
  bool is_power_manager_available_ = false;

  // If true, the bluez stub will not call the callback immediately, but instead
  // will wait until it is told so. Needed to exercise bluez in-progress
  // scenarios.
  bool simulates_bluez_long_return_ = false;
  // Keeps the in-progress bluez callback to be executed later.
  base::OnceClosure bluez_callback_;

  // The mocked DBus bus and proxies.
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> power_manager_proxy_;
  scoped_refptr<dbus::MockObjectProxy> bluez_proxy_;

  // Keeps the callbacks of power manager events.
  dbus::ObjectProxy::WaitForServiceToBeAvailableCallback
      power_manager_available_callback_;
  dbus::ObjectProxy::NameOwnerChangedCallback
      power_manager_name_owner_changed_callback_;
  dbus::ObjectProxy::SignalCallback suspend_imminent_signal_callback_;
  dbus::ObjectProxy::SignalCallback suspend_done_signal_callback_;

  // The expected parameter to power manager's HandleSuspendReadiness.
  // Null means that there should be no call to HandleSuspendReadiness.
  // TODO(sonnysasaka): Migrate this to base::Optional when it's available.
  std::unique_ptr<power_manager::SuspendReadinessInfo>
      expected_suspend_readiness_;

  // The expected method call to bluez. Null means that there should be no
  // method calls to bluez.
  // TODO(sonnysasaka): Migrate this to base::Optional when it's available.
  std::unique_ptr<std::string> expected_bluez_method_call_;

  // The SuspendManager under test.
  std::unique_ptr<SuspendManager> suspend_manager_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SuspendManagerTest);
};

TEST_F(SuspendManagerTest, PowerManagerNotAvailable) {
  // There should be no calls to power manager.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethodWithErrorCallback(_, _, _, _))
      .Times(0);
  EXPECT_CALL(*power_manager_proxy_, DoCallMethod(_, _, _)).Times(0);
  // Start without power manager available event.

  // Bluez HandleSuspendImminent shouldn't be called.
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _)).Times(0);
  // HandleSuspendReadiness shouldn't be called.
  expected_suspend_readiness_.reset();

  // Trigger suspend imminent signal.
  EmitSuspendImminentSignal(kSuspendId);

  // Trigger suspend done signal.
  EmitSuspendDoneSignal(kSuspendId);
}

TEST_F(SuspendManagerTest, PowerManagerAvailableFailure) {
  // There should be no calls to power manager.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethodWithErrorCallback(_, _, _, _))
      .Times(0);
  EXPECT_CALL(*power_manager_proxy_, DoCallMethod(_, _, _)).Times(0);
  // Start with power manager available event, but it's a failure event.
  TriggerPowerManagerAvailable(false);

  // Bluez HandleSuspendImminent shouldn't be called.
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _)).Times(0);
  // HandleSuspendReadiness shouldn't be called.
  expected_suspend_readiness_.reset();

  // Trigger suspend imminent signal.
  EmitSuspendImminentSignal(kSuspendId);

  // Trigger suspend done signal.
  EmitSuspendDoneSignal(kSuspendId);
}

TEST_F(SuspendManagerTest, PowerManagerAvailableSuccess) {
  // Power manager should receive RegisterSuspendDelay after it's available.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(
          this,
          &SuspendManagerTest::StubPowerManagerCallMethodWithErrorCallback));
  // Start with power manager available event.
  TriggerPowerManagerAvailable(true);

  // Bluez HandleSuspendImminent should be called after SuspendImminent signal.
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendImminent);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));
  // HandleSuspendReadiness should be called after HandleSuspendImminent
  // finishes.
  expected_suspend_readiness_ =
      std::make_unique<power_manager::SuspendReadinessInfo>();
  expected_suspend_readiness_->set_delay_id(kDelayId);
  expected_suspend_readiness_->set_suspend_id(kSuspendId);
  EXPECT_CALL(*power_manager_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubPowerManagerCallMethod));

  // Trigger suspend imminent signal.
  EmitSuspendImminentSignal(kSuspendId);

  // Bluez HandleSuspendDone should be called after SuspendDone signal.
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendDone);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));

  // Trigger suspend done signal.
  EmitSuspendDoneSignal(kSuspendId);
}

TEST_F(SuspendManagerTest, PowerManagerAvailableTwice) {
  // Power manager should receive one RegisterSuspendDelay after it's available
  // even though we receive double available signals.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(
          this,
          &SuspendManagerTest::StubPowerManagerCallMethodWithErrorCallback));
  // These two events could both happen.
  TriggerPowerManagerAvailable(true);
  TriggerPowerManagerNameOwnerChanged("", ":1.234");
}

TEST_F(SuspendManagerTest, PowerManagerNameOwnerChanged) {
  // Power manager should receive RegisterSuspendDelay after it's available.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(
          this,
          &SuspendManagerTest::StubPowerManagerCallMethodWithErrorCallback));
  // Start with power manager name owner changed callback with a new name.
  TriggerPowerManagerNameOwnerChanged("", ":1.234");

  // Bluez HandleSuspendImminent should be called after SuspendImminent signal.
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendImminent);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));
  // HandleSuspendReadiness should be called after HandleSuspendImminent
  // finishes.
  expected_suspend_readiness_ =
      std::make_unique<power_manager::SuspendReadinessInfo>();
  expected_suspend_readiness_->set_delay_id(kDelayId);
  expected_suspend_readiness_->set_suspend_id(kSuspendId);
  EXPECT_CALL(*power_manager_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubPowerManagerCallMethod));

  // Trigger suspend imminent signal.
  EmitSuspendImminentSignal(kSuspendId);

  // Bluez HandleSuspendDone should be called after SuspendDone signal.
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendDone);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));

  // Trigger suspend done signal.
  EmitSuspendDoneSignal(kSuspendId);

  // Bluez HandleSuspendDone should be called after power manager is down
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendDone);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));

  // Simulate power manager losing name owner. The subsequent SuspendImminent
  // signal should be ignored before power manager is alive again.
  TriggerPowerManagerNameOwnerChanged(":1.234", "");

  // Bluez HandleSuspendImminent shouldn't be called.
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _)).Times(0);
  // HandleSuspendReadiness shouldn't be called.
  expected_suspend_readiness_.reset();
  // Trigger suspend imminent signal.
  EmitSuspendImminentSignal(kSuspendId);

  // Simulate power manager getting name ownership.
  // Power manager should receive RegisterSuspendDelay after it's available.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(
          this,
          &SuspendManagerTest::StubPowerManagerCallMethodWithErrorCallback));
  TriggerPowerManagerNameOwnerChanged("", ":1.345");
}

// SignalDone happens while HandleSuspendImminent is still in progress.
TEST_F(SuspendManagerTest, PowerManagerSuspendDoneEarly) {
  // Power manager should receive RegisterSuspendDelay after it's available.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(
          this,
          &SuspendManagerTest::StubPowerManagerCallMethodWithErrorCallback));
  // Start with power manager available event.
  TriggerPowerManagerAvailable(true);

  // Tell our bluez stub to pretend to not return immediately so that we can
  // exercise bluez in-progress scenarios.
  simulates_bluez_long_return_ = true;

  // Bluez HandleSuspendImminent should be called after SuspendImminent signal.
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendImminent);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));
  // HandleSuspendReadiness shouldn't be called yet, since bluez is still in
  // progress doing HandleSuspendImminent.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethod(_, _, _)).Times(0);

  // Trigger suspend imminent signal.
  EmitSuspendImminentSignal(kSuspendId);

  // Bluez HandleSuspendDone shouldn't be called after SuspendDone signal,
  // since the current HandleSuspendImminent is still in progress.
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _)).Times(0);

  // Trigger suspend done signal while HandleSuspendImminent is still in
  // progress.
  EmitSuspendDoneSignal(kSuspendId);

  // Even after bluez returns, HandleSuspendReadiness shouldn't be called, but
  // bluez HandleSuspendDone should be called to undo the suspend preparation.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethod(_, _, _)).Times(0);
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendDone);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));

  // HandleSuspendImminent finishes.
  CallBluezCallback();
}

// SignalDone happens while HandleSuspendImminent is still in progress. But then
// the next SignalImminent also happens while HandleSuspendDone is still in
// progress.
TEST_F(SuspendManagerTest, PowerManagerSuspendDoneEarlySuspendImminentEarly) {
  // Power manager should receive RegisterSuspendDelay after it's available.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(
          this,
          &SuspendManagerTest::StubPowerManagerCallMethodWithErrorCallback));
  // Start with power manager available event.
  TriggerPowerManagerAvailable(true);

  // Tell our bluez stub to pretend to not return immediately so that we can
  // exercise bluez in-progress scenarios.
  simulates_bluez_long_return_ = true;

  // Bluez HandleSuspendImminent should be called after SuspendImminent signal.
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendImminent);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));
  // HandleSuspendReadiness shouldn't be called yet, since bluez is still in
  // progress doing HandleSuspendImminent.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethod(_, _, _)).Times(0);

  // Trigger suspend imminent signal.
  EmitSuspendImminentSignal(kSuspendId);

  // Bluez HandleSuspendDone shouldn't be called after SuspendDone signal,
  // since the current HandleSuspendImminent is still in progress.
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _)).Times(0);

  // Trigger suspend done signal while HandleSuspendImminent is still in
  // progress.
  EmitSuspendDoneSignal(kSuspendId);

  // Even after bluez returns, HandleSuspendReadiness shouldn't be called, but
  // bluez HandleSuspendDone should be called to undo the suspend preparation.
  EXPECT_CALL(*power_manager_proxy_, DoCallMethod(_, _, _)).Times(0);
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendDone);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));

  // HandleSuspendImminent finishes.
  CallBluezCallback();

  // Here the HandleSuspendDone is still in progress. When the next
  // SuspendImminent happens we shouldn't make any call to bluez.
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _)).Times(0);

  // Trigger suspend imminent signal with different suspend id.
  EmitSuspendImminentSignal(kSuspendId + 1);

  // Bluez HandleSuspendImminent should be called after HandleSuspendDone
  // finishes.
  expected_bluez_method_call_ =
      std::make_unique<std::string>(bluetooth_adapter::kHandleSuspendImminent);
  EXPECT_CALL(*bluez_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubBluezCallMethod));

  // HandleSuspendDone finishes.
  CallBluezCallback();

  // HandleSuspendReadiness should be called after HandleSuspendImminent
  // finishes.
  expected_suspend_readiness_ =
      std::make_unique<power_manager::SuspendReadinessInfo>();
  expected_suspend_readiness_->set_delay_id(kDelayId);
  expected_suspend_readiness_->set_suspend_id(kSuspendId + 1);
  EXPECT_CALL(*power_manager_proxy_, DoCallMethod(_, _, _))
      .WillOnce(Invoke(this, &SuspendManagerTest::StubPowerManagerCallMethod));

  // HandleSuspendImminent finishes.
  CallBluezCallback();
}

}  // namespace bluetooth
