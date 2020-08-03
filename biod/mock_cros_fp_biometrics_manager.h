// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_CROS_FP_BIOMETRICS_MANAGER_H_
#define BIOD_MOCK_CROS_FP_BIOMETRICS_MANAGER_H_

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gmock/gmock.h>

#include "biod/cros_fp_biometrics_manager.h"
#include "biod/mock_biod_metrics.h"
#include "biod/power_button_filter.h"

namespace biod {

class MockCrosFpBiometricsManager : public CrosFpBiometricsManager {
 public:
  /**
   * @param bus DBus Usually a mock bus
   * @param cros_fp_device Usually a mock device so the call to Init()
   * succeeds.
   * @param biod_metrics Usually a mock metrics object
   *
   * @return mock instance on success, nullptr on failure
   */
  static std::unique_ptr<MockCrosFpBiometricsManager> Create(
      const scoped_refptr<dbus::Bus>& bus,
      std::unique_ptr<CrosFpDeviceFactory> cros_fp_device_factory,
      std::unique_ptr<BiodMetricsInterface> biod_metrics) {
    // Using new to access non-public constructor.
    // See https://abseil.io/tips/134.
    auto mock = base::WrapUnique(new MockCrosFpBiometricsManager(
        PowerButtonFilter::Create(bus), std::move(cros_fp_device_factory),
        std::move(biod_metrics)));
    if (!mock->Init()) {
      return nullptr;
    }
    return mock;
  }

  ~MockCrosFpBiometricsManager() override = default;

  MOCK_METHOD(BiometricType, GetType, (), (override));
  MOCK_METHOD(BiometricsManager::EnrollSession,
              StartEnrollSession,
              (std::string user_id, std::string label),
              (override));
  MOCK_METHOD(BiometricsManager::AuthSession, StartAuthSession, (), (override));
  MOCK_METHOD(std::vector<std::unique_ptr<BiometricsManager::Record>>,
              GetRecords,
              (),
              (override));
  MOCK_METHOD(bool, DestroyAllRecords, (), (override));
  MOCK_METHOD(void, RemoveRecordsFromMemory, (), (override));
  MOCK_METHOD(bool,
              ReadRecordsForSingleUser,
              (const std::string& user_id),
              (override));
  MOCK_METHOD(
      void,
      SetEnrollScanDoneHandler,
      (const BiometricsManager::EnrollScanDoneCallback& on_enroll_scan_done),
      (override));
  MOCK_METHOD(
      void,
      SetAuthScanDoneHandler,
      (const BiometricsManager::AuthScanDoneCallback& on_auth_scan_done),
      (override));
  MOCK_METHOD(
      void,
      SetSessionFailedHandler,
      (const BiometricsManager::SessionFailedCallback& on_session_failed),
      (override));
  MOCK_METHOD(bool, SendStatsOnLogin, (), (override));
  MOCK_METHOD(void, SetDiskAccesses, (bool allow), (override));
  MOCK_METHOD(bool, ResetSensor, (), (override));
  MOCK_METHOD(bool, ResetEntropy, (bool factory_init), (override));
  MOCK_METHOD(void, EndEnrollSession, (), (override));
  MOCK_METHOD(void, EndAuthSession, (), (override));

 protected:
  using CrosFpBiometricsManager::CrosFpBiometricsManager;
};

}  // namespace biod

#endif  // BIOD_MOCK_CROS_FP_BIOMETRICS_MANAGER_H_
