// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/filesystem_label.h"

#include <string>

#include <gtest/gtest.h>

namespace cros_disks {

namespace {

// A subset of known forbidden characters for testing
const char kForbiddenTestCharacters[] = {
    '*',  '?',  '.',  ',',    ';',    ':',    '/', '\\', '|',
    '+',  '=',  '<',  '>',    '[',    ']',    '"', '\'', '\t',
    '\v', '\r', '\n', '\x02', '\x10', '\x7f', '\0'};

};  // namespace

TEST(FilesystemLabelTest, ValidateVolumeLabel) {
  // Test long volume names
  EXPECT_EQ(LabelErrorType::kLabelErrorLongName,
            ValidateVolumeLabel("ABCDEFGHIJKL", "vfat"));
  EXPECT_EQ(LabelErrorType::kLabelErrorLongName,
            ValidateVolumeLabel("ABCDEFGHIJKLMNOP", "exfat"));
  EXPECT_EQ(LabelErrorType::kLabelErrorLongName,
            ValidateVolumeLabel("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFG", "ntfs"));

  // Test volume name length limits
  EXPECT_EQ(LabelErrorType::kLabelErrorNone,
            ValidateVolumeLabel("ABCDEFGHIJK", "vfat"));
  EXPECT_EQ(LabelErrorType::kLabelErrorNone,
            ValidateVolumeLabel("ABCDEFGHIJKLMNO", "exfat"));
  EXPECT_EQ(LabelErrorType::kLabelErrorNone,
            ValidateVolumeLabel("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEF", "ntfs"));

  // Test unsupported file system type
  EXPECT_EQ(LabelErrorType::kLabelErrorUnsupportedFilesystem,
            ValidateVolumeLabel("ABC", "nonexistent-fs"));
}

class FilesystemLabelCharacterTest
    : public ::testing::TestWithParam<const char*> {};

INSTANTIATE_TEST_SUITE_P(AsciiRange,
                         FilesystemLabelCharacterTest,
                         testing::Values("vfat", "exfat", "ntfs"));

TEST_P(FilesystemLabelCharacterTest, ValidateVolumeLabelCharacters) {
  const char* filesystem = GetParam();

  // Test allowed characters in volume name
  EXPECT_EQ(LabelErrorType::kLabelErrorNone,
            ValidateVolumeLabel("AZaz09", filesystem));
  EXPECT_EQ(LabelErrorType::kLabelErrorNone,
            ValidateVolumeLabel(" !#$%&()", filesystem));
  EXPECT_EQ(LabelErrorType::kLabelErrorNone,
            ValidateVolumeLabel("-@^_`{}~", filesystem));

  for (char c : kForbiddenTestCharacters) {
    // Test forbidden characters in volume name
    EXPECT_EQ(LabelErrorType::kLabelErrorInvalidCharacter,
              ValidateVolumeLabel(std::string("ABC") + c, filesystem));
  }
}

}  // namespace cros_disks
