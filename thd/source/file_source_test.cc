// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

#include "thd/source/file_source.h"

namespace thd {

class FileSourceTest : public ::testing::Test {
 public:
  FileSourceTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    file_ = temp_dir_.GetPath().Append("content");
    source_ = std::make_unique<FileSource>(file_);
  }
  FileSourceTest(const FileSourceTest&) = delete;
  FileSourceTest& operator=(const FileSourceTest&) = delete;

  ~FileSourceTest() override {}

 protected:
  // Temporary directory containing a file used for the file source.
  base::ScopedTempDir temp_dir_;

  // Path to file that source is initialized with, and will read out of.
  base::FilePath file_;

  // File source object to test.
  std::unique_ptr<FileSource> source_;
};

// Test to verify that the value written to the file is read out properly.
// This is indicated by both ReadValue() returninig true, and
// |output_| being set to |input|.
TEST_F(FileSourceTest, Basic) {
  const int kInput = 17;
  int64_t output = -1;
  const std::string kInputStr = std::to_string(kInput);
  ASSERT_EQ(kInputStr.length(),
            base::WriteFile(file_, kInputStr.c_str(), kInputStr.length()));
  ASSERT_TRUE(source_->ReadValue(&output));
  EXPECT_EQ(kInput, output);
}

// Test to verify failure on file content being a string that
// cannot be parsed into an integer.
TEST_F(FileSourceTest, FileContentsNotInteger) {
  int64_t output = -1;
  const std::string kNonsenseInput = "nonsense";
  ASSERT_EQ(
      kNonsenseInput.length(),
      base::WriteFile(file_, kNonsenseInput.c_str(), kNonsenseInput.length()));
  EXPECT_FALSE(source_->ReadValue(&output));
}

// Test to verify that an invalid file path will be rejected, and
// that the evaluation of a file path's validity happens on each
// call to ReadValue() again.
TEST_F(FileSourceTest, FilePathInvalid) {
  const int kInput = 17;
  int64_t output = -1;
  const std::string kInputStr = std::to_string(kInput);
  base::FilePath old_location = file_;
  ASSERT_TRUE(base::DeleteFile(file_, false));
  ASSERT_FALSE(source_->ReadValue(&output));
  file_ = old_location;
  ASSERT_EQ(kInputStr.length(),
            base::WriteFile(file_, kInputStr.c_str(), kInputStr.length()));
  ASSERT_TRUE(source_->ReadValue(&output));
  EXPECT_EQ(kInput, output);
}

}  // namespace thd
