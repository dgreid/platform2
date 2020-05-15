// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/utils/backlight_utils.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

using ::chromeos::cros_healthd::mojom::BacklightInfo;
using ::chromeos::cros_healthd::mojom::BacklightInfoPtr;
using ::chromeos::cros_healthd::mojom::BacklightResultPtr;
using ::chromeos::cros_healthd::mojom::ErrorType;
using ::testing::UnorderedElementsAreArray;

constexpr char kBacklightPropertiesPath[] = "/cros-healthd/backlight";
constexpr char kHasBacklightProperty[] = "has-backlight";
constexpr char kRelativeBacklightDirectoryPath[] = "sys/class/backlight";
constexpr char kBrightnessFileName[] = "brightness";
constexpr char kMaxBrightnessFileName[] = "max_brightness";

constexpr uint32_t kFirstFakeBacklightBrightness = 98;
constexpr uint32_t kFirstFakeBacklightMaxBrightness = 99;
constexpr uint32_t kSecondFakeBacklightBrightness = 12;
constexpr uint32_t kSecondFakeBacklightMaxBrightness = 43;
constexpr char kFakeNonIntegerFileContents[] = "Not an integer!";

base::FilePath GetFirstFakeBacklightDirectory(const base::FilePath& root) {
  return root.Append(kRelativeBacklightDirectoryPath).Append("first_dir");
}

base::FilePath GetSecondFakeBacklightDirectory(const base::FilePath& root) {
  return root.Append(kRelativeBacklightDirectoryPath).Append("second_dir");
}

// Workaround for UnorderedElementsAreArray not accepting move-only types - this
// simple matcher expects a std::cref(mojo_ipc::BacklightInfoPtr) and checks
// each of the three fields for equality.
MATCHER_P(MatchesBacklightInfoPtr, ptr, "") {
  return arg->path == ptr.get()->path &&
         arg->max_brightness == ptr.get()->max_brightness &&
         arg->brightness == ptr.get()->brightness;
}

}  // namespace

class BacklightUtilsTest : public ::testing::Test {
 protected:
  BacklightUtilsTest() {
    fake_cros_config_ = std::make_unique<brillo::FakeCrosConfig>();
    backlight_fetcher_ =
        std::make_unique<BacklightFetcher>(fake_cros_config_.get());
  }
  BacklightUtilsTest(const BacklightUtilsTest&) = delete;
  BacklightUtilsTest& operator=(const BacklightUtilsTest&) = delete;

  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  const base::FilePath& GetTempDirPath() const {
    DCHECK(temp_dir_.IsValid());
    return temp_dir_.GetPath();
  }

  BacklightResultPtr FetchBacklightInfo(const base::FilePath& root_dir) {
    return backlight_fetcher_->FetchBacklightInfo(root_dir);
  }

  void SetHasBacklightString(const std::string& val) {
    fake_cros_config_->SetString(kBacklightPropertiesPath,
                                 kHasBacklightProperty, val);
  }

 private:
  std::unique_ptr<brillo::FakeCrosConfig> fake_cros_config_;
  std::unique_ptr<BacklightFetcher> backlight_fetcher_;
  base::ScopedTempDir temp_dir_;
};

// Test that backlight info can be read when it exists.
TEST_F(BacklightUtilsTest, TestFetchBacklightInfo) {
  auto root_dir = GetTempDirPath();
  base::FilePath first_backlight_dir = GetFirstFakeBacklightDirectory(root_dir);
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      first_backlight_dir.Append(kMaxBrightnessFileName),
      std::to_string(kFirstFakeBacklightMaxBrightness)));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      first_backlight_dir.Append(kBrightnessFileName),
      std::to_string(kFirstFakeBacklightBrightness)));
  base::FilePath second_backlight_dir =
      GetSecondFakeBacklightDirectory(root_dir);
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      second_backlight_dir.Append(kMaxBrightnessFileName),
      std::to_string(kSecondFakeBacklightMaxBrightness)));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      second_backlight_dir.Append(kBrightnessFileName),
      std::to_string(kSecondFakeBacklightBrightness)));

  std::vector<BacklightInfoPtr> expected_results;
  expected_results.push_back(BacklightInfo::New(
      first_backlight_dir.value(), kFirstFakeBacklightMaxBrightness,
      kFirstFakeBacklightBrightness));
  expected_results.push_back(BacklightInfo::New(
      second_backlight_dir.value(), kSecondFakeBacklightMaxBrightness,
      kSecondFakeBacklightBrightness));

  auto backlight_result = FetchBacklightInfo(root_dir);
  ASSERT_TRUE(backlight_result->is_backlight_info());
  const auto& backlight_info = backlight_result->get_backlight_info();

  // Since FetchBacklightInfo uses base::FileEnumerator, we're not guaranteed
  // the order of the two results.
  EXPECT_THAT(backlight_info,
              UnorderedElementsAreArray({
                  MatchesBacklightInfoPtr(std::cref(expected_results[0])),
                  MatchesBacklightInfoPtr(std::cref(expected_results[1])),
              }));
}

// Test that one bad backlight directory (missing required files) returns an
// error.
TEST_F(BacklightUtilsTest, TestFetchBacklightInfoOneBadOneGoodDirectory) {
  auto root_dir = GetTempDirPath();
  base::FilePath first_backlight_dir = GetFirstFakeBacklightDirectory(root_dir);
  // Skip the brightness file for the first directory.
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      first_backlight_dir.Append(kMaxBrightnessFileName),
      std::to_string(kFirstFakeBacklightMaxBrightness)));
  base::FilePath second_backlight_dir =
      GetSecondFakeBacklightDirectory(root_dir);
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      second_backlight_dir.Append(kMaxBrightnessFileName),
      std::to_string(kSecondFakeBacklightMaxBrightness)));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      second_backlight_dir.Append(kBrightnessFileName),
      std::to_string(kSecondFakeBacklightBrightness)));

  auto backlight_result = FetchBacklightInfo(root_dir);
  ASSERT_TRUE(backlight_result->is_error());
  EXPECT_EQ(backlight_result->get_error()->type, ErrorType::kFileReadError);
}

// Test that fetching backlight info returns an empty list when no backlight
// directories exist.
TEST_F(BacklightUtilsTest, TestFetchBacklightInfoNoDirectories) {
  auto backlight_result = FetchBacklightInfo(GetTempDirPath());
  ASSERT_TRUE(backlight_result->is_backlight_info());
  EXPECT_EQ(backlight_result->get_backlight_info().size(), 0);
}

// Test that fetching backlight info returns an error when the brightness file
// doesn't exist.
TEST_F(BacklightUtilsTest, TestFetchBacklightInfoNoBrightness) {
  auto root_dir = GetTempDirPath();
  base::FilePath first_backlight_dir = GetFirstFakeBacklightDirectory(root_dir);
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      first_backlight_dir.Append(kMaxBrightnessFileName),
      std::to_string(kFirstFakeBacklightMaxBrightness)));

  auto backlight_result = FetchBacklightInfo(root_dir);
  ASSERT_TRUE(backlight_result->is_error());
  EXPECT_EQ(backlight_result->get_error()->type, ErrorType::kFileReadError);
}

// Test that fetching backlight info returns an error when the max_brightness
// file doesn't exist.
TEST_F(BacklightUtilsTest, TestFetchBacklightInfoNoMaxBrightness) {
  auto root_dir = GetTempDirPath();
  base::FilePath first_backlight_dir = GetFirstFakeBacklightDirectory(root_dir);
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      first_backlight_dir.Append(kBrightnessFileName),
      std::to_string(kFirstFakeBacklightBrightness)));

  auto backlight_result = FetchBacklightInfo(root_dir);
  ASSERT_TRUE(backlight_result->is_error());
  EXPECT_EQ(backlight_result->get_error()->type, ErrorType::kFileReadError);
}

// Test that fetching backlight info returns an error when the brightess file is
// formatted incorrectly.
TEST_F(BacklightUtilsTest,
       TestFetchBacklightInfoBrightnessFormattedIncorrectly) {
  auto root_dir = GetTempDirPath();
  base::FilePath first_backlight_dir = GetFirstFakeBacklightDirectory(root_dir);
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      first_backlight_dir.Append(kMaxBrightnessFileName),
      std::to_string(kFirstFakeBacklightMaxBrightness)));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      first_backlight_dir.Append(kBrightnessFileName),
      kFakeNonIntegerFileContents));

  auto backlight_result = FetchBacklightInfo(root_dir);
  ASSERT_TRUE(backlight_result->is_error());
  EXPECT_EQ(backlight_result->get_error()->type, ErrorType::kFileReadError);
}

// Test that fetching backlight info returns an error when the max_brightess
// file is formatted incorrectly.
TEST_F(BacklightUtilsTest,
       TestFetchBacklightInfoMaxBrightnessFormattedIncorrectly) {
  auto root_dir = GetTempDirPath();
  base::FilePath first_backlight_dir = GetFirstFakeBacklightDirectory(root_dir);
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      first_backlight_dir.Append(kMaxBrightnessFileName),
      kFakeNonIntegerFileContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      first_backlight_dir.Append(kBrightnessFileName),
      std::to_string(kFirstFakeBacklightMaxBrightness)));

  auto backlight_result = FetchBacklightInfo(root_dir);
  ASSERT_TRUE(backlight_result->is_error());
  EXPECT_EQ(backlight_result->get_error()->type, ErrorType::kFileReadError);
}

// Test that we return an empty BacklightInfo list when cros_config says it
// doesn't exist.
TEST_F(BacklightUtilsTest, TestCrosConfigReportsNoBacklight) {
  SetHasBacklightString("false");

  auto backlight_result = FetchBacklightInfo(GetTempDirPath());
  ASSERT_TRUE(backlight_result->is_backlight_info());
  EXPECT_EQ(backlight_result->get_backlight_info().size(), 0);
}

}  // namespace diagnostics
