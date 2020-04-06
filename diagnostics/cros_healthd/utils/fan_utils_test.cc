// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "debugd/dbus-proxy-mocks.h"
#include "diagnostics/cros_healthd/utils/fan_utils.h"

namespace diagnostics {

namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::WithArg;

// Test values for fan speed.
constexpr uint32_t kFirstFanSpeedRpm = 2255;
constexpr uint32_t kSecondFanSpeedRpm = 1263;

}  // namespace

class FanUtilsTest : public ::testing::Test {
 protected:
  FanUtilsTest() {
    fan_fetcher_ = std::make_unique<FanFetcher>(&mock_debugd_proxy_);
  }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(
        base::CreateDirectory(GetTempDirPath().Append(kRelativeCrosEcPath)));
  }

  const base::FilePath& GetTempDirPath() const {
    DCHECK(temp_dir_.IsValid());
    return temp_dir_.GetPath();
  }

  FanFetcher* fan_fetcher() { return fan_fetcher_.get(); }

  org::chromium::debugdProxyMock* mock_debugd_proxy() {
    return &mock_debugd_proxy_;
  }

 private:
  StrictMock<org::chromium::debugdProxyMock> mock_debugd_proxy_;
  std::unique_ptr<FanFetcher> fan_fetcher_;
  base::ScopedTempDir temp_dir_;
};

// Test that fan information can be fetched successfully.
TEST_F(FanUtilsTest, FetchFanInfo) {
  // Set the mock debugd response.
  EXPECT_CALL(*mock_debugd_proxy(),
              CollectFanSpeed(_, _, kDebugdDBusTimeout.InMilliseconds()))
      .WillOnce(DoAll(WithArg<0>(Invoke([](std::string* output) {
                        *output = base::StringPrintf(
                            "Fan 0 RPM: %u\nFan 1 RPM: %u\n", kFirstFanSpeedRpm,
                            kSecondFanSpeedRpm);
                      })),
                      Return(true)));

  auto fan_info = fan_fetcher()->FetchFanInfo(GetTempDirPath());
  ASSERT_EQ(fan_info.size(), 2);
  EXPECT_EQ(fan_info[0]->speed_rpm, kFirstFanSpeedRpm);
  EXPECT_EQ(fan_info[1]->speed_rpm, kSecondFanSpeedRpm);
}

// Test that no fan information is returned for a device that has no fan.
TEST_F(FanUtilsTest, NoFan) {
  // Set the mock debugd response.
  EXPECT_CALL(*mock_debugd_proxy(),
              CollectFanSpeed(_, _, kDebugdDBusTimeout.InMilliseconds()))
      .WillOnce(
          DoAll(WithArg<0>(Invoke([](std::string* output) { *output = ""; })),
                Return(true)));

  auto fan_info = fan_fetcher()->FetchFanInfo(GetTempDirPath());
  EXPECT_EQ(fan_info.size(), 0);
}

// Test that debugd failing to collect fan speed fails gracefully.
TEST_F(FanUtilsTest, CollectFanSpeedFailure) {
  // Set the mock debugd response.
  EXPECT_CALL(*mock_debugd_proxy(),
              CollectFanSpeed(_, _, kDebugdDBusTimeout.InMilliseconds()))
      .WillOnce(DoAll(WithArg<1>(Invoke([](brillo::ErrorPtr* error) {
                        *error = brillo::Error::Create(FROM_HERE, "", "", "");
                      })),
                      Return(false)));

  auto fan_info = fan_fetcher()->FetchFanInfo(GetTempDirPath());
  EXPECT_EQ(fan_info.size(), 0);
}

// Test that fan speed is set to 0 RPM when a fan stalls.
TEST_F(FanUtilsTest, FanStalled) {
  // Set the mock debugd response.
  EXPECT_CALL(*mock_debugd_proxy(),
              CollectFanSpeed(_, _, kDebugdDBusTimeout.InMilliseconds()))
      .WillOnce(DoAll(WithArg<0>(Invoke([](std::string* output) {
                        *output = base::StringPrintf(
                            "Fan 0 stalled!\nFan 1 RPM: %u\n",
                            kSecondFanSpeedRpm);
                      })),
                      Return(true)));

  auto fan_info = fan_fetcher()->FetchFanInfo(GetTempDirPath());
  ASSERT_EQ(fan_info.size(), 2);
  EXPECT_EQ(fan_info[0]->speed_rpm, 0);
  EXPECT_EQ(fan_info[1]->speed_rpm, kSecondFanSpeedRpm);
}

// Test that failing to match a line of output to the fan speed regex fails
// gracefully and does not prevent other valid lines from being matched.
TEST_F(FanUtilsTest, BadLine) {
  // Set the mock debugd response.
  EXPECT_CALL(*mock_debugd_proxy(),
              CollectFanSpeed(_, _, kDebugdDBusTimeout.InMilliseconds()))
      .WillOnce(DoAll(WithArg<0>(Invoke([](std::string* output) {
                        *output = base::StringPrintf(
                            "Fan 0 RPM: bad\nFan 1 RPM: %u\n",
                            kSecondFanSpeedRpm);
                      })),
                      Return(true)));

  auto fan_info = fan_fetcher()->FetchFanInfo(GetTempDirPath());
  ASSERT_EQ(fan_info.size(), 1);
  EXPECT_EQ(fan_info[0]->speed_rpm, kSecondFanSpeedRpm);
}

// Test that failing to convert the first fan speed string to an integer fails
// gracefully and does not prevent other valid fan speed strings from being
// converted.
TEST_F(FanUtilsTest, BadValue) {
  // Set the mock debugd response.
  EXPECT_CALL(*mock_debugd_proxy(),
              CollectFanSpeed(_, _, kDebugdDBusTimeout.InMilliseconds()))
      .WillOnce(DoAll(WithArg<0>(Invoke([](std::string* output) {
                        *output = base::StringPrintf(
                            "Fan 0 RPM: -115\nFan 1 RPM: %u\n",
                            kSecondFanSpeedRpm);
                      })),
                      Return(true)));

  auto fan_info = fan_fetcher()->FetchFanInfo(GetTempDirPath());
  ASSERT_EQ(fan_info.size(), 1);
  EXPECT_EQ(fan_info[0]->speed_rpm, kSecondFanSpeedRpm);
}

// Test that no fan info is fetched for a device that does not have a Google EC.
TEST_F(FanUtilsTest, NoGoogleEc) {
  base::FilePath root_dir = GetTempDirPath();
  ASSERT_TRUE(base::DeleteFile(root_dir.Append(kRelativeCrosEcPath),
                               true /* recursive */));
  auto fan_info = fan_fetcher()->FetchFanInfo(root_dir);
  EXPECT_EQ(fan_info.size(), 0);
}

}  // namespace diagnostics
