// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for cros_config command-line utility.

#include <libgen.h>

#include <cstdio>
#include <string>
#include <vector>

#include <base/process/launch.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

namespace {

// Directory where cros_config is installed.  Needed because the tests run out
// of a different directory.
char* installed_dir = nullptr;

};  // namespace

// Return a vector containing a command-line to run cros_config.  If non-empty,
// |params| will be appended as additional parameters.
std::vector<std::string> GetCrosConfigCommand(
    const std::vector<std::string>& params) {
  std::vector<std::string> cmd = {
      base::StringPrintf("%s/cros_config", installed_dir),
      "--test_file=test.json", "--test_name=Another"};
  cmd.insert(cmd.end(), params.begin(), params.end());
  return cmd;
}

TEST(CrosConfigTest, MissingParams) {
  std::string output;
  bool success = base::GetAppOutput(GetCrosConfigCommand({}), &output);
  EXPECT_FALSE(success);
}

TEST(CrosConfigTest, GetStringRoot) {
  std::string val;
  bool success =
      base::GetAppOutput(GetCrosConfigCommand({"/", "wallpaper"}), &val);
  EXPECT_TRUE(success);
  EXPECT_EQ("default", val);
}

TEST(CrosConfigTest, GetStringNonRoot) {
  std::string val;
  bool success = base::GetAppOutput(
      GetCrosConfigCommand({"/identity", "smbios-name-match"}), &val);
  EXPECT_TRUE(success);
  EXPECT_EQ("Another", val);
}

int main(int argc, char** argv) {
  int status = system("exec ./chromeos-config-test-setup.sh");
  if (status != 0)
    return EXIT_FAILURE;
  testing::InitGoogleTest(&argc, argv);

  installed_dir = dirname(argv[0]);
  if (!installed_dir)
    return EXIT_FAILURE;

  return RUN_ALL_TESTS();
}
