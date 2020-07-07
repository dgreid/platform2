// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "diagnostics/wilco_dtc_supportd/probe_service_impl.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

class MockCallback {
 public:
  MOCK_METHOD(void, OnTelemetryInfo, (mojo_ipc::TelemetryInfoPtr));
};

class MockProbeService : public mojo_ipc::CrosHealthdProbeService {
 public:
  MOCK_METHOD(void,
              ProbeProcessInfo,
              (uint32_t, ProbeProcessInfoCallback),
              (override));
  MOCK_METHOD(void,
              ProbeTelemetryInfo,
              (const std::vector<mojo_ipc::ProbeCategoryEnum>&,
               ProbeTelemetryInfoCallback),
              (override));
};

class MockProbeServiceDelegate : public ProbeService::Delegate {
 public:
  MOCK_METHOD(bool,
              BindCrosHealthdProbeService,
              (mojo_ipc::CrosHealthdProbeServiceRequest),
              (override));
};

}  // namespace

// Tests for the ProbeServiceImpl class.
class ProbeServiceImplTest : public testing::Test {
 public:
  ProbeServiceImplTest() = default;

  MockCallback* mock_callback() { return &mock_callback_; }

  MockProbeService* mock_probe_service() { return &mock_probe_service_; }

  mojo::Binding<mojo_ipc::CrosHealthdProbeService>* service_binding() {
    return &service_binding_;
  }

  MockProbeServiceDelegate* mock_delegate() { return &mock_delegate_; }

  ProbeService* service() { return &service_; }

 private:
  base::MessageLoop message_loop_;

  StrictMock<MockCallback> mock_callback_;

  StrictMock<MockProbeService> mock_probe_service_;
  mojo::Binding<mojo_ipc::CrosHealthdProbeService> service_binding_{
      &mock_probe_service_ /* impl */};

  StrictMock<MockProbeServiceDelegate> mock_delegate_;

  ProbeServiceImpl service_{&mock_delegate_};
};

TEST_F(ProbeServiceImplTest, ProbeServiceNotAvailable) {
  EXPECT_CALL(*mock_callback(), OnTelemetryInfo(_))
      .WillOnce(
          Invoke([](mojo_ipc::TelemetryInfoPtr ptr) { EXPECT_FALSE(ptr); }));
  EXPECT_CALL(*mock_delegate(), BindCrosHealthdProbeService(_));

  service()->ProbeTelemetryInfo(
      {}, base::BindOnce(&MockCallback::OnTelemetryInfo,
                         base::Unretained(mock_callback())));
}

TEST_F(ProbeServiceImplTest, ProbeServiceNotResponsive) {
  EXPECT_CALL(*mock_callback(), OnTelemetryInfo(_))
      .WillOnce(
          Invoke([](mojo_ipc::TelemetryInfoPtr ptr) { EXPECT_FALSE(ptr); }));
  EXPECT_CALL(*mock_delegate(), BindCrosHealthdProbeService(_))
      .WillOnce(Return(true));

  service()->ProbeTelemetryInfo(
      {}, base::BindOnce(&MockCallback::OnTelemetryInfo,
                         base::Unretained(mock_callback())));
}

TEST_F(ProbeServiceImplTest, DroppedConnection) {
  EXPECT_CALL(*mock_delegate(), BindCrosHealthdProbeService(_))
      .WillOnce(Return(true));

  base::RunLoop run_loop;
  service()->ProbeTelemetryInfo(
      {}, base::BindOnce(
              [](base::Closure callback, mojo_ipc::TelemetryInfoPtr ptr) {
                EXPECT_FALSE(ptr);
                callback.Run();
              },
              run_loop.QuitClosure()));
  run_loop.Run();
}

TEST_F(ProbeServiceImplTest, RecoverAfterDroppedConnection) {
  EXPECT_CALL(*mock_delegate(), BindCrosHealthdProbeService(_))
      .WillOnce(Return(true));

  base::RunLoop run_loop1;
  service()->ProbeTelemetryInfo(
      {}, base::BindOnce(
              [](base::Closure callback, mojo_ipc::TelemetryInfoPtr ptr) {
                EXPECT_FALSE(ptr);
                callback.Run();
              },
              run_loop1.QuitClosure()));
  run_loop1.Run();

  EXPECT_CALL(*mock_probe_service(), ProbeTelemetryInfo(_, _))
      .WillOnce(Invoke(
          [](const std::vector<mojo_ipc::ProbeCategoryEnum>& categories,
             mojo_ipc::CrosHealthdProbeService::ProbeTelemetryInfoCallback
                 callback) {
            mojo_ipc::TelemetryInfo telemetry_info;
            std::move(callback).Run(telemetry_info.Clone());
          }));

  EXPECT_CALL(*mock_delegate(), BindCrosHealthdProbeService(_))
      .WillOnce(
          Invoke([service_binding = service_binding()](
                     mojo_ipc::CrosHealthdProbeServiceRequest request) -> bool {
            service_binding->Bind(std::move(request));
            return true;
          }));

  base::RunLoop run_loop2;
  service()->ProbeTelemetryInfo(
      {}, base::BindOnce(
              [](base::Closure callback, mojo_ipc::TelemetryInfoPtr ptr) {
                EXPECT_TRUE(ptr);
                callback.Run();
              },
              run_loop2.QuitClosure()));
  run_loop2.Run();
}

TEST_F(ProbeServiceImplTest, ProbeTelemetryInfo) {
  const std::vector<mojo_ipc::ProbeCategoryEnum> kCategories{
      mojo_ipc::ProbeCategoryEnum::kBattery};

  EXPECT_CALL(*mock_probe_service(), ProbeTelemetryInfo(kCategories, _))
      .WillOnce(Invoke(
          [](const std::vector<mojo_ipc::ProbeCategoryEnum>& categories,
             mojo_ipc::CrosHealthdProbeService::ProbeTelemetryInfoCallback
                 callback) {
            mojo_ipc::TelemetryInfo telemetry_info;
            telemetry_info.battery_result =
                mojo_ipc::BatteryResult::NewBatteryInfo(
                    mojo_ipc::BatteryInfoPtr());

            std::move(callback).Run(telemetry_info.Clone());
          }));

  EXPECT_CALL(*mock_delegate(), BindCrosHealthdProbeService(_))
      .WillOnce(
          Invoke([service_binding = service_binding()](
                     mojo_ipc::CrosHealthdProbeServiceRequest request) -> bool {
            service_binding->Bind(std::move(request));
            return true;
          }));

  base::RunLoop run_loop;
  service()->ProbeTelemetryInfo(
      kCategories,
      base::BindOnce(
          [](base::Closure callback, mojo_ipc::TelemetryInfoPtr ptr) {
            ASSERT_TRUE(ptr);
            EXPECT_TRUE(ptr->battery_result->is_battery_info());
            callback.Run();
          },
          run_loop.QuitClosure()));
  run_loop.Run();
}

}  // namespace diagnostics
