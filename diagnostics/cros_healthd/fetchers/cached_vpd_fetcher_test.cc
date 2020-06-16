// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/optional.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/cached_vpd_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::CachedVpdResultPtr;
using ::chromeos::cros_healthd::mojom::ErrorType;

const char kRelativeSKUNumberPath[] = "sys/firmware/vpd/ro/sku_number";
const char kFakeSKUNumber[] = "ABCD&^A";

}  // namespace

class VpdUtilsTest : public ::testing::Test {
 protected:
  VpdUtilsTest() = default;
  VpdUtilsTest(const VpdUtilsTest&) = delete;
  VpdUtilsTest& operator=(const VpdUtilsTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  const base::FilePath& GetTempDirPath() const {
    DCHECK(temp_dir_.IsValid());
    return temp_dir_.GetPath();
  }

  CachedVpdResultPtr FetchCachedVpdInfo(const base::FilePath& root_dir) {
    return cached_vpd_fetcher_.FetchCachedVpdInfo(root_dir);
  }

  void SetHasSkuNumber(const bool val) {
    mock_context_.fake_system_config()->SetHasSkuNumberProperty(val);
  }

 private:
  MockContext mock_context_;
  CachedVpdFetcher cached_vpd_fetcher_{&mock_context_};
  base::ScopedTempDir temp_dir_;
};

// Test that we can read the cached VPD info, when it exists.
TEST_F(VpdUtilsTest, TestFetchCachedVpdInfo) {
  base::FilePath root_dir = GetTempDirPath();
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeSKUNumberPath), kFakeSKUNumber));
  SetHasSkuNumber(true);
  auto vpd_result = FetchCachedVpdInfo(root_dir);
  ASSERT_TRUE(vpd_result->is_vpd_info());
  const auto& vpd_info = vpd_result->get_vpd_info();
  ASSERT_TRUE(vpd_info->sku_number.has_value());
  EXPECT_EQ(vpd_info->sku_number.value(), kFakeSKUNumber);
}

// Test that reading cached VPD info that does not exist returns an error.
TEST_F(VpdUtilsTest, TestFetchCachedVpdInfoNoFile) {
  SetHasSkuNumber(true);
  auto vpd_result = FetchCachedVpdInfo(GetTempDirPath());
  ASSERT_TRUE(vpd_result->is_error());
  EXPECT_EQ(vpd_result->get_error()->type, ErrorType::kFileReadError);
}

// Test that no sku_number is returned when the device does not have a SKU
// number.
TEST_F(VpdUtilsTest, TestFetchCachedVpdInfoNoSkuNumber) {
  SetHasSkuNumber(false);
  auto vpd_result = FetchCachedVpdInfo(base::FilePath());
  ASSERT_TRUE(vpd_result->is_vpd_info());
  const auto& vpd_info = vpd_result->get_vpd_info();
  EXPECT_FALSE(vpd_info->sku_number.has_value());
}

}  // namespace diagnostics
