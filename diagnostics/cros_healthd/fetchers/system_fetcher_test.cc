// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/optional.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/system_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {

namespace {

// Fake cached VPD values used for testing.
const char kFakeFirstPowerDate[] = "2020-40";
const char kFakeManufactureDate[] = "2019-01-01";
const char kFakeSkuNumber[] = "ABCD&^A";
// Fake CrosConfig value used for testing.
constexpr char kFakeMarketingName[] = "Latitude 1234 Chromebook Enterprise";
// Fake DMI values used for testing.
constexpr char kFakeBiosVersion[] = "Google_BoardName.12200.68.0";
constexpr char kFakeBoardName[] = "BoardName";
constexpr char kFakeBoardVersion[] = "rev1234";
constexpr char kFakeChassisType[] = "9";
constexpr uint64_t kFakeChassisTypeOutput = 9;
constexpr char kFakeProductName[] = "ProductName";

}  // namespace

class SystemUtilsTest : public ::testing::Test {
 protected:
  SystemUtilsTest() = default;
  SystemUtilsTest(const SystemUtilsTest&) = delete;
  SystemUtilsTest& operator=(const SystemUtilsTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    base::FilePath root_dir = GetTempDirPath();
    // Populate fake cached VPD values.
    relative_vpd_rw_dir_ = root_dir.Append(kRelativeVpdRwPath);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        relative_vpd_rw_dir_.Append(kFirstPowerDateFileName),
        kFakeFirstPowerDate));
    relative_vpd_ro_dir_ = root_dir.Append(kRelativeVpdRoPath);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        relative_vpd_ro_dir_.Append(kManufactureDateFileName),
        kFakeManufactureDate));
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        relative_vpd_ro_dir_.Append(kSkuNumberFileName), kFakeSkuNumber));
    // Populate fake DMI values.
    relative_dmi_info_path_ = root_dir.Append(kRelativeDmiInfoPath);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        relative_dmi_info_path_.Append(kBiosVersionFileName),
        kFakeBiosVersion));
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        relative_dmi_info_path_.Append(kBoardNameFileName), kFakeBoardName));
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        relative_dmi_info_path_.Append(kBoardVersionFileName),
        kFakeBoardVersion));
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        relative_dmi_info_path_.Append(kChassisTypeFileName),
        kFakeChassisType));
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        relative_dmi_info_path_.Append(kProductNameFileName),
        kFakeProductName));

    SetHasSkuNumber(true);
    SetMarketingName(kFakeMarketingName);
  }

  const base::FilePath& GetTempDirPath() const {
    DCHECK(temp_dir_.IsValid());
    return temp_dir_.GetPath();
  }

  chromeos::cros_healthd::mojom::SystemResultPtr FetchSystemInfo(
      const base::FilePath& root_dir) {
    return system_fetcher_.FetchSystemInfo(root_dir);
  }

  void SetHasSkuNumber(bool val) {
    mock_context_.fake_system_config()->SetHasSkuNumber(val);
  }

  void SetMarketingName(const std::string& val) {
    mock_context_.fake_system_config()->SetMarketingName(val);
  }

  void ValidateCachedVpdInfo(
      const chromeos::cros_healthd::mojom::SystemInfoPtr& system_info) {
    ASSERT_TRUE(system_info->first_power_date.has_value());
    EXPECT_EQ(system_info->first_power_date.value(), kFakeFirstPowerDate);
    ASSERT_TRUE(system_info->manufacture_date.has_value());
    EXPECT_EQ(system_info->manufacture_date.value(), kFakeManufactureDate);
    ASSERT_TRUE(system_info->product_sku_number.has_value());
    EXPECT_EQ(system_info->product_sku_number.value(), kFakeSkuNumber);
  }

  void ValidateCrosConfigInfo(
      const chromeos::cros_healthd::mojom::SystemInfoPtr& system_info) {
    EXPECT_EQ(system_info->marketing_name, kFakeMarketingName);
  }

  void ValidateDmiInfo(
      const chromeos::cros_healthd::mojom::SystemInfoPtr& system_info) {
    ASSERT_TRUE(system_info->bios_version.has_value());
    EXPECT_EQ(system_info->bios_version, kFakeBiosVersion);
    ASSERT_TRUE(system_info->board_name.has_value());
    EXPECT_EQ(system_info->board_name, kFakeBoardName);
    ASSERT_TRUE(system_info->board_version.has_value());
    EXPECT_EQ(system_info->board_version, kFakeBoardVersion);
    ASSERT_TRUE(system_info->chassis_type);
    EXPECT_EQ(system_info->chassis_type->value, kFakeChassisTypeOutput);
    ASSERT_TRUE(system_info->product_name.has_value());
    EXPECT_EQ(system_info->product_name, kFakeProductName);
  }

  const base::FilePath& relative_vpd_rw_dir() { return relative_vpd_rw_dir_; }

  const base::FilePath& relative_vpd_ro_dir() { return relative_vpd_ro_dir_; }

  const base::FilePath& relative_dmi_info_path() {
    return relative_dmi_info_path_;
  }

 private:
  MockContext mock_context_;
  SystemFetcher system_fetcher_{&mock_context_};
  base::ScopedTempDir temp_dir_;
  base::FilePath relative_vpd_rw_dir_;
  base::FilePath relative_vpd_ro_dir_;
  base::FilePath relative_dmi_info_path_;
};

// Test that we can read the system info, when it exists.
TEST_F(SystemUtilsTest, TestFetchSystemInfo) {
  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
}

// Test that no first_power_date is reported when |kFirstPowerDateFileName| is
// not found.
TEST_F(SystemUtilsTest, TestNoFirstPowerDate) {
  // Delete the file containing first power date.
  ASSERT_TRUE(base::DeleteFile(
      relative_vpd_rw_dir().Append(kFirstPowerDateFileName), false));

  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  // Confirm that cached VPD values except first power date are obtained.
  EXPECT_FALSE(system_info->first_power_date.has_value());
  ASSERT_TRUE(system_info->manufacture_date.has_value());
  EXPECT_EQ(system_info->manufacture_date.value(), kFakeManufactureDate);
  ASSERT_TRUE(system_info->product_sku_number.has_value());
  EXPECT_EQ(system_info->product_sku_number.value(), kFakeSkuNumber);

  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
}

// Test that no manufacture_date is reported when |kManufactureDateFileName| is
// not found.
TEST_F(SystemUtilsTest, TestNoManufactureDate) {
  // Delete the file containing manufacture date.
  ASSERT_TRUE(base::DeleteFile(
      relative_vpd_ro_dir().Append(kManufactureDateFileName), false));

  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  // Confirm that cached VPD values except manufacture date are obtained.
  ASSERT_TRUE(system_info->first_power_date.has_value());
  EXPECT_EQ(system_info->first_power_date.value(), kFakeFirstPowerDate);
  EXPECT_FALSE(system_info->manufacture_date.has_value());
  ASSERT_TRUE(system_info->product_sku_number.has_value());
  EXPECT_EQ(system_info->product_sku_number.value(), kFakeSkuNumber);

  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
}

// Test that reading system info that does not have |kSkuNumberFileName| (when
// it should) reports an error.
TEST_F(SystemUtilsTest, TestSkuNumberError) {
  // Delete the file containing sku number.
  ASSERT_TRUE(base::DeleteFile(relative_vpd_ro_dir().Append(kSkuNumberFileName),
                               false));

  // Confirm that an error is obtained.
  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_error());
  EXPECT_EQ(system_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kFileReadError);
}

// Test that no product_sku_number is returned when the device does not have
// |kSkuNumberFileName|.
TEST_F(SystemUtilsTest, TestNoSkuNumber) {
  // Delete the file containing sku number.
  ASSERT_TRUE(base::DeleteFile(relative_vpd_ro_dir().Append(kSkuNumberFileName),
                               false));
  // Ensure that there is no sku number.
  SetHasSkuNumber(false);

  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();
  // Confirm that correct cached VPD values except sku number are obtained.
  ASSERT_TRUE(system_info->first_power_date.has_value());
  EXPECT_EQ(system_info->first_power_date.value(), kFakeFirstPowerDate);
  ASSERT_TRUE(system_info->manufacture_date.has_value());
  EXPECT_EQ(system_info->manufacture_date.value(), kFakeManufactureDate);
  EXPECT_FALSE(system_info->product_sku_number.has_value());

  ValidateCrosConfigInfo(system_info);
  ValidateDmiInfo(system_info);
}

// Test that no DMI fields are populated when |kRelativeDmiInfoPath| doesn't
// exist.
TEST_F(SystemUtilsTest, TestNoSysDevicesVirtualDmiId) {
  // Delete the directory |kRelativeDmiInfoPath|.
  ASSERT_TRUE(base::DeleteFile(relative_dmi_info_path(), true));

  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);

  // Confirm that no DMI values are obtained.
  EXPECT_FALSE(system_info->bios_version.has_value());
  EXPECT_FALSE(system_info->board_name.has_value());
  EXPECT_FALSE(system_info->board_version.has_value());
  EXPECT_FALSE(system_info->chassis_type);
  EXPECT_FALSE(system_info->product_name.has_value());
}

// Test that there is no bios_version when |kBiosVersionFileName| is missing.
TEST_F(SystemUtilsTest, TestNoBiosVersion) {
  // Delete the file containing bios version.
  ASSERT_TRUE(base::DeleteFile(
      relative_dmi_info_path().Append(kBiosVersionFileName), false));

  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);

  // Confirm that the bios_version was not populated.
  EXPECT_FALSE(system_info->bios_version.has_value());
  ASSERT_TRUE(system_info->board_name.has_value());
  EXPECT_EQ(system_info->board_name.value(), kFakeBoardName);
  ASSERT_TRUE(system_info->board_version.has_value());
  EXPECT_EQ(system_info->board_version.value(), kFakeBoardVersion);
  ASSERT_TRUE(system_info->chassis_type);
  EXPECT_EQ(system_info->chassis_type->value, kFakeChassisTypeOutput);
  ASSERT_TRUE(system_info->product_name.has_value());
  EXPECT_EQ(system_info->product_name.value(), kFakeProductName);
}

// Test that there is no board_name when |kBoardNameFileName| is missing.
TEST_F(SystemUtilsTest, TestNoBoardName) {
  // Delete the file containing board name.
  ASSERT_TRUE(base::DeleteFile(
      relative_dmi_info_path().Append(kBoardNameFileName), false));

  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);

  // Confirm that the board_name was not populated.
  ASSERT_TRUE(system_info->bios_version.has_value());
  EXPECT_EQ(system_info->bios_version.value(), kFakeBiosVersion);
  EXPECT_FALSE(system_info->board_name.has_value());
  ASSERT_TRUE(system_info->board_version.has_value());
  EXPECT_EQ(system_info->board_version.value(), kFakeBoardVersion);
  ASSERT_TRUE(system_info->chassis_type);
  EXPECT_EQ(system_info->chassis_type->value, kFakeChassisTypeOutput);
  ASSERT_TRUE(system_info->product_name.has_value());
  EXPECT_EQ(system_info->product_name.value(), kFakeProductName);
}

// Test that there is no board_version when |kBoardVersionFileName| is missing.
TEST_F(SystemUtilsTest, TestNoBoardVersion) {
  // Delete the file containing board version.
  ASSERT_TRUE(base::DeleteFile(
      relative_dmi_info_path().Append(kBoardVersionFileName), false));

  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);

  // Confirm that the board_version was not populated.
  ASSERT_TRUE(system_info->bios_version.has_value());
  EXPECT_EQ(system_info->bios_version.value(), kFakeBiosVersion);
  ASSERT_TRUE(system_info->board_name.has_value());
  EXPECT_EQ(system_info->board_name.value(), kFakeBoardName);
  EXPECT_FALSE(system_info->board_version.has_value());
  ASSERT_TRUE(system_info->chassis_type);
  EXPECT_EQ(system_info->chassis_type->value, kFakeChassisTypeOutput);
  ASSERT_TRUE(system_info->product_name.has_value());
  EXPECT_EQ(system_info->product_name.value(), kFakeProductName);
}

// Test that there is no chassis_type when |kChassisTypeFileName| is missing.
TEST_F(SystemUtilsTest, TestNoChassisType) {
  // Delete the file containing chassis type.
  ASSERT_TRUE(base::DeleteFile(
      relative_dmi_info_path().Append(kChassisTypeFileName), false));

  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);

  // Confirm that the chassis_type was not populated.
  ASSERT_TRUE(system_info->bios_version.has_value());
  EXPECT_EQ(system_info->bios_version.value(), kFakeBiosVersion);
  ASSERT_TRUE(system_info->board_name.has_value());
  EXPECT_EQ(system_info->board_name.value(), kFakeBoardName);
  ASSERT_TRUE(system_info->board_version.has_value());
  EXPECT_EQ(system_info->board_version.value(), kFakeBoardVersion);
  EXPECT_FALSE(system_info->chassis_type);
  ASSERT_TRUE(system_info->product_name.has_value());
  EXPECT_EQ(system_info->product_name.value(), kFakeProductName);
}

// Test that reading a chassis_type that cannot be converted to an unsigned
// integer reports an error.
TEST_F(SystemUtilsTest, TestBadChassisType) {
  // Overwrite the contents of |kChassisTypeFileName| with a chassis_type value
  // that cannot be parsed into an unsigned integer.
  std::string bad_chassis_type = "bad chassis type";
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      relative_dmi_info_path().Append(kChassisTypeFileName), bad_chassis_type));

  // Confirm that an error is obtained.
  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_error());
  EXPECT_EQ(system_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that there is no product_name when |kProductNameFileName| is missing.
TEST_F(SystemUtilsTest, TestNoProductName) {
  // Delete the file containing product name.
  ASSERT_TRUE(base::DeleteFile(
      relative_dmi_info_path().Append(kProductNameFileName), false));

  auto system_result = FetchSystemInfo(GetTempDirPath());
  ASSERT_TRUE(system_result->is_system_info());
  const auto& system_info = system_result->get_system_info();

  ValidateCachedVpdInfo(system_info);
  ValidateCrosConfigInfo(system_info);

  // Confirm that the product_name was not populated.
  ASSERT_TRUE(system_info->bios_version.has_value());
  EXPECT_EQ(system_info->bios_version.value(), kFakeBiosVersion);
  ASSERT_TRUE(system_info->board_name.has_value());
  EXPECT_EQ(system_info->board_name.value(), kFakeBoardName);
  ASSERT_TRUE(system_info->board_version.has_value());
  EXPECT_EQ(system_info->board_version.value(), kFakeBoardVersion);
  ASSERT_TRUE(system_info->chassis_type);
  EXPECT_EQ(system_info->chassis_type->value, kFakeChassisTypeOutput);
  EXPECT_FALSE(system_info->product_name.has_value());
}

}  // namespace diagnostics
