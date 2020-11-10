// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "installer/util/key_reader.h"

class KeyReaderTest : public ::testing::Test {
 public:
  void SetUp() override { ev_.value = 0; }
  struct input_event ev_;
};

TEST_F(KeyReaderTest, BasicKeyTest) {
  key_reader::KeyReader key_reader(true, false, "us");
  EXPECT_TRUE(key_reader.SetKeyboardContext());
  // Test Basic Numbers.
  ev_.code = 2;
  key_reader.GetChar(ev_);

  ev_.code = 4;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13", key_reader.GetUserInputForTest());

  // Test capitalization and special characters.
  // Left shift key down.
  ev_.code = 42;
  ev_.value = 1;
  key_reader.GetChar(ev_);

  ev_.code = 16;
  ev_.value = 0;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13Q", key_reader.GetUserInputForTest());

  ev_.code = 17;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13QW", key_reader.GetUserInputForTest());

  ev_.code = 3;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13QW@", key_reader.GetUserInputForTest());

  // Left shit key release.
  ev_.code = 42;
  ev_.value = 0;
  key_reader.GetChar(ev_);

  // No longer capitalized or special.
  ev_.code = 18;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13QW@e", key_reader.GetUserInputForTest());

  ev_.code = 3;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13QW@e2", key_reader.GetUserInputForTest());
}

TEST_F(KeyReaderTest, PrintableKeyTest) {
  //  key_reader.SetKeyboardContext("us");
  key_reader::KeyReader key_reader(true, false, "us");
  EXPECT_TRUE(key_reader.SetKeyboardContext());

  ev_.code = 2;
  key_reader.GetChar(ev_);

  ev_.code = 4;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13", key_reader.GetUserInputForTest());

  // Non-alphanumeric keys should not affect input length.
  // Left Shift.
  ev_.code = 42;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13", key_reader.GetUserInputForTest());

  // Escape.
  ev_.code = 1;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13", key_reader.GetUserInputForTest());

  // Left Alt.
  ev_.code = 56;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13", key_reader.GetUserInputForTest());

  // Tab.
  ev_.code = 15;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13", key_reader.GetUserInputForTest());

  // Ctrl.
  ev_.code = 29;
  key_reader.GetChar(ev_);
  EXPECT_EQ("13", key_reader.GetUserInputForTest());

  // Continue taking in input.
  ev_.code = 3;
  key_reader.GetChar(ev_);
  EXPECT_EQ("132", key_reader.GetUserInputForTest());

  // Space bar.
  ev_.code = 57;
  key_reader.GetChar(ev_);
  EXPECT_EQ("132 ", key_reader.GetUserInputForTest());
}

TEST_F(KeyReaderTest, InputLengthTest) {
  key_reader::KeyReader key_reader(true, false, "us");
  EXPECT_TRUE(key_reader.SetKeyboardContext());

  // Add max input chars.
  ev_.code = 52;
  for (int i = 0; i < key_reader::kMaxInputLength; i++) {
    key_reader.GetChar(ev_);
  }

  EXPECT_EQ(std::string(key_reader::kMaxInputLength, '.'),
            key_reader.GetUserInputForTest());

  // Cannot add past kMaxInputLength.
  ev_.code = 3;
  key_reader.GetChar(ev_);
  EXPECT_EQ(std::string(key_reader::kMaxInputLength, '.'),
            key_reader.GetUserInputForTest());

  // Test backspace. individual key press.
  ev_.code = 14;
  for (int i = 0; i < 20; i++) {
    key_reader.GetChar(ev_);
  }

  EXPECT_EQ(std::string(key_reader::kMaxInputLength - 20, '.'),
            key_reader.GetUserInputForTest());

  // Back space repeated keypress.
  // Stop deleting when string empty.
  ev_.value = 2;
  int remaining_chars =
      key_reader::kBackspaceSensitivity * (key_reader::kMaxInputLength - 20);
  for (int i = 0; i < remaining_chars + 2; i++) {
    key_reader.GetChar(ev_);
  }

  EXPECT_EQ("", key_reader.GetUserInputForTest());
}

TEST_F(KeyReaderTest, ReturnKeyTest) {
  key_reader::KeyReader key_reader(true, false, "us");
  EXPECT_TRUE(key_reader.SetKeyboardContext());

  // Return key press should return true.

  ev_.code = 28;
  EXPECT_TRUE(key_reader.GetChar(ev_));

  ev_.code = 16;
  ev_.value = 0;
  for (int i = 0; i < 5; i++) {
    key_reader.GetChar(ev_);
  }
  EXPECT_EQ("qqqqq", key_reader.GetUserInputForTest());

  ev_.code = 28;
  EXPECT_TRUE(key_reader.GetChar(ev_));
}

TEST_F(KeyReaderTest, FrenchKeyTest) {
  key_reader::KeyReader key_reader(true, false, "fr");
  EXPECT_TRUE(key_reader.SetKeyboardContext());

  ev_.code = 16;
  key_reader.GetChar(ev_);

  ev_.code = 17;
  key_reader.GetChar(ev_);
  EXPECT_EQ("az", key_reader.GetUserInputForTest());

  ev_.code = 4;
  key_reader.GetChar(ev_);
  ev_.code = 5;
  key_reader.GetChar(ev_);
  EXPECT_EQ("az\"'", key_reader.GetUserInputForTest());

  // Not a printable ASCII (accent aigu), do not add to input.
  ev_.code = 8;
  key_reader.GetChar(ev_);
  EXPECT_EQ("az\"'", key_reader.GetUserInputForTest());

  // Test capitalization and special characters.
  // Left shift key down.
  ev_.code = 42;
  ev_.value = 1;
  key_reader.GetChar(ev_);

  ev_.value = 0;
  ev_.code = 17;
  key_reader.GetChar(ev_);
  EXPECT_EQ("az\"'Z", key_reader.GetUserInputForTest());

  ev_.code = 4;
  key_reader.GetChar(ev_);
  ev_.code = 5;
  key_reader.GetChar(ev_);
  EXPECT_EQ("az\"'Z34", key_reader.GetUserInputForTest());

  ev_.code = 42;
  ev_.value = 0;
  key_reader.GetChar(ev_);

  // Get third char on key.
  // ALTGR (right alt) + CTL key press.

  ev_.code = 29;
  ev_.value = 1;
  key_reader.GetChar(ev_);

  ev_.code = 100;
  ev_.value = 1;
  key_reader.GetChar(ev_);

  ev_.code = 4;
  ev_.value = 0;
  key_reader.GetChar(ev_);
  EXPECT_EQ("az\"'Z34#", key_reader.GetUserInputForTest());
}

TEST_F(KeyReaderTest, JapaneseKeyTest) {
  key_reader::KeyReader key_reader(true, false, "jp");
  EXPECT_TRUE(key_reader.SetKeyboardContext());

  ev_.code = 16;
  key_reader.GetChar(ev_);

  ev_.code = 17;
  key_reader.GetChar(ev_);
  EXPECT_EQ("qw", key_reader.GetUserInputForTest());

  ev_.code = 42;
  ev_.value = 1;
  key_reader.GetChar(ev_);

  ev_.value = 0;
  ev_.code = 4;
  key_reader.GetChar(ev_);
  ev_.code = 5;
  key_reader.GetChar(ev_);
  EXPECT_EQ("qw#$", key_reader.GetUserInputForTest());

  // Test capitalization and special characters.
  // Left shift key down.
  ev_.code = 42;
  ev_.value = 1;
  key_reader.GetChar(ev_);

  ev_.value = 0;
  ev_.code = 17;
  key_reader.GetChar(ev_);
  EXPECT_EQ("qw#$W", key_reader.GetUserInputForTest());

  ev_.code = 42;
  ev_.value = 0;
  key_reader.GetChar(ev_);

  // Get third char on key.
  // ALT + CTL key press.

  ev_.code = 29;
  ev_.value = 1;
  key_reader.GetChar(ev_);

  ev_.code = 56;
  ev_.value = 1;
  key_reader.GetChar(ev_);

  // Japanese character should not be added to input.
  ev_.code = 16;
  ev_.value = 0;
  key_reader.GetChar(ev_);
  EXPECT_EQ("qw#$W", key_reader.GetUserInputForTest());
}
