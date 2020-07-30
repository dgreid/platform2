// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/macros.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/smartctl_check/smartctl_check_utils.h"

namespace diagnostics {
namespace {

constexpr char kSmartctlOutputFormat[] =
    "%sAvailable Spare: %s\nAvailable Spare Threshold: %s";

}  // namespace

TEST(SmartctlCheckUtilsTest, GoodParseSimple) {
  int available_spare_pct, available_spare_threshold_pct;
  EXPECT_TRUE(ScrapeAvailableSparePercents(
      base::StringPrintf(kSmartctlOutputFormat, "", "75%", "25%"),
      &available_spare_pct, &available_spare_threshold_pct));
  EXPECT_EQ(available_spare_pct, 75);
  EXPECT_EQ(available_spare_threshold_pct, 25);
}

TEST(SmartctlCheckUtilsTest, GoodParseLeadingLF) {
  int available_spare_pct, available_spare_threshold_pct;
  EXPECT_TRUE(ScrapeAvailableSparePercents(
      base::StringPrintf(kSmartctlOutputFormat, "\n", "75%", "25%"),
      &available_spare_pct, &available_spare_threshold_pct));
  EXPECT_EQ(available_spare_pct, 75);
  EXPECT_EQ(available_spare_threshold_pct, 25);
}

TEST(SmartctlCheckUtilsTest, GoodParseNullOutValues) {
  EXPECT_TRUE(ScrapeAvailableSparePercents(
      base::StringPrintf(kSmartctlOutputFormat, "\n", "75%", "25%"), nullptr,
      nullptr));
}

TEST(SmartctlCheckUtilsTest, GoodParseLeadingLine) {
  int available_spare_pct, available_spare_threshold_pct;
  EXPECT_TRUE(ScrapeAvailableSparePercents(
      base::StringPrintf(kSmartctlOutputFormat, "Other info: 100%\n", "75%",
                         "25%"),
      &available_spare_pct, &available_spare_threshold_pct));
  EXPECT_EQ(available_spare_pct, 75);
  EXPECT_EQ(available_spare_threshold_pct, 25);
}

TEST(SmartctlCheckUtilsTest, BadParseBadSpare) {
  int available_spare_pct, available_spare_threshold_pct;
  EXPECT_FALSE(ScrapeAvailableSparePercents(
      base::StringPrintf(kSmartctlOutputFormat, "", "bad", "10%"),
      &available_spare_pct, &available_spare_threshold_pct));
}

TEST(SmartctlCheckUtilsTest, BadParseBadSpareThreshold) {
  int available_spare_pct, available_spare_threshold_pct;
  EXPECT_FALSE(ScrapeAvailableSparePercents(
      base::StringPrintf(kSmartctlOutputFormat, "", "100%", "10"),
      &available_spare_pct, &available_spare_threshold_pct));
}

TEST(SmartctlCheckUtilsTest, BadParseOnlySpareThreshold) {
  int available_spare_pct, available_spare_threshold_pct;
  EXPECT_FALSE(ScrapeAvailableSparePercents("Available Spare Threshold: 10%",
                                            &available_spare_pct,
                                            &available_spare_threshold_pct));
}

TEST(SmartctlCheckUtilsTest, BadParseOnlySpare) {
  int available_spare_pct, available_spare_threshold_pct;
  EXPECT_FALSE(ScrapeAvailableSparePercents("Available Spare: 80%",
                                            &available_spare_pct,
                                            &available_spare_threshold_pct));
}

TEST(SmartctlCheckUtilsTest, BadParseOnlyBadSpare) {
  int available_spare_pct, available_spare_threshold_pct;
  EXPECT_FALSE(ScrapeAvailableSparePercents("Available Spare: bad",
                                            &available_spare_pct,
                                            &available_spare_threshold_pct));
}

TEST(SmartctlCheckUtilsTest, BadParseOnlyBadSpareThreshold) {
  int available_spare_pct, available_spare_threshold_pct;
  EXPECT_FALSE(ScrapeAvailableSparePercents("Available Spare Threshold: bad",
                                            &available_spare_pct,
                                            &available_spare_threshold_pct));
}

}  // namespace diagnostics
