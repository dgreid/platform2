// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "minios/screens.h"

namespace screens {

class ScreensTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_root_ = temp_dir_.GetPath().value();
    screens_.SetRootForTest(test_root_);

    screens_path_ = base::FilePath(test_root_).Append(kScreens);

    base::FilePath locale_dir_en =
        base::FilePath(screens_path_).Append("en-US");
    ASSERT_TRUE(base::CreateDirectory(locale_dir_en));
    base::FilePath locale_dir_fr = base::FilePath(screens_path_).Append("fr");
    ASSERT_TRUE(CreateDirectory(locale_dir_fr));
    // Create and write constants file.
    std::string token_consts =
        "TITLE_minios_token_HEIGHT=38 \nDESC_minios_token_HEIGHT=44\n"
        "DESC_screen_token_HEIGHT=incorrect\n";
    ASSERT_TRUE(
        base::WriteFile(locale_dir_en.Append("constants.sh"), token_consts));

    // Create console and glyph directories.
    ASSERT_TRUE(
        base::CreateDirectory(base::FilePath(test_root_).Append("dev/pts")));
    console_ = base::FilePath(test_root_).Append("dev/pts/0");
    ASSERT_TRUE(base::WriteFile(console_, ""));
    ASSERT_TRUE(CreateDirectory(
        base::FilePath(screens_path_).Append("glyphs").Append("white")));
    // Load constants file.
    ASSERT_TRUE(screens_.Init());
  }

 protected:
  // Creates file in temp directory given relative path.
  void CreateFile(const base::FilePath& directory,
                  const std::string& file_name) {
    base::FilePath dir_path = base::FilePath(test_root_).Append(directory);
    if (!base::PathExists(dir_path))
      ASSERT_TRUE(CreateDirectory(dir_path));

    base::File file(dir_path.Append(file_name),
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    ASSERT_TRUE(file.IsValid());
  }

  // Test directory.
  base::ScopedTempDir temp_dir_;
  // Path to output pts.
  base::FilePath console_;
  // Path to /etc/screens in test directory.
  base::FilePath screens_path_;

  Screens screens_;
  std::string test_root_;
};

TEST_F(ScreensTest, ShowText) {
  EXPECT_TRUE(screens_.ShowText("chrome", 200, -100, "white"));

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  std::string expected_command =
      "\x1B]image:file=" + test_root_ + "/etc/screens/glyphs/" +
      "white/99.png;offset=200,-100;scale=1/a\x1B]image:file=" + test_root_ +
      "/etc/screens/glyphs/white/"
      "104.png;offset=210,-100;scale=1/a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/glyphs/white/"
      "114.png;offset=220,-100;scale=1/a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/glyphs/white/"
      "111.png;offset=230,-100;scale=1/a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/glyphs/white/"
      "109.png;offset=240,-100;scale=1/a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/glyphs/white/"
      "101.png;offset=250,-100;scale=1/a";
  EXPECT_EQ(expected_command, written_command);
}

TEST_F(ScreensTest, ShowImageTest) {
  EXPECT_TRUE(screens_.ShowImage(base::FilePath(test_root_).Append("image.png"),
                                 50, 20));

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ(
      "\x1B]image:file=" + test_root_ + "/image.png;offset=50,20;scale=1/a",
      written_command);
}

TEST_F(ScreensTest, ShowImageRtl) {
  screens_.SetLocaleRtlForTest(true);
  EXPECT_TRUE(screens_.ShowImage(base::FilePath(test_root_).Append("image.png"),
                                 50, 10));

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ(
      "\x1B]image:file=" + test_root_ + "/image.png;offset=-50,10;scale=1/a",
      written_command);
}

TEST_F(ScreensTest, ShowBox) {
  EXPECT_TRUE(screens_.ShowBox(-100, -200, 50, 40, "0x8AB4F8"));
  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ("\x1B]box:color=0x8AB4F8;size=50,40;offset=-100,-200;scale=1\a",
            written_command);
}

TEST_F(ScreensTest, ShowBoxRtl) {
  // Set locale to be read right to left.
  screens_.SetLocaleRtlForTest(true);
  EXPECT_TRUE(screens_.ShowBox(-100, -200, 50, 20, "0x8AB4F8"));
  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  // X offset should be inverted.
  EXPECT_EQ("\x1B]box:color=0x8AB4F8;size=50,20;offset=100,-200;scale=1\a",
            written_command);
}

TEST_F(ScreensTest, ShowMessage) {
  CreateFile(base::FilePath(kScreens).Append("fr"), "minios_token.png");

  // Override language to french.
  screens_.SetLanguageForTest("fr");
  EXPECT_TRUE(screens_.ShowMessage("minios_token", 0, 20));

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ("\x1B]image:file=" + test_root_ +
                "/etc/screens/fr/minios_token.png;offset=0,20;scale=1/a",
            written_command);
}

TEST_F(ScreensTest, ShowMessageFallback) {
  // Create french and english image files.
  CreateFile(base::FilePath(kScreens).Append("fr"), "not_minios_token.png");
  CreateFile(base::FilePath(kScreens).Append("en-US"), "minios_token.png");

  // Override language to french.
  screens_.SetLanguageForTest("fr");
  EXPECT_TRUE(screens_.ShowMessage("minios_token", 0, 20));

  // French token does not exist, fall back to english token.
  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ("\x1B]image:file=" + test_root_ +
                "/etc/screens/en-US/minios_token.png;offset=0,20;scale=1/a",
            written_command);
}

TEST_F(ScreensTest, InstructionsWithTitle) {
  // Create english title and description tokens.
  CreateFile(base::FilePath(kScreens).Append("en-US"),
             "title_minios_token.png");
  CreateFile(base::FilePath(kScreens).Append("en-US"), "desc_minios_token.png");

  screens_.InstructionsWithTitle("minios_token");

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  std::string expected_command =
      "\x1B]image:file=" + test_root_ +
      "/etc/screens/en-US/title_minios_token.png;offset=-180,-301;scale=1/"
      "a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/en-US/desc_minios_token.png;offset=-180,-244;scale=1/a";

  EXPECT_EQ(expected_command, written_command);
}

TEST_F(ScreensTest, ReadDimension) {
  std::string token_consts =
      "TITLE_minios_token_HEIGHT=\nDESC_minios_token_HEIGHT=44\nDESC_"
      "screen_token_HEIGHT=incorrect\n screen_whitespace_HEIGHT=  77  \n";
  ASSERT_TRUE(base::WriteFile(
      base::FilePath(screens_path_).Append("fr").Append("constants.sh"),
      token_consts));

  // Loads French dimension constants into memory.
  screens_.SetLanguageForTest("fr");

  EXPECT_EQ(4, screens_.image_dimensions_.size());
  EXPECT_EQ("  77", screens_.image_dimensions_[3].second);
}

TEST_F(ScreensTest, GetDimension) {
  EXPECT_EQ(-1, screens_.GetDimension("DESC_invalid_HEIGHT"));
  EXPECT_EQ(-1, screens_.GetDimension("incorrect_DESC_minios_token_HEIGHT"));

  // Not a number.
  EXPECT_EQ(-1, screens_.GetDimension("DESC_screen_token_HEIGHT"));

  // Correctly returns the dimension.
  EXPECT_EQ(38, screens_.GetDimension("TITLE_minios_token_HEIGHT"));
}

}  // namespace screens
