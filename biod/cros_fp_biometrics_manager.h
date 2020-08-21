// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_BIOMETRICS_MANAGER_H_
#define BIOD_CROS_FP_BIOMETRICS_MANAGER_H_

#include "biod/biometrics_manager.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/values.h>
#include <dbus/bus.h>

#include "biod/biod_storage.h"
#include "biod/cros_fp_device.h"
#include "biod/cros_fp_device_factory.h"
#include "biod/power_button_filter_interface.h"

namespace biod {

class BiodMetrics;

class CrosFpBiometricsManager : public BiometricsManager {
 public:
  static std::unique_ptr<CrosFpBiometricsManager> Create(
      const scoped_refptr<dbus::Bus>& bus,
      std::unique_ptr<CrosFpDeviceFactory> cros_fp_device_factory);

  // BiometricsManager overrides:
  ~CrosFpBiometricsManager() override;

  BiometricType GetType() override;
  BiometricsManager::EnrollSession StartEnrollSession(
      std::string user_id, std::string label) override;
  BiometricsManager::AuthSession StartAuthSession() override;
  std::vector<std::unique_ptr<BiometricsManager::Record>> GetRecords() override;
  bool DestroyAllRecords() override;
  void RemoveRecordsFromMemory() override;
  bool ReadRecordsForSingleUser(const std::string& user_id) override;

  void SetEnrollScanDoneHandler(const BiometricsManager::EnrollScanDoneCallback&
                                    on_enroll_scan_done) override;
  void SetAuthScanDoneHandler(const BiometricsManager::AuthScanDoneCallback&
                                  on_auth_scan_done) override;
  void SetSessionFailedHandler(const BiometricsManager::SessionFailedCallback&
                                   on_session_failed) override;

  bool SendStatsOnLogin() override;

  void SetDiskAccesses(bool allow) override;

  bool ResetSensor() override;

  bool ResetEntropy(bool factory_init) override;

 protected:
  void EndEnrollSession() override;
  void EndAuthSession() override;

 private:
  // For testing.
  friend class CrosFpBiometricsManagerPeer;

  using SessionAction = base::Callback<void(const uint32_t event)>;

  struct InternalRecord {
    int record_format_version;
    std::string record_id;
    std::string user_id;
    std::string label;
    std::vector<uint8_t> validation_val;
  };

  class Record : public BiometricsManager::Record {
   public:
    Record(const base::WeakPtr<CrosFpBiometricsManager>& biometrics_manager,
           int index)
        : biometrics_manager_(biometrics_manager), index_(index) {}

    // BiometricsManager::Record overrides:
    const std::string& GetId() const override;
    const std::string& GetUserId() const override;
    const std::string& GetLabel() const override;
    const std::vector<uint8_t>& GetValidationVal() const override;
    bool SetLabel(std::string label) override;
    bool Remove() override;
    bool SupportsPositiveMatchSecret() const override;
    bool NeedsNewValidationValue() const override;

   private:
    base::WeakPtr<CrosFpBiometricsManager> biometrics_manager_;
    int index_;
  };

  enum class MigrationStatus {
    kNotDoingMigration,
    kSuccess,
    kError,
  };

  explicit CrosFpBiometricsManager(
      std::unique_ptr<PowerButtonFilterInterface> power_button_filter,
      std::unique_ptr<CrosFpDeviceFactory> cros_fp_device_factory);
  bool Init();

  void OnEnrollScanDone(ScanResult result,
                        const BiometricsManager::EnrollStatus& enroll_status);
  void OnAuthScanDone(ScanResult result,
                      const BiometricsManager::AttemptMatches& matches);
  void OnSessionFailed();

  void OnMkbpEvent(uint32_t event);

  // Request an action from the Fingerprint MCU and set the appropriate callback
  // when the event with the result will arrive.
  bool RequestEnrollImage(InternalRecord record);
  bool RequestEnrollFingerUp(InternalRecord record);
  bool RequestMatch(int attempt = 0);
  bool RequestMatchFingerUp();

  // Actions taken when the corresponding Fingerprint MKBP events happen.
  void DoEnrollImageEvent(InternalRecord record, uint32_t event);
  void DoEnrollFingerUpEvent(InternalRecord record, uint32_t event);
  void DoMatchEvent(int attempt, uint32_t event);
  void DoMatchFingerUpEvent(uint32_t event);
  bool ValidationValueIsCorrect(uint32_t match_idx);
  BiometricsManager::AttemptMatches CalculateMatches(int match_idx,
                                                     bool matched);
  MigrationStatus MigrateToValidationValue(int match_idx);

  void KillMcuSession();

  void OnTaskComplete();

  static void InsertEmptyPositiveMatchSalt(VendorTemplate* tmpl);

  bool LoadRecord(int record_format_version,
                  const std::string& user_id,
                  const std::string& label,
                  const std::string& record_id,
                  const std::vector<uint8_t>& validation_val,
                  const base::Value& data);
  bool WriteRecord(const BiometricsManager::Record& record,
                   uint8_t* tmpl_data,
                   size_t tmpl_size);
  // Clear FPMCU context and re-upload all records from storage.
  bool ReloadAllRecords(std::string user_id);

  // BiodMetrics must come before CrosFpDevice, since CrosFpDevice has a
  // raw pointer to BiodMetrics. We must ensure CrosFpDevice is destructed
  // first.
  std::unique_ptr<BiodMetrics> biod_metrics_;
  std::unique_ptr<CrosFpDeviceInterface> cros_dev_;

  SessionAction next_session_action_;

  // This list of records should be matching the templates loaded on the MCU.
  std::vector<InternalRecord> records_;

  // Set of templates that came with a wrong validation value in matching.
  std::unordered_set<uint32_t> suspicious_templates_;

  BiometricsManager::EnrollScanDoneCallback on_enroll_scan_done_;
  BiometricsManager::AuthScanDoneCallback on_auth_scan_done_;
  BiometricsManager::SessionFailedCallback on_session_failed_;

  base::WeakPtrFactory<CrosFpBiometricsManager> session_weak_factory_;
  base::WeakPtrFactory<CrosFpBiometricsManager> weak_factory_;

  std::unique_ptr<PowerButtonFilterInterface> power_button_filter_;
  std::unique_ptr<CrosFpDeviceFactory> cros_fp_device_factory_;
  BiodStorage biod_storage_;

  bool use_positive_match_secret_;

  DISALLOW_COPY_AND_ASSIGN(CrosFpBiometricsManager);
};

}  // namespace biod

#endif  // BIOD_CROS_FP_BIOMETRICS_MANAGER_H_
