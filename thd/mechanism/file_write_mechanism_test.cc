// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

#include "thd/mechanism/file_write_mechanism.h"
#include "thd/mechanism/mechanism.h"

namespace thd {

namespace {
// The constants below are arbitrary values to configure the
// FileWriteMechanism used in the tests below.
const int kMinLevel = 10;
const int kMaxLevel = 200;
const char kTestName[] = "test-name";

// Arbitrary value to use on SetLevel(int level) calls.
const int kInput = (kMinLevel + kMaxLevel) / 2;
}  // namespace

class FileWriteMechanismTest : public ::testing::Test {
 public:
  FileWriteMechanismTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    file_ = temp_dir_.GetPath().Append("content");
  }
  FileWriteMechanismTest(const FileWriteMechanismTest&) = delete;
  FileWriteMechanismTest& operator=(const FileWriteMechanismTest&) = delete;

  ~FileWriteMechanismTest() override {}

 protected:
  // Temporary directory containing a file used for the file mechanism.
  base::ScopedTempDir temp_dir_;

  // Path to file that the mechanism will write to.
  base::FilePath file_;
};

// Test that FileWriteMechanism rejects any request to SetLevel(level)
// where |level| < min level, or |level| > max level.
TEST_F(FileWriteMechanismTest, LevelOutOfBounds) {
  FileWriteMechanism mechanism(kMaxLevel, kMinLevel,
                               kMaxLevel, /* default level */
                               kTestName, file_);
  EXPECT_FALSE(mechanism.SetLevel(kMaxLevel + 5));
  EXPECT_FALSE(mechanism.SetLevel(kMinLevel - 5));
}

// Test that FileWriteMechanism getters return the values passed in the
// constructor.
TEST_F(FileWriteMechanismTest, DefaultLevel) {
  const int kDefaultLevel = kMinLevel + (kMaxLevel - kMinLevel) / 4;
  FileWriteMechanism mechanism(kMaxLevel, kMinLevel, kDefaultLevel, kTestName,
                               file_);
  EXPECT_EQ(kMaxLevel, mechanism.GetMaxLevel());
  EXPECT_EQ(kMinLevel, mechanism.GetMinLevel());
  EXPECT_EQ(kDefaultLevel, mechanism.GetDefaultLevel());
}

// Test standard behavior to verify expected value
// is written to the file.
TEST_F(FileWriteMechanismTest, RegularFile) {
  FileWriteMechanism mechanism(kMaxLevel, kMinLevel, kMaxLevel, kTestName,
                               file_);
  ASSERT_TRUE(mechanism.SetLevel(kInput));
  std::string file_content;
  ASSERT_TRUE(base::ReadFileToString(file_, &file_content));
  int output;
  ASSERT_TRUE(base::StringToInt(file_content, &output));
  EXPECT_EQ(kInput, output);
}

// Test to verify SetLevel(int64_t level) returning false when there
// is an error in file-writing (error in this case is file is ro).
TEST_F(FileWriteMechanismTest, InvalidPath) {
  FileWriteMechanism mechanism(kMinLevel, kMaxLevel, kMaxLevel, kTestName,
                               file_);
  base::File out_file = base::File(
      file_, base::File::Flags::FLAG_READ | base::File::Flags::FLAG_CREATE);
  EXPECT_FALSE(mechanism.SetLevel(kInput));
}

}  // namespace thd
