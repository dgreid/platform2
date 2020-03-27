// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/message_loop/message_loop.h>
#include <base/optional.h>
#include <base/run_loop.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>

#include "debugd/dbus-proxy-mocks.h"
#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/cros_healthd/utils/backlight_utils.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "diagnostics/cros_healthd/utils/vpd_utils.h"
#include "mojo/cros_healthd.mojom.h"

using testing::_;
using testing::Invoke;
using testing::NotNull;
using testing::Return;
using testing::StrictMock;
using testing::WithArgs;

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

constexpr uint32_t kExpectedId = 123;
constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
    mojo_ipc::DiagnosticRoutineStatusEnum::kReady;

// Saves |response| to |response_destination|.
template <class T>
void SaveMojoResponse(T* response_destination, T response) {
  *response_destination = std::move(response);
}

class MockCrosHealthdRoutineService : public CrosHealthdRoutineService {
 public:
  MOCK_METHOD0(GetAvailableRoutines,
               std::vector<mojo_ipc::DiagnosticRoutineEnum>());
  MOCK_METHOD4(RunBatteryCapacityRoutine,
               void(uint32_t low_mah,
                    uint32_t high_mah,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD4(RunBatteryHealthRoutine,
               void(uint32_t maximum_cycle_count,
                    uint32_t percent_battery_wear_allowed,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD3(RunUrandomRoutine,
               void(uint32_t length_seconds,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD2(RunSmartctlCheckRoutine,
               void(int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD4(RunAcPowerRoutine,
               void(mojo_ipc::AcPowerStatusEnum expected_status,
                    const base::Optional<std::string>& expected_power_type,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD3(RunCpuCacheRoutine,
               void(base::TimeDelta exec_duration,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD3(RunCpuStressRoutine,
               void(base::TimeDelta exec_duration,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD3(RunFloatingPointAccuracyRoutine,
               void(base::TimeDelta exec_duration,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD3(RunNvmeWearLevelRoutine,
               void(uint32_t wear_level_threshold,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD3(RunNvmeSelfTestRoutine,
               void(mojo_ipc::NvmeSelfTestTypeEnum nvme_self_test_type,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD5(RunDiskReadRoutine,
               void(mojo_ipc::DiskReadRoutineTypeEnum type,
                    base::TimeDelta exec_duration,
                    uint32_t file_size_mb,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD4(RunPrimeSearchRoutine,
               void(base::TimeDelta exec_duration,
                    uint64_t max_num,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD4(RunBatteryDischargeRoutine,
               void(base::TimeDelta exec_duration,
                    uint32_t maximum_discharge_percent_allowed,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD4(GetRoutineUpdate,
               void(int32_t uuid,
                    mojo_ipc::DiagnosticRoutineCommandEnum command,
                    bool include_output,
                    mojo_ipc::RoutineUpdate* response));
};

}  // namespace

// Tests for the CrosHealthddMojoService class.
class CrosHealthdMojoServiceTest : public testing::Test {
 protected:
  CrosHealthdMojoServiceTest() {
    mojo::core::Init();
    mock_debugd_proxy_ = std::make_unique<org::chromium::debugdProxyMock>();
    options_.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = new dbus::MockBus(options_);
    mock_power_manager_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), power_manager::kPowerManagerServiceName,
        dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    fake_cros_config_ = std::make_unique<brillo::FakeCrosConfig>();
    backlight_fetcher_ =
        std::make_unique<BacklightFetcher>(fake_cros_config_.get());
    battery_fetcher_ = std::make_unique<BatteryFetcher>(
        mock_debugd_proxy_.get(), mock_power_manager_proxy_.get(),
        fake_cros_config_.get());
    cached_vpd_fetcher_ =
        std::make_unique<CachedVpdFetcher>(fake_cros_config_.get());
    service_ = std::make_unique<CrosHealthdMojoService>(
        backlight_fetcher_.get(), battery_fetcher_.get(),
        cached_vpd_fetcher_.get(), &routine_service_);
  }

  CrosHealthdMojoService* service() { return service_.get(); }

  MockCrosHealthdRoutineService* routine_service() { return &routine_service_; }

 private:
  base::MessageLoop message_loop_;
  StrictMock<MockCrosHealthdRoutineService> routine_service_;
  std::unique_ptr<org::chromium::debugdProxyMock> mock_debugd_proxy_;
  dbus::Bus::Options options_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_power_manager_proxy_;
  std::unique_ptr<brillo::FakeCrosConfig> fake_cros_config_;
  std::unique_ptr<BacklightFetcher> backlight_fetcher_;
  std::unique_ptr<BatteryFetcher> battery_fetcher_;
  std::unique_ptr<CachedVpdFetcher> cached_vpd_fetcher_;
  std::unique_ptr<CrosHealthdMojoService> service_;
};

// Test that we can request the battery capacity routine.
TEST_F(CrosHealthdMojoServiceTest, RequestBatteryCapacityRoutine) {
  constexpr uint32_t low_mah = 10;
  constexpr uint32_t high_mah = 100;

  EXPECT_CALL(*routine_service(), RunBatteryCapacityRoutine(
                                      low_mah, high_mah, NotNull(), NotNull()))
      .WillOnce(WithArgs<2, 3>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunBatteryCapacityRoutine(
      low_mah, high_mah,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the battery health routine.
TEST_F(CrosHealthdMojoServiceTest, RequestBatteryHealthRoutine) {
  constexpr uint32_t maximum_cycle_count = 44;
  constexpr uint32_t percent_battery_wear_allowed = 13;

  EXPECT_CALL(
      *routine_service(),
      RunBatteryHealthRoutine(maximum_cycle_count, percent_battery_wear_allowed,
                              NotNull(), NotNull()))
      .WillOnce(WithArgs<2, 3>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunBatteryHealthRoutine(
      maximum_cycle_count, percent_battery_wear_allowed,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the urandom routine.
TEST_F(CrosHealthdMojoServiceTest, RequestUrandomRoutine) {
  constexpr uint32_t length_seconds = 22;

  EXPECT_CALL(*routine_service(),
              RunUrandomRoutine(length_seconds, NotNull(), NotNull()))
      .WillOnce(WithArgs<1, 2>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunUrandomRoutine(
      length_seconds,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the smartctl-check routine.
TEST_F(CrosHealthdMojoServiceTest, RequestSmartctlCheckRoutine) {
  EXPECT_CALL(*routine_service(), RunSmartctlCheckRoutine(NotNull(), NotNull()))
      .WillOnce(WithArgs<0, 1>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunSmartctlCheckRoutine(base::Bind(
      &SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>, &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the AC power routine.
TEST_F(CrosHealthdMojoServiceTest, RequestAcPowerRoutine) {
  constexpr mojo_ipc::AcPowerStatusEnum kConnected =
      mojo_ipc::AcPowerStatusEnum::kConnected;
  const base::Optional<std::string> kPowerType{"USB_PD"};
  EXPECT_CALL(*routine_service(),
              RunAcPowerRoutine(kConnected, kPowerType, NotNull(), NotNull()))
      .WillOnce(WithArgs<2, 3>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunAcPowerRoutine(
      kConnected, kPowerType,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the CPU cache routine.
TEST_F(CrosHealthdMojoServiceTest, RequestCpuCacheRoutine) {
  constexpr auto exec_duration = base::TimeDelta().FromSeconds(30);

  EXPECT_CALL(*routine_service(),
              RunCpuCacheRoutine(exec_duration, NotNull(), NotNull()))
      .WillOnce(WithArgs<1, 2>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunCpuCacheRoutine(
      exec_duration.InSeconds(),
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the CPU stress routine.
TEST_F(CrosHealthdMojoServiceTest, RequestCpuStressRoutine) {
  constexpr auto exec_duration = base::TimeDelta().FromMinutes(5);

  EXPECT_CALL(*routine_service(),
              RunCpuStressRoutine(exec_duration, NotNull(), NotNull()))
      .WillOnce(WithArgs<1, 2>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunCpuStressRoutine(
      exec_duration.InSeconds(),
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the floating-point-accuracy routine.
TEST_F(CrosHealthdMojoServiceTest, RequestFloatingPointAccuracyRoutine) {
  constexpr base::TimeDelta exec_duration = base::TimeDelta::FromSeconds(22);

  EXPECT_CALL(*routine_service(), RunFloatingPointAccuracyRoutine(
                                      exec_duration, NotNull(), NotNull()))
      .WillOnce(WithArgs<1, 2>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunFloatingPointAccuracyRoutine(
      exec_duration.InSeconds(),
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the NvmeWearLevel routine.
TEST_F(CrosHealthdMojoServiceTest, RequestNvmeWearLevelRoutine) {
  constexpr uint32_t kThreshold = 50;

  EXPECT_CALL(*routine_service(),
              RunNvmeWearLevelRoutine(kThreshold, NotNull(), NotNull()))
      .WillOnce(WithArgs<1, 2>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunNvmeWearLevelRoutine(
      kThreshold, base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                             &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the NvmeSelfTest routine.
TEST_F(CrosHealthdMojoServiceTest, RequestNvmeSelfTestRoutine) {
  constexpr mojo_ipc::NvmeSelfTestTypeEnum kNvmeSelfTestType =
      mojo_ipc::NvmeSelfTestTypeEnum::kShortSelfTest;
  EXPECT_CALL(*routine_service(),
              RunNvmeSelfTestRoutine(kNvmeSelfTestType, NotNull(), NotNull()))
      .WillOnce(WithArgs<1, 2>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunNvmeSelfTestRoutine(
      kNvmeSelfTestType,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the disk-read routine.
TEST_F(CrosHealthdMojoServiceTest, RequestDiskReadRoutine) {
  constexpr mojo_ipc::DiskReadRoutineTypeEnum kType =
      mojo_ipc::DiskReadRoutineTypeEnum::kLinearRead;
  constexpr auto kExecDuration = base::TimeDelta::FromSeconds(8);
  constexpr uint32_t kFileSizeMb = 2048;
  EXPECT_CALL(*routine_service(),
              RunDiskReadRoutine(kType, kExecDuration, kFileSizeMb, NotNull(),
                                 NotNull()))
      .WillOnce(WithArgs<3, 4>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunDiskReadRoutine(
      kType, kExecDuration.InSeconds(), kFileSizeMb,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the prime-search routine.
TEST_F(CrosHealthdMojoServiceTest, RequestPrimeSearchRoutine) {
  constexpr auto kExecDuration = base::TimeDelta::FromSeconds(8);
  constexpr uint32_t kMaxNum = 10020;
  EXPECT_CALL(*routine_service(), RunPrimeSearchRoutine(kExecDuration, kMaxNum,
                                                        NotNull(), NotNull()))
      .WillOnce(WithArgs<2, 3>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunPrimeSearchRoutine(
      kExecDuration.InSeconds(), kMaxNum,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the battery discharge routine.
TEST_F(CrosHealthdMojoServiceTest, RequestBatteryDischargeRoutine) {
  constexpr uint32_t kLengthSeconds = 90;
  constexpr uint32_t kMaximumDischargePercentAllowed = 34;
  EXPECT_CALL(*routine_service(),
              RunBatteryDischargeRoutine(
                  base::TimeDelta::FromSeconds(kLengthSeconds),
                  kMaximumDischargePercentAllowed, NotNull(), NotNull()))
      .WillOnce(WithArgs<2, 3>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunBatteryDischargeRoutine(
      kLengthSeconds, kMaximumDischargePercentAllowed,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test an update request.
TEST_F(CrosHealthdMojoServiceTest, RequestRoutineUpdate) {
  constexpr int kId = 3;
  constexpr mojo_ipc::DiagnosticRoutineCommandEnum kCommand =
      mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus;
  constexpr bool kIncludeOutput = true;
  constexpr int kFakeProgressPercent = 13;

  EXPECT_CALL(*routine_service(),
              GetRoutineUpdate(kId, kCommand, kIncludeOutput, _))
      .WillOnce(WithArgs<3>(Invoke([](mojo_ipc::RoutineUpdate* update) {
        update->progress_percent = kFakeProgressPercent;
      })));

  mojo_ipc::RoutineUpdatePtr response;
  service()->GetRoutineUpdate(
      kId, kCommand, kIncludeOutput,
      base::Bind(&SaveMojoResponse<mojo_ipc::RoutineUpdatePtr>, &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->progress_percent, kFakeProgressPercent);
}

// Test that we report available routines correctly.
TEST_F(CrosHealthdMojoServiceTest, RequestAvailableRoutines) {
  const std::vector<mojo_ipc::DiagnosticRoutineEnum> available_routines = {
      mojo_ipc::DiagnosticRoutineEnum::kUrandom,
      mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck,
      mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy,
      mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel,
      mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest,
      mojo_ipc::DiagnosticRoutineEnum::kDiskRead,
      mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch,
  };

  EXPECT_CALL(*routine_service(), GetAvailableRoutines())
      .WillOnce(Return(available_routines));

  std::vector<mojo_ipc::DiagnosticRoutineEnum> response;
  service()->GetAvailableRoutines(base::Bind(
      [](std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>* out,
         const std::vector<
             chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>& routines) {
        *out = routines;
      },
      &response));

  EXPECT_EQ(response, available_routines);
}

}  // namespace diagnostics
