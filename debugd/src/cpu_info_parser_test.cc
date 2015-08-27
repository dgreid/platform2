// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <map>
#include <string>

#include "debugd/src/cpu_info_parser.h"

using debugd::CPUInfoParser;

namespace {

// A test file that contains CPU information. Taken from a lumpy ChromeOS box.
const char kTestCPUInfoFilename[] = "../src/testdata/cpu_info.txt";

}  // namespace

// Tests whether we can get the model name from the CPU info file.
TEST(CPUInfoParser, TestCPUModelName) {
  CPUInfoParser cpu_info_parser(kTestCPUInfoFilename);
  std::string model_name;
  EXPECT_TRUE(cpu_info_parser.GetKey("model name", &model_name));
  EXPECT_EQ(std::string("Intel(R) Celeron(R) CPU 867 @ 1.30GHz"), model_name);
}

// Tests whether we can handle a key that is not present in the CPU info file.
TEST(CPUInfoParser, TestMissingKey) {
  CPUInfoParser cpu_info_parser(kTestCPUInfoFilename);
  std::string model_name;
  EXPECT_FALSE(cpu_info_parser.GetKey("this is a missing key", &model_name));
}
