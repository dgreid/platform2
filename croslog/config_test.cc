// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/config.h"

#include <base/files/file_path.h>
#include <gtest/gtest.h>

namespace croslog {

class ConfigTest : public ::testing::Test {
 public:
  ConfigTest() = default;
  ConfigTest(const ConfigTest&) = delete;
  ConfigTest& operator=(const ConfigTest&) = delete;

  ~ConfigTest() override = default;
};

TEST_F(ConfigTest, ParseCommandLineSourceMode) {
  base::FilePath kCrosLogProgramPath("croslog");

  {
    Config config;
    base::CommandLine command_line_without_source(kCrosLogProgramPath);
    EXPECT_TRUE(config.ParseCommandLineArgs(&command_line_without_source));
    EXPECT_EQ(SourceMode::PLAINTEXT_LOG, config.source);
  }

  {
    Config config;
    base::CommandLine command_line_with_journal_log(kCrosLogProgramPath);
    command_line_with_journal_log.AppendSwitchASCII("source", "journal");
    EXPECT_TRUE(config.ParseCommandLineArgs(&command_line_with_journal_log));
    EXPECT_EQ(SourceMode::JOURNAL_LOG, config.source);
  }

  {
    Config config;
    base::CommandLine command_line_with_plaintext_log(kCrosLogProgramPath);
    command_line_with_plaintext_log.AppendSwitchASCII("source", "plaintext");
    EXPECT_TRUE(config.ParseCommandLineArgs(&command_line_with_plaintext_log));
    EXPECT_EQ(SourceMode::PLAINTEXT_LOG, config.source);
  }

  {
    Config config;
    base::CommandLine command_line_with_invalid_source(kCrosLogProgramPath);
    command_line_with_invalid_source.AppendSwitchASCII("source", "invalid");
    EXPECT_FALSE(
        config.ParseCommandLineArgs(&command_line_with_invalid_source));
    EXPECT_EQ(SourceMode::PLAINTEXT_LOG, config.source);
  }
}

}  // namespace croslog
