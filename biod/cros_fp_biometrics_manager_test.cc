// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/cros_fp_biometrics_manager.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>

#include "biod/biod_crypto.h"
#include "biod/biod_crypto_test_data.h"
#include "biod/cros_fp_device_interface.h"
#include "biod/mock_biod_metrics.h"

namespace biod {

namespace {
constexpr int kMaxTemplateCount = 5;
constexpr char kRecordID[] = "record0";
constexpr char kLabel[] = "label0";
}  // namespace

using crypto_test_data::kFakePositiveMatchSecret1;
using crypto_test_data::kFakePositiveMatchSecret2;
using crypto_test_data::kFakeValidationValue1;
using crypto_test_data::kFakeValidationValue2;
using crypto_test_data::kUserID;

class FakeCrosFpDevice : public CrosFpDeviceInterface {
 public:
  FakeCrosFpDevice() { positive_match_secret_ = kFakePositiveMatchSecret1; }
  // CrosFpDeviceInterface overrides:
  ~FakeCrosFpDevice() override = default;

  bool SetFpMode(const FpMode& mode) override { return false; }
  bool GetFpMode(FpMode* mode) override { return false; }
  bool GetFpStats(int* capture_ms, int* matcher_ms, int* overall_ms) override {
    return false;
  }
  bool GetDirtyMap(std::bitset<32>* bitmap) override { return false; }
  bool SupportsPositiveMatchSecret() override { return true; }
  bool GetPositiveMatchSecret(int index, brillo::SecureBlob* secret) override {
    if (positive_match_secret_.empty())
      return false;
    secret->resize(FP_POSITIVE_MATCH_SECRET_BYTES);
    // Zero-pad the secret if it's too short.
    std::fill(secret->begin(), secret->end(), 0);
    std::copy(positive_match_secret_.begin(), positive_match_secret_.end(),
              secret->begin());
    return true;
  }
  bool GetTemplate(int index, VendorTemplate* out) override { return true; }
  bool UploadTemplate(const VendorTemplate& tmpl) override { return false; }
  bool SetContext(std::string user_id) override { return false; }
  bool ResetContext() override { return false; }
  bool InitEntropy(bool reset) override { return false; }
  bool UpdateFpInfo() override { return true; }

  int MaxTemplateCount() override { return kMaxTemplateCount; }
  int TemplateVersion() override { return FP_TEMPLATE_FORMAT_VERSION; }
  int DeadPixelCount() override { return 0; }

  EcCmdVersionSupportStatus EcCmdVersionSupported(uint16_t cmd,
                                                  uint32_t ver) override {
    return EcCmdVersionSupportStatus::UNSUPPORTED;
  }

 private:
  friend class CrosFpBiometricsManagerPeer;
  std::vector<uint8_t> positive_match_secret_;
};

class FakeCrosFpDeviceFactoryImpl : public CrosFpDeviceFactory {
  std::unique_ptr<CrosFpDeviceInterface> Create(
      const MkbpCallback& callback,
      BiodMetricsInterface* biod_metrics) override {
    return std::make_unique<FakeCrosFpDevice>();
  }
};

// Using a peer class to control access to the class under test is better than
// making the text fixture a friend class.
class CrosFpBiometricsManagerPeer {
 public:
  CrosFpBiometricsManagerPeer() {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    const scoped_refptr<dbus::MockBus> mock_bus = new dbus::MockBus(options);

    // Set EXPECT_CALL, otherwise gmock forces an failure due to "uninteresting
    // call" because we use StrictMock.
    // https://github.com/google/googletest/blob/fb49e6c164490a227bbb7cf5223b846c836a0305/googlemock/docs/cook_book.md#the-nice-the-strict-and-the-naggy-nicestrictnaggy
    const scoped_refptr<dbus::MockObjectProxy> power_manager_proxy =
        new dbus::MockObjectProxy(
            mock_bus.get(), power_manager::kPowerManagerServiceName,
            dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    EXPECT_CALL(*mock_bus,
                GetObjectProxy(
                    power_manager::kPowerManagerServiceName,
                    dbus::ObjectPath(power_manager::kPowerManagerServicePath)))
        .WillOnce(testing::Return(power_manager_proxy.get()));

    cros_fp_biometrics_manager_ = CrosFpBiometricsManager::Create(
        mock_bus, std::make_unique<FakeCrosFpDeviceFactoryImpl>(),
        std::make_unique<metrics::MockBiodMetrics>());
    // Keep a pointer to the fake device to manipulate it later.
    fake_cros_dev_ = static_cast<FakeCrosFpDevice*>(
        cros_fp_biometrics_manager_->cros_dev_.get());
  }

  // Methods to access or modify the fake device.

  void SetDevicePositiveMatchSecret(const std::vector<uint8_t>& new_secret) {
    fake_cros_dev_->positive_match_secret_ = new_secret;
  }

  // Methods to access or modify CrosFpBiometricsManager private fields.

  bool SupportsPositiveMatchSecret() {
    return cros_fp_biometrics_manager_->use_positive_match_secret_;
  }

  void SetUsePositiveMatchSecret(bool use) {
    cros_fp_biometrics_manager_->use_positive_match_secret_ = use;
  }

  // Add a record to cros_fp_biometrics_manager_, return the index.
  int AddRecord(int record_format_version,
                const std::string& record_id,
                const std::string& user_id,
                const std::string& label,
                const std::vector<uint8_t>& validation_value) {
    CrosFpBiometricsManager::InternalRecord internal_record = {
        record_format_version, record_id, user_id, label, validation_value};
    cros_fp_biometrics_manager_->records_.emplace_back(
        std::move(internal_record));
    return cros_fp_biometrics_manager_->records_.size() - 1;
  }

  bool ValidationValueEquals(int index,
                             const std::vector<uint8_t>& reference_value) {
    return cros_fp_biometrics_manager_->records_[index].validation_val ==
           reference_value;
  }

  // Methods to execute CrosFpBiometricsManager private methods.

  bool ComputeValidationValue(const std::vector<uint8_t>& secret,
                              const std::string& user_id,
                              std::vector<uint8_t>* out) {
    const brillo::SecureBlob secret_blob(secret);
    return BiodCrypto::ComputeValidationValue(secret_blob, user_id, out);
  }

  bool ValidationValueIsCorrect(uint32_t match_idx) {
    return cros_fp_biometrics_manager_->ValidationValueIsCorrect(match_idx);
  }

  BiometricsManager::AttemptMatches CalculateMatches(int match_idx,
                                                     bool matched) {
    return cros_fp_biometrics_manager_->CalculateMatches(match_idx, matched);
  }

  bool MigrateToValidationValue(int match_idx) {
    CrosFpBiometricsManager::MigrationStatus status =
        cros_fp_biometrics_manager_->MigrateToValidationValue(match_idx);
    return status == CrosFpBiometricsManager::MigrationStatus::kSuccess;
  }

  static void InsertEmptyPositiveMatchSalt(VendorTemplate* tmpl) {
    CrosFpBiometricsManager::InsertEmptyPositiveMatchSalt(tmpl);
  }

  bool RecordNeedsValidationValue(int index) {
    CrosFpBiometricsManager::Record current_record(
        cros_fp_biometrics_manager_->weak_factory_.GetWeakPtr(), index);
    return current_record.NeedsNewValidationValue();
  }

 private:
  std::unique_ptr<CrosFpBiometricsManager> cros_fp_biometrics_manager_;
  FakeCrosFpDevice* fake_cros_dev_;
};

class CrosFpBiometricsManagerTest : public ::testing::Test {
 public:
  bool BytesAreZeros(const uint8_t* start, size_t size) {
    const std::vector<uint8_t> zeros(size);
    return std::memcmp(start, zeros.data(), size) == 0;
  }

 protected:
  CrosFpBiometricsManagerPeer cros_fp_biometrics_manager_peer_;
};

TEST_F(CrosFpBiometricsManagerTest, TestComputeValidationValue) {
  EXPECT_TRUE(cros_fp_biometrics_manager_peer_.SupportsPositiveMatchSecret());
  const std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
      kSecretValidationValuePairs = {
          std::make_pair(kFakePositiveMatchSecret1, kFakeValidationValue1),
          std::make_pair(kFakePositiveMatchSecret2, kFakeValidationValue2),
      };
  for (const auto& pair : kSecretValidationValuePairs) {
    std::vector<uint8_t> validation_value;
    EXPECT_TRUE(cros_fp_biometrics_manager_peer_.ComputeValidationValue(
        pair.first, kUserID, &validation_value));
    EXPECT_EQ(validation_value, pair.second);
  }
}

TEST_F(CrosFpBiometricsManagerTest, TestValidationValueIsCorrect) {
  ASSERT_TRUE(cros_fp_biometrics_manager_peer_.SupportsPositiveMatchSecret());
  cros_fp_biometrics_manager_peer_.SetDevicePositiveMatchSecret(
      kFakePositiveMatchSecret1);
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, kFakeValidationValue1);
  bool ret = cros_fp_biometrics_manager_peer_.ValidationValueIsCorrect(index);
  EXPECT_TRUE(ret);

  // Make the device return a wrong positive_match_secret.
  cros_fp_biometrics_manager_peer_.SetDevicePositiveMatchSecret(
      kFakePositiveMatchSecret2);
  ret = cros_fp_biometrics_manager_peer_.ValidationValueIsCorrect(index);
  EXPECT_FALSE(ret);
}

TEST_F(CrosFpBiometricsManagerTest, TestCalculateMatchesNotMatched) {
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, kFakeValidationValue1);
  BiometricsManager::AttemptMatches matches =
      cros_fp_biometrics_manager_peer_.CalculateMatches(index, false);
  // If matched is false then we should report no matches.
  EXPECT_TRUE(matches.empty());
}

TEST_F(CrosFpBiometricsManagerTest, TestCalculateMatchesInvalidIndex) {
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, kFakeValidationValue1);
  BiometricsManager::AttemptMatches matches =
      cros_fp_biometrics_manager_peer_.CalculateMatches(index + 1, true);
  // If index is invalid then we should report no matches.
  EXPECT_TRUE(matches.empty());
}

TEST_F(CrosFpBiometricsManagerTest,
       TestCalculateMatchesWithPositiveMatchSecret) {
  ASSERT_TRUE(cros_fp_biometrics_manager_peer_.SupportsPositiveMatchSecret());
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, kFakeValidationValue1);
  BiometricsManager::AttemptMatches matches =
      cros_fp_biometrics_manager_peer_.CalculateMatches(index, true);
  EXPECT_EQ(matches,
            BiometricsManager::AttemptMatches({{kUserID, {kRecordID}}}));
}

TEST_F(CrosFpBiometricsManagerTest,
       TestCalculateMatchesWithoutPositiveMatchSecret) {
  // If not supporting positive match secret, we should just report matches.
  cros_fp_biometrics_manager_peer_.SetUsePositiveMatchSecret(false);
  ASSERT_FALSE(cros_fp_biometrics_manager_peer_.SupportsPositiveMatchSecret());
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, kFakeValidationValue1);
  BiometricsManager::AttemptMatches matches =
      cros_fp_biometrics_manager_peer_.CalculateMatches(index, true);
  EXPECT_EQ(matches,
            BiometricsManager::AttemptMatches({{kUserID, {kRecordID}}}));
}

TEST_F(CrosFpBiometricsManagerTest, TestCalculateMatchesOnMigration) {
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersionNoValidationValue, kRecordID, kUserID, kLabel,
      std::vector<uint8_t>());

  // IF firmware supports positive match secret but the record does not have
  // validation value:
  EXPECT_TRUE(cros_fp_biometrics_manager_peer_.SupportsPositiveMatchSecret());
  EXPECT_TRUE(
      cros_fp_biometrics_manager_peer_.RecordNeedsValidationValue(index));

  // THEN we accept the match since we are going to do migration.
  auto matches = cros_fp_biometrics_manager_peer_.CalculateMatches(index, true);
  EXPECT_EQ(matches,
            BiometricsManager::AttemptMatches({{kUserID, {kRecordID}}}));
}

TEST_F(CrosFpBiometricsManagerTest, TestMigrateToValidationValue) {
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, std::vector<uint8_t>());
  bool ret = cros_fp_biometrics_manager_peer_.MigrateToValidationValue(index);
  EXPECT_TRUE(ret);
  // After migration, the record at |index| should have the validation value
  // corresponding to the device's positive match secret.
  EXPECT_TRUE(cros_fp_biometrics_manager_peer_.ValidationValueEquals(
      index, kFakeValidationValue1));
}

TEST_F(CrosFpBiometricsManagerTest, TestMigrateToValidationValueFailures) {
  int index = cros_fp_biometrics_manager_peer_.AddRecord(
      kRecordFormatVersion, kRecordID, kUserID, kLabel, std::vector<uint8_t>());

  // Setting the devices positive match secret to empty will make the fake
  // device return false when asked for positive match secret.
  cros_fp_biometrics_manager_peer_.SetDevicePositiveMatchSecret(
      std::vector<uint8_t>());
  EXPECT_FALSE(
      cros_fp_biometrics_manager_peer_.MigrateToValidationValue(index));
}

TEST_F(CrosFpBiometricsManagerTest, TestInsertEmptyPositiveMatchSalt) {
  // Prepare a template of old format, with zero-length template field.
  size_t metadata_size = sizeof(struct ec_fp_template_encryption_metadata);
  std::vector<uint8_t> tmpl(metadata_size, 0xff);

  CrosFpBiometricsManagerPeer::InsertEmptyPositiveMatchSalt(&tmpl);

  EXPECT_EQ(tmpl.size(), metadata_size + FP_POSITIVE_MATCH_SALT_BYTES);
  EXPECT_TRUE(
      BytesAreZeros(tmpl.data() + metadata_size, FP_POSITIVE_MATCH_SALT_BYTES));
}

}  // namespace biod
