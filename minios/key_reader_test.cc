// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "minios/key_reader.h"

using testing::_;

class KeyReaderTest : public ::testing::Test {
 public:
  void SetUp() override { ev_.value = 0; }
  struct input_event ev_;
};

class MockKeyReader : public key_reader::KeyReader {
 public:
  MockKeyReader() : KeyReader(true) {}
  explicit MockKeyReader(bool include_usb) : KeyReader(include_usb) {}
  MOCK_METHOD(bool, GetEpEvent, (int epfd, struct input_event* ev, int* index));
  MOCK_METHOD(bool, GetValidFds, (bool check_supported_keys));
  MOCK_METHOD(bool, EpollCreate, (base::ScopedFD * epfd));
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

TEST_F(KeyReaderTest, EvWaitKeyEnter) {
  MockKeyReader key_reader;

  testing::InSequence s;
  EXPECT_CALL(key_reader, GetValidFds(true)).WillOnce(testing::Return(true));
  EXPECT_CALL(key_reader, EpollCreate(_)).WillOnce(testing::Return(true));

  // Records both key press and key release before returning. Other calls are
  // ignored
  struct input_event ev_press {
    .type = EV_KEY, .code = 28, .value = 1,
  };
  EXPECT_CALL(key_reader, GetEpEvent(_, _, _))
      .WillOnce(
          DoAll(testing::SetArgPointee<1>(ev_press), testing::Return(true)));

  // Other key presses are ignored.
  ev_press.code = 45;
  EXPECT_CALL(key_reader, GetEpEvent(_, _, _))
      .WillOnce(
          DoAll(testing::SetArgPointee<1>(ev_press), testing::Return(true)));

  // Non EV_KEY calls are ignored
  struct input_event ev_input {
    .type = EV_LED, .code = 28, .value = 0,
  };
  EXPECT_CALL(key_reader, GetEpEvent(_, _, _))
      .WillOnce(
          DoAll(testing::SetArgPointee<1>(ev_input), testing::Return(true)));

  // Key release recorded.
  struct input_event ev_release {
    .type = EV_KEY, .code = 28, .value = 0,
  };
  EXPECT_CALL(key_reader, GetEpEvent(_, _, _))
      .WillOnce(
          DoAll(testing::SetArgPointee<1>(ev_release), testing::Return(true)));

  int index;
  EXPECT_TRUE(key_reader.EvWaitForKeys({28, 103}, &index));
}

TEST_F(KeyReaderTest, EvWaitKeyFileError) {
  MockKeyReader key_reader;
  EXPECT_CALL(key_reader, GetValidFds(true)).WillOnce(testing::Return(false));

  int index;
  EXPECT_FALSE(key_reader.EvWaitForKeys({28, 103}, &index));
}

TEST_F(KeyReaderTest, EvWaitKeyEpollError) {
  MockKeyReader key_reader;

  EXPECT_CALL(key_reader, GetValidFds(true)).WillOnce(testing::Return(true));
  EXPECT_CALL(key_reader, EpollCreate(_)).WillOnce(testing::Return(true));
  EXPECT_CALL(key_reader, GetEpEvent(_, _, _)).WillOnce(testing::Return(false));

  int index;
  EXPECT_FALSE(key_reader.EvWaitForKeys({28, 103}, &index));
}

TEST_F(KeyReaderTest, OnlyEvWaitKeyFunction) {
  MockKeyReader key_reader;
  // Cannot access password functions.
  EXPECT_FALSE(key_reader.GetInput());
}

TEST_F(KeyReaderTest, OnlyEvWaitKeyFunctionFalse) {
  MockKeyReader key_reader(false);
  // Cannot access password functions when include usb is false.
  EXPECT_FALSE(key_reader.GetInput());
}
