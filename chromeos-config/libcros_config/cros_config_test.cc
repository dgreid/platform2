// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for the CrosConfig library, which provides access to the
// Chrome OS master configuration.

#include <cstdlib>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <gtest/gtest.h>
#include "chromeos-config/libcros_config/cros_config.h"
#include "chromeos-config/libcros_config/cros_config_fallback.h"
#include "chromeos-config/libcros_config/identity.h"

namespace {
constexpr char kTestFile[] = "test.json";
constexpr char kTestFileArm[] = "test_arm.json";
constexpr char kTestFileInvalid[] = "invalid_file.json";
}  // namespace

class CrosConfigTest : public testing::Test {
 protected:
  void InitConfig(const std::string name = "Another",
                  int sku_id = -1,
                  std::string whitelabel_name = "") {
    base::FilePath filepath(kTestFile);
    ASSERT_TRUE(cros_config_.InitForTest(sku_id, filepath,
                                         brillo::SystemArchitecture::kX86, name,
                                         whitelabel_name));
  }

  void InitConfigArm(const std::string device_name = "google,some",
                     int sku_id = -1,
                     std::string whitelabel_name = "") {
    base::FilePath filepath(kTestFileArm);
    ASSERT_TRUE(cros_config_.InitForTest(sku_id, filepath,
                                         brillo::SystemArchitecture::kArm,
                                         device_name, whitelabel_name));
  }

  brillo::CrosConfig cros_config_;
};

TEST_F(CrosConfigTest, CheckMissingFile) {
  base::FilePath filepath(kTestFileInvalid);
  EXPECT_FALSE(cros_config_.InitForTest(
      -1, filepath, brillo::SystemArchitecture::kX86, "no-model", ""));
}

TEST_F(CrosConfigTest, CheckUnknownModel) {
  base::FilePath filepath(kTestFile);
  EXPECT_FALSE(cros_config_.InitForTest(
      -1, filepath, brillo::SystemArchitecture::kX86, "no-model", ""));
}

TEST_F(CrosConfigTest, Check111NoInit) {
  std::string val;
  EXPECT_FALSE(cros_config_.GetString("/", "wallpaper", &val));
}

TEST_F(CrosConfigTest, CheckWrongPath) {
  InitConfig();
  std::string val;
  EXPECT_FALSE(cros_config_.GetString("/wibble", "wallpaper", &val));
}

TEST_F(CrosConfigTest, CheckBadString) {
  InitConfig();
  std::string val;
  EXPECT_FALSE(cros_config_.GetString("/", "string-list", &val));
}

TEST_F(CrosConfigTest, CheckGoodStringRoot) {
  InitConfig();
  std::string val;
  ASSERT_TRUE(cros_config_.GetString("/", "wallpaper", &val));
  EXPECT_EQ("default", val);
}

TEST_F(CrosConfigTest, CheckGoodStringNonRoot) {
  InitConfig();
  std::string val;
  ASSERT_TRUE(cros_config_.GetString("/identity", "smbios-name-match", &val));
  EXPECT_EQ("Another", val);
}

TEST_F(CrosConfigTest, CheckSMBIOSMatchInsensitive) {
  InitConfig("AnOtHeR");
  std::string val;
  ASSERT_TRUE(cros_config_.GetString("/", "name", &val));
  EXPECT_EQ("another", val);
}

TEST_F(CrosConfigTest, CheckEmptyPathError) {
  InitConfig();
  std::string val;
  ASSERT_FALSE(cros_config_.GetString("", "wallpaper", &val));
  EXPECT_EQ("", val);
}

TEST_F(CrosConfigTest, CheckPathWithoutSlashError) {
  InitConfig();
  std::string val;
  ASSERT_FALSE(cros_config_.GetString("noslash", "wallpaper", &val));
  EXPECT_EQ("", val);
}

TEST_F(CrosConfigTest, CheckUiPowerPosition) {
  InitConfig("Some", 1);
  std::string val;
  ASSERT_TRUE(cros_config_.GetString("/ui/power-button", "edge", &val));
  EXPECT_EQ("left", val);
  ASSERT_TRUE(cros_config_.GetString("/ui/power-button", "position", &val));
  EXPECT_EQ("0.3", val);
}

TEST_F(CrosConfigTest, CheckCameraCount) {
  InitConfig("Some", 0);
  std::string val;
  ASSERT_TRUE(cros_config_.GetString("/camera", "count", &val));
  EXPECT_EQ("1", val);
}

TEST_F(CrosConfigTest, CheckDeviceIndex0) {
  InitConfig("Some", 0);

  int device_index;
  ASSERT_TRUE(cros_config_.GetDeviceIndex(&device_index));
  EXPECT_EQ(0, device_index);
}

TEST_F(CrosConfigTest, CheckDeviceIndex1) {
  InitConfig("Some", 1);

  int device_index;
  ASSERT_TRUE(cros_config_.GetDeviceIndex(&device_index));
  EXPECT_EQ(1, device_index);
}

TEST_F(CrosConfigTest, CheckDeviceIndex2) {
  InitConfig("Some", 2);

  int device_index;
  ASSERT_TRUE(cros_config_.GetDeviceIndex(&device_index));
  EXPECT_EQ(2, device_index);
}

TEST_F(CrosConfigTest, CheckDeviceIndex3) {
  InitConfig("Another", 0);

  int device_index;
  ASSERT_TRUE(cros_config_.GetDeviceIndex(&device_index));
  EXPECT_EQ(3, device_index);
}

TEST_F(CrosConfigTest, CheckWriteFallbackFS) {
  base::FilePath tempdir;
  ASSERT_TRUE(base::CreateNewTempDirectory("cros_config_test", &tempdir));

  brillo::CrosConfigFallback fallback;
  EXPECT_TRUE(fallback.WriteConfigFS(tempdir));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(
      tempdir.Append("firmware").Append("image-name"), &contents));
  EXPECT_EQ("test_mosys_model_string", contents);
}

int main(int argc, char** argv) {
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_FILE;
  settings.log_file = "log.test";
  settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  settings.delete_old = logging::DELETE_OLD_LOG_FILE;
  logging::InitLogging(settings);
  logging::SetMinLogLevel(-3);

  int status = system("exec ./chromeos-config-test-setup.sh");
  if (status != 0) {
    LOG(ERROR) << "Failed to run ./chromeos-config-test-setup.sh";
    return EXIT_FAILURE;
  }

  // $SRC/testbin contains a fake mosys which is used by the tests
  const char* src_path = getenv("SRC");
  if (!src_path || !src_path[0]) {
    LOG(ERROR) << "SRC must be set in the environment";
    return EXIT_FAILURE;
  }

  std::string new_path(src_path);
  new_path += "/testbin";

  if (setenv("PATH", new_path.c_str(), true) < 0) {
    LOG(ERROR) << "Failed to set PATH";
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
