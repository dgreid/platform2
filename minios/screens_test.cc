// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "minios/screens.h"

using testing::_;

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
  brillo::TouchFile(screens_path_.Append("fr").Append("minios_token.png"));

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
  brillo::TouchFile(screens_path_.Append("fr").Append("not_minios_token.png"));
  brillo::TouchFile(screens_path_.Append("en-US").Append("minios_token.png"));

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
  brillo::TouchFile(
      screens_path_.Append("en-US").Append("title_minios_token.png"));
  brillo::TouchFile(
      screens_path_.Append("en-US").Append("desc_minios_token.png"));

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

TEST_F(ScreensTest, UpdateButtons) {
  int index = 1;
  bool enter = false;
  int menu_items = 4;

  screens_.UpdateButtons(menu_items, kKeyUp, &index, &enter);
  EXPECT_EQ(0, index);

  // Test range.
  screens_.UpdateButtons(menu_items, kKeyUp, &index, &enter);
  EXPECT_EQ(0, index);
  // Move to last item.
  index = menu_items - 1;
  screens_.UpdateButtons(menu_items, kKeyDown, &index, &enter);
  EXPECT_EQ(menu_items - 1, index);
  EXPECT_FALSE(enter);
  // Enter key pressed.
  index = 1;
  screens_.UpdateButtons(menu_items, kKeyEnter, &index, &enter);
  EXPECT_EQ(1, index);
  EXPECT_TRUE(enter);

  // Unknown key, no action taken.
  index = 2;
  enter = false;
  screens_.UpdateButtons(menu_items, 89, &index, &enter);
  EXPECT_EQ(2, index);
  EXPECT_FALSE(enter);

  // If index somehow goes out of range, reset to 0.
  index = menu_items + 5;
  enter = false;
  screens_.UpdateButtons(menu_items, kKeyEnter, &index, &enter);
  EXPECT_EQ(0, index);
}

TEST_F(ScreensTest, UpdateButtonsIsDetachable) {
  int index = 1;
  bool enter = false;
  int menu_items = 4;

  screens_.UpdateButtons(menu_items, kKeyVolUp, &index, &enter);
  EXPECT_EQ(0, index);

  // Test range.
  screens_.UpdateButtons(menu_items, kKeyVolUp, &index, &enter);
  EXPECT_EQ(0, index);
  // Move to last item.
  index = menu_items - 1;
  screens_.UpdateButtons(menu_items, kKeyVolDown, &index, &enter);
  EXPECT_EQ(3, index);
  EXPECT_FALSE(enter);
  // Enter key pressed.
  index = 1;
  screens_.UpdateButtons(menu_items, kKeyPower, &index, &enter);
  EXPECT_EQ(1, index);
  EXPECT_TRUE(enter);
}

class MockScreens : public Screens {
 public:
  MockScreens() = default;
  MOCK_METHOD(bool,
              ShowBox,
              (int offset_x,
               int offset_y,
               int size_x,
               int size_y,
               const std::string& color));
  MOCK_METHOD(bool,
              ShowImage,
              (const base::FilePath& image_name, int offset_x, int offset_y));
  MOCK_METHOD(bool,
              ShowMessage,
              (const std::string& message_token, int offset_x, int offset_y));
};

class ScreensTestMocks : public ::testing::Test {
 public:
  void SetUp() override {
    base::ScopedTempDir temp_dir_;
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    screens_path_ = base::FilePath(temp_dir_.GetPath()).Append(kScreens);
    brillo::TouchFile(screens_path_.Append("en-US").Append("constants.sh"));
    mock_screens_.SetRootForTest(temp_dir_.GetPath().value());
    mock_screens_.Init();
  }

 protected:
  base::FilePath screens_path_;
  MockScreens mock_screens_;
};

TEST_F(ScreensTestMocks, ShowButtonFocused) {
  const int offset_y = 50;
  const int inner_width = 45;
  std::string message = "btn_enter";

  // Clear the button area.
  EXPECT_CALL(mock_screens_, ShowBox(_, offset_y, _, _, kMenuBlack))
      .WillRepeatedly(testing::Return(true));

  // Show button.
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_left_focused.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_right_focused.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, offset_y, inner_width, _, kMenuBlue))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowMessage(message + "_focused", _, offset_y))
      .WillOnce(testing::Return(true));

  brillo::TouchFile(
      screens_path_.Append("en-US").Append(message + "_focused.png"));
  mock_screens_.ShowButton(message, offset_y, /* focus=*/true, inner_width);
}

TEST_F(ScreensTestMocks, ShowButton) {
  const int offset_y = 50;
  const int inner_width = 45;
  const std::string message = "btn_enter";

  // Clear the button area.
  EXPECT_CALL(mock_screens_, ShowBox(_, offset_y, _, _, kMenuBlack))
      .WillRepeatedly(testing::Return(true));

  // Show button.
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_left.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_right.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowMessage(message, _, offset_y))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, _, _, _, kMenuButtonFrameGrey))
      .Times(2)
      .WillRepeatedly(testing::Return(true));

  brillo::TouchFile(screens_path_.Append("en-US").Append(message + ".png"));
  mock_screens_.ShowButton(message, offset_y, /* focus=*/false, inner_width);
}

TEST_F(ScreensTestMocks, ShowStepper) {
  const std::string step1 = "done";
  const std::string step2 = "2";
  const std::string step3 = "error";

  // Create icons.
  brillo::TouchFile(screens_path_.Append("ic_" + step1 + ".png"));
  brillo::TouchFile(screens_path_.Append("ic_" + step2 + ".png"));
  brillo::TouchFile(screens_path_.Append("ic_" + step3 + ".png"));

  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_" + step1 + ".png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_" + step2 + ".png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_" + step3 + ".png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, _, _, _, kMenuGrey))
      .Times(2)
      .WillRepeatedly(testing::Return(true));

  mock_screens_.ShowStepper({step1, step2, step3});
}

TEST_F(ScreensTestMocks, ShowStepperError) {
  brillo::TouchFile(screens_path_.Append("ic_done.png"));

  const std::string step1 = "done";
  const std::string step2 = "2";
  const std::string step3 = "error";

  // Stepper icons not found. Default to done.
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_done.png"), _, _))
      .Times(3)
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, _, _, _, kMenuGrey))
      .Times(2)
      .WillRepeatedly(testing::Return(true));
  mock_screens_.ShowStepper({step1, step2, step3});
}

TEST_F(ScreensTestMocks, ShowLanguageMenu) {
  EXPECT_CALL(
      mock_screens_,
      ShowImage(screens_path_.Append("language_menu_bg_focused.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_language-globe.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_dropdown.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowMessage("language_folded", _, _))
      .WillOnce(testing::Return(true));

  mock_screens_.ShowLanguageMenu(/* focus=*/true);
}

TEST_F(ScreensTestMocks, ShowFooter) {
  // Show left and right footer components.
  EXPECT_CALL(mock_screens_,
              ShowMessage(testing::StartsWith("footer_left"), _, _))
      .Times(3)
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowMessage(testing::StartsWith("footer_right"), _, _))
      .Times(2)
      .WillRepeatedly(testing::Return(true));

  // Show key icons and QR code and HWID text glyphs.
  EXPECT_CALL(mock_screens_, ShowImage(_, _, _))
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, _, _, _, kMenuGrey))
      .WillOnce(testing::Return(true));

  mock_screens_.ShowFooter();
}

}  // namespace screens
