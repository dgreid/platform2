// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/system/powerd_adapter.h"
#include "diagnostics/common/system/powerd_adapter_impl.h"

using ::testing::_;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace diagnostics {

namespace {

// Writes empty proto messate to |signal|. After this operation, empty proto
// message can be successfuly read from the |signal|.
void WriteEmptyProtoToSignal(dbus::Signal* signal) {
  ASSERT_TRUE(signal);
  const std::string kSerializedProto;
  dbus::MessageWriter writer(signal);
  writer.AppendArrayOfBytes(
      reinterpret_cast<const uint8_t*>(kSerializedProto.data()),
      kSerializedProto.size());
}

class MockPowerdAdapterObserver : public PowerdAdapter::Observer {
 public:
  enum SignalType {
    POWER_SUPPLY,
    SUSPEND_IMMINENT,
    DARK_SUSPEND_IMMINENT,
    SUSPEND_DONE,
  };

  void OnPowerSupplyPollSignal(
      const power_manager::PowerSupplyProperties& power_supply) {
    OnSignal(POWER_SUPPLY);
  }
  void OnSuspendImminentSignal(
      const power_manager::SuspendImminent& suspend_imminent) {
    OnSignal(SUSPEND_IMMINENT);
  }
  void OnDarkSuspendImminentSignal(
      const power_manager::SuspendImminent& suspend_imminent) {
    OnSignal(DARK_SUSPEND_IMMINENT);
  }
  void OnSuspendDoneSignal(const power_manager::SuspendDone& suspend_done) {
    OnSignal(SUSPEND_DONE);
  }

  MOCK_METHOD(void, OnSignal, (SignalType));
};

}  // namespace

class BasePowerdAdapterImplTest : public ::testing::Test {
 public:
  BasePowerdAdapterImplTest()
      : dbus_bus_(new StrictMock<dbus::MockBus>(dbus::Bus::Options())),
        dbus_object_proxy_(new StrictMock<dbus::MockObjectProxy>(
            dbus_bus_.get(),
            power_manager::kPowerManagerServiceName,
            dbus::ObjectPath(power_manager::kPowerManagerServicePath))) {}

  void SetUp() override {
    EXPECT_CALL(*dbus_bus_,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(Return(dbus_object_proxy_.get()));

    EXPECT_CALL(*dbus_object_proxy_,
                DoConnectToSignal(power_manager::kPowerManagerInterface,
                                  power_manager::kPowerSupplyPollSignal, _, _))
        .WillOnce(SaveArg<2>(
            &on_signal_callbacks_[power_manager::kPowerSupplyPollSignal]));
    EXPECT_CALL(*dbus_object_proxy_,
                DoConnectToSignal(power_manager::kPowerManagerInterface,
                                  power_manager::kSuspendImminentSignal, _, _))
        .WillOnce(SaveArg<2>(
            &on_signal_callbacks_[power_manager::kSuspendImminentSignal]));
    EXPECT_CALL(
        *dbus_object_proxy_,
        DoConnectToSignal(power_manager::kPowerManagerInterface,
                          power_manager::kDarkSuspendImminentSignal, _, _))
        .WillOnce(SaveArg<2>(
            &on_signal_callbacks_[power_manager::kDarkSuspendImminentSignal]));
    EXPECT_CALL(*dbus_object_proxy_,
                DoConnectToSignal(power_manager::kPowerManagerInterface,
                                  power_manager::kSuspendDoneSignal, _, _))
        .WillOnce(SaveArg<2>(
            &on_signal_callbacks_[power_manager::kSuspendDoneSignal]));

    powerd_adapter_ = std::make_unique<PowerdAdapterImpl>(dbus_bus_);
  }

  void InvokeSignal(const std::string& method_name, dbus::Signal* signal) {
    ASSERT_TRUE(signal);
    auto callback_iter = on_signal_callbacks_.find(method_name);
    ASSERT_NE(callback_iter, on_signal_callbacks_.end());
    callback_iter->second.Run(signal);
  }

  PowerdAdapterImpl* powerd_adapter() const {
    DCHECK(powerd_adapter_);
    return powerd_adapter_.get();
  }

 private:
  scoped_refptr<StrictMock<dbus::MockBus>> dbus_bus_;

  scoped_refptr<StrictMock<dbus::MockObjectProxy>> dbus_object_proxy_;

  std::unique_ptr<PowerdAdapterImpl> powerd_adapter_;

  std::unordered_map<std::string, base::Callback<void(dbus::Signal* signal)>>
      on_signal_callbacks_;

  DISALLOW_COPY_AND_ASSIGN(BasePowerdAdapterImplTest);
};

// This is a parameterized test with the following parameters:
// * |signal_name| - signal name which will be invoked;
// * |expected_received_signal_type| - expected received signal type.
class PowerdAdapterImplTest
    : public BasePowerdAdapterImplTest,
      public testing::WithParamInterface<
          std::tuple<std::string /* signal_name */,
                     MockPowerdAdapterObserver::
                         SignalType /* expected_received_signal_type */>> {
 public:
  PowerdAdapterImplTest() = default;

 protected:
  // Accessors to individual test parameters from the test parameter tuple
  // returned by gtest's GetParam():

  const std::string& signal_name() const { return std::get<0>(GetParam()); }

  MockPowerdAdapterObserver::SignalType expected_received_signal_type() const {
    return std::get<1>(GetParam());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(PowerdAdapterImplTest);
};

TEST_P(PowerdAdapterImplTest, OnSignal) {
  StrictMock<MockPowerdAdapterObserver> mock_observer;
  powerd_adapter()->AddObserver(&mock_observer);

  dbus::Signal signal(power_manager::kPowerManagerInterface, signal_name());

  // Invoke signal without a valid proto message.
  InvokeSignal(signal_name(), &signal);

  // Invoke signal with a valid proto message.
  WriteEmptyProtoToSignal(&signal);
  EXPECT_CALL(mock_observer, OnSignal(expected_received_signal_type()));
  InvokeSignal(signal_name(), &signal);

  // Expect that |mock_observer| will not receive further notifications once
  // |mock_observer| was removed from powerd adapter.
  powerd_adapter()->RemoveObserver(&mock_observer);
  WriteEmptyProtoToSignal(&signal);
  InvokeSignal(signal_name(), &signal);
}

INSTANTIATE_TEST_CASE_P(
    ,
    PowerdAdapterImplTest,
    testing::Values(
        std::make_tuple(power_manager::kPowerSupplyPollSignal,
                        MockPowerdAdapterObserver::POWER_SUPPLY),
        std::make_tuple(power_manager::kSuspendImminentSignal,
                        MockPowerdAdapterObserver::SUSPEND_IMMINENT),
        std::make_tuple(power_manager::kDarkSuspendImminentSignal,
                        MockPowerdAdapterObserver::DARK_SUSPEND_IMMINENT),
        std::make_tuple(power_manager::kSuspendDoneSignal,
                        MockPowerdAdapterObserver::SUSPEND_DONE)));

}  // namespace diagnostics
