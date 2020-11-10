// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "vm_tools/garcon/mime_types_parser.h"

namespace vm_tools {
namespace garcon {

namespace {

class MimeTypesParserTest : public ::testing::Test {
 public:
  MimeTypesParserTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    mime_types_path_ = temp_dir_.GetPath().Append("mime.types");
  }
  MimeTypesParserTest(const MimeTypesParserTest&) = delete;
  MimeTypesParserTest& operator=(const MimeTypesParserTest&) = delete;

  ~MimeTypesParserTest() override = default;

  void WriteContents(const std::string& file_contents) {
    EXPECT_EQ(file_contents.size(),
              base::WriteFile(mime_types_path_, file_contents.c_str(),
                              file_contents.size()));
  }

  std::string TempFilePath() const { return mime_types_path_.value(); }

 private:
  base::ScopedTempDir temp_dir_;
  base::FilePath mime_types_path_;
};

}  // namespace

TEST_F(MimeTypesParserTest, NonExistentFileFails) {
  MimeTypeMap map;
  EXPECT_FALSE(ParseMimeTypes("/invalid/filepath/foo", &map));
}

TEST_F(MimeTypesParserTest, ValidResult1) {
  MimeTypeMap map;
  WriteContents(
      u8R"xxx(
    # This is a comment
    mime-type-1/foo
    application/pdf     pdf
    text/plain       txt   doc foo
    # Another comment
    aa/bb/cc     aa  cc
    ignore/me
    )xxx");
  EXPECT_TRUE(ParseMimeTypes(TempFilePath(), &map));
  MimeTypeMap expected = {
      {"pdf", "application/pdf"}, {"txt", "text/plain"}, {"doc", "text/plain"},
      {"foo", "text/plain"},      {"aa", "aa/bb/cc"},    {"cc", "aa/bb/cc"},
  };
  EXPECT_EQ(map, expected);
}

TEST_F(MimeTypesParserTest, ValidResult2) {
  MimeTypeMap map;
  WriteContents(
      u8R"xxx(
    application/postscript  ps ai eps epsi
    text/plain       txt   doc foo
    # More comments
    application/rtf
    application/vnc.debian.binary-package  deb ddeb udeb
    audio/foo
    image/png  png
    image/jpeg jpeg jpe jpg
    text/override   doc
    )xxx");
  EXPECT_TRUE(ParseMimeTypes(TempFilePath(), &map));
  MimeTypeMap expected = {
      {"ps", "application/postscript"},
      {"ai", "application/postscript"},
      {"eps", "application/postscript"},
      {"epsi", "application/postscript"},
      {"txt", "text/plain"},
      {"doc", "text/override"},
      {"foo", "text/plain"},
      {"deb", "application/vnc.debian.binary-package"},
      {"ddeb", "application/vnc.debian.binary-package"},
      {"udeb", "application/vnc.debian.binary-package"},
      {"png", "image/png"},
      {"jpeg", "image/jpeg"},
      {"jpe", "image/jpeg"},
      {"jpg", "image/jpeg"},
  };
  EXPECT_EQ(map, expected);
}

}  // namespace garcon
}  // namespace vm_tools
