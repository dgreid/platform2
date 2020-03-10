// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cli/command.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <gtest/gtest.h>

namespace shill_cli {

namespace {

base::Callback<bool()> WrapHasRun(bool* has_run) {
  return base::Bind(
      [](bool* b) {
        *b = true;
        return true;
      },
      base::Unretained(has_run));
}

base::Callback<bool()> WrapPlainReturn(bool return_value) {
  return base::Bind([](bool b) { return b; }, return_value);
}

}  // namespace

class CommandTest : public testing::Test {
 public:
  CommandTest()
      : has_top_run_(false),
        cmd_("testcli", "Test command", WrapHasRun(&has_top_run_)) {}

  bool Run(std::vector<std::string> args) {
    return cmd_.Run(args.cbegin(), args.cend(), cmd_.full_name());
  }

  template <typename... Args>
  void AddSubcommand(Command* parent_command, Args... args) {
    parent_command->AddSubcommand(args...);
  }

  std::vector<std::string> GetPrefixedSubcommands() const {
    return cmd_.GetPrefixedSubcommands();
  }

 protected:
  bool has_top_run_;
  Command cmd_;
};

TEST_F(CommandTest, RunTopWhenEmpty) {
  EXPECT_TRUE(Run({}));
  EXPECT_TRUE(has_top_run_);
}

TEST_F(CommandTest, FailOnUnknownCommand) {
  EXPECT_FALSE(Run({"unknown"}));
  EXPECT_FALSE(has_top_run_);
}

TEST_F(CommandTest, PrefixMatchCommand) {
  bool has_run = false;
  AddSubcommand(&cmd_, "device", "", WrapHasRun(&has_run));

  EXPECT_TRUE(Run({"device"}));
  EXPECT_TRUE(has_run);
  EXPECT_FALSE(has_top_run_);

  has_run = false;
  EXPECT_TRUE(Run({"dev"}));
  EXPECT_TRUE(has_run);
  EXPECT_FALSE(has_top_run_);

  has_run = false;
  EXPECT_TRUE(Run({"d"}));
  EXPECT_TRUE(has_run);
  EXPECT_FALSE(has_top_run_);
}

TEST_F(CommandTest, FailOnPrefixMatchAmbiguity) {
  bool has_run_device = false;
  bool has_run_detect = false;
  AddSubcommand(&cmd_, "device", "", WrapHasRun(&has_run_device));
  AddSubcommand(&cmd_, "detect", "", WrapHasRun(&has_run_detect));

  EXPECT_TRUE(Run({"device"}));
  EXPECT_TRUE(has_run_device);
  EXPECT_FALSE(has_run_detect);
  EXPECT_FALSE(has_top_run_);

  has_run_device = false;
  EXPECT_TRUE(Run({"dev"}));
  EXPECT_TRUE(has_run_device);
  EXPECT_FALSE(has_run_detect);
  EXPECT_FALSE(has_top_run_);

  has_run_device = false;
  EXPECT_FALSE(Run({"d"}));
  EXPECT_FALSE(has_run_device);
  EXPECT_FALSE(has_run_detect);
  EXPECT_FALSE(has_top_run_);
}

TEST_F(CommandTest, PreventAmbiguousCommandDefinitions) {
  AddSubcommand(&cmd_, "device", "", WrapPlainReturn(true));
  ASSERT_DEATH(AddSubcommand(&cmd_, "devic", "", WrapPlainReturn(true)), ".*");
  ASSERT_DEATH(AddSubcommand(&cmd_, "device", "", WrapPlainReturn(true)), ".*");
  ASSERT_DEATH(AddSubcommand(&cmd_, "devicee", "", WrapPlainReturn(true)),
               ".*");
}

TEST_F(CommandTest, PrefixedSubcommandsEmpty) {
  // Not quite empty because of the help subcommand.
  EXPECT_EQ(GetPrefixedSubcommands(), std::vector<std::string>{"h[elp]"});
}

TEST_F(CommandTest, PrefixedSubcommandsNotEmpty) {
  AddSubcommand(&cmd_, "test", "", WrapPlainReturn(true));
  AddSubcommand(&cmd_, "device", "", WrapPlainReturn(true));
  EXPECT_EQ(GetPrefixedSubcommands(),
            std::vector<std::string>({"d[evice]", "h[elp]", "t[est]"}));

  AddSubcommand(&cmd_, "detect", "", WrapPlainReturn(true));
  EXPECT_EQ(
      GetPrefixedSubcommands(),
      std::vector<std::string>({"det[ect]", "dev[ice]", "h[elp]", "t[est]"}));
}

}  // namespace shill_cli
