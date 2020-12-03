// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/config.h"

#include <vector>

#include <base/files/file_path.h>
#include <gtest/gtest.h>

#include <brillo/flag_helper.h>

namespace croslog {

namespace {
constexpr char kCrosLogPath[] = "croslog";
}

class ParseCommandLineTest : public ::testing::Test {
 private:
  void TearDown() override {
    brillo::FlagHelper::GetInstance()->ResetForTesting();
    base::CommandLine::Reset();
  }
};

TEST_F(ParseCommandLineTest, SourceModeNoArg) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath};
  EXPECT_TRUE(config.ParseCommandLineArgs(args.size(), args.data()));
  EXPECT_EQ(SourceMode::PLAINTEXT_LOG, config.source);
}

TEST_F(ParseCommandLineTest, SourceModeJournalArg) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath, "--source=journal"};
  EXPECT_TRUE(config.ParseCommandLineArgs(args.size(), args.data()));
  EXPECT_EQ(SourceMode::JOURNAL_LOG, config.source);
}

TEST_F(ParseCommandLineTest, SourceModePlainTextArg) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath, "--source=plaintext"};
  EXPECT_TRUE(config.ParseCommandLineArgs(args.size(), args.data()));
  EXPECT_EQ(SourceMode::PLAINTEXT_LOG, config.source);
}

TEST_F(ParseCommandLineTest, SourceModeWithoutEqual) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath, "--source", "journal"};
  // Fails to parse.
  EXPECT_FALSE(config.ParseCommandLineArgs(args.size(), args.data()));
  // Falls back to the default.
  EXPECT_EQ(SourceMode::PLAINTEXT_LOG, config.source);
}

TEST_F(ParseCommandLineTest, SourceModeInvalidValue) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath, "--source=invalid"};
  // Fails to parse.
  EXPECT_FALSE(config.ParseCommandLineArgs(args.size(), args.data()));
  // Falls back to the default.
  EXPECT_EQ(SourceMode::PLAINTEXT_LOG, config.source);
}

TEST_F(ParseCommandLineTest, PagerModeNoArg) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath};
  EXPECT_TRUE(config.ParseCommandLineArgs(args.size(), args.data()));
  EXPECT_TRUE(config.no_pager);
}

TEST_F(ParseCommandLineTest, PagerModeWithArg) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath, "--pager"};
  EXPECT_TRUE(config.ParseCommandLineArgs(args.size(), args.data()));
  EXPECT_FALSE(config.no_pager);
}

TEST_F(ParseCommandLineTest, PagerModeWithNoArg) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath, "--no-pager"};
  EXPECT_TRUE(config.ParseCommandLineArgs(args.size(), args.data()));
  EXPECT_TRUE(config.no_pager);
}

TEST_F(ParseCommandLineTest, BootModeNoArg) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath};
  EXPECT_TRUE(config.ParseCommandLineArgs(args.size(), args.data()));
  // |boot| doesn't have value.
  EXPECT_FALSE(config.boot.has_value());
}

TEST_F(ParseCommandLineTest, BootModeWithoutSpecifiedID) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath, "--boot"};
  EXPECT_TRUE(config.ParseCommandLineArgs(args.size(), args.data()));
  // |boot| has an empty value.
  EXPECT_TRUE(config.boot.has_value());
  EXPECT_TRUE(config.boot->empty());
}

TEST_F(ParseCommandLineTest, BootModeWithSpecifiedID) {
  Config config;
  std::vector<const char*> args = {kCrosLogPath, "--boot=BOOTID"};
  EXPECT_TRUE(config.ParseCommandLineArgs(args.size(), args.data()));
  // |boot| has a value of the specified BOOT ID.
  EXPECT_TRUE(config.boot.has_value());
  EXPECT_EQ("BOOTID", *(config.boot));
}

}  // namespace croslog
