// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_entry_reader.h"

#include <base/files/file_path.h>
#include <gtest/gtest.h>

namespace croslog {

class LogEntryReaderTest : public ::testing::Test {
 public:
  LogEntryReaderTest() = default;
  ~LogEntryReaderTest() override = default;

  void SetLogContentText(LogEntryReader* reader, const char* text) {
    reader->OpenMemoryBufferForTest(text, strlen(text));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(LogEntryReaderTest);
};

const char* kNormalLines[] = {"Lorem ipsum dolor sit amet, consectetur",
                              "adipiscing elit, sed do eiusmod tempor",
                              "incididunt ut labore et dolore magna aliqua.",
                              "Ut enim ad minim veniam, quis nostrud",
                              "exercitation ullamco laboris nisi ut aliquip ex",
                              "ea commodo consequat. Duis aute irure dolor in",
                              "reprehenderit in voluptate velit esse cillum",
                              "dolore eu fugiat nulla pariatur."};

const char* kCrazyLines[] = {"",
                             "   Lorem ipsum dolor sit amet, consectetur",
                             " adipiscing elit, sed do eiusmod tempor ",
                             "",
                             "",
                             " incididunt ut labore et dolore magna aliqua."};

const char* kEmptyLines[] = {"", "", "", "", ""};

const char* kAppendingLines[][2] = {{"A", "A\n"},
                                    {"B", "A\nB\n"},
                                    {"C", "A\nB\nC\n"},
                                    {"D", "A\nB\nC\nD\n"},
                                    {"E", "A\nB\nC\nD\nE\n"}};

TEST_F(LogEntryReaderTest, Forward) {
  {
    LogEntryReader reader;
    reader.OpenFile(base::FilePath("./testdata/TEST_NORMAL_LINES"), false);

    for (size_t i = 0; i < base::size(kNormalLines); i++) {
      RawLogLineUnsafe s = reader.Forward();
      EXPECT_TRUE(s.data() != nullptr);
      EXPECT_EQ(kNormalLines[i], s);
    }

    EXPECT_TRUE(reader.Forward().data() == nullptr);
    EXPECT_TRUE(reader.Forward().data() == nullptr);
  }

  {
    LogEntryReader reader;
    reader.OpenFile(base::FilePath("./testdata/TEST_CRAZY_LINES"), false);

    for (int i = 0; i < base::size(kCrazyLines); i++) {
      RawLogLineUnsafe s = reader.Forward();
      EXPECT_TRUE(s.data() != nullptr);
      EXPECT_EQ(kCrazyLines[i], s);
    }

    EXPECT_TRUE(reader.Forward().data() == nullptr);
    EXPECT_TRUE(reader.Forward().data() == nullptr);
  }

  {
    LogEntryReader reader;
    reader.OpenFile(base::FilePath("./testdata/TEST_EMPTY_LINES"), false);

    for (int i = 0; i < base::size(kEmptyLines); i++) {
      RawLogLineUnsafe s = reader.Forward();
      EXPECT_TRUE(s.data() != nullptr);
      EXPECT_EQ(kEmptyLines[i], s);
    }

    EXPECT_TRUE(reader.Forward().data() == nullptr);
    EXPECT_TRUE(reader.Forward().data() == nullptr);
  }
}

TEST_F(LogEntryReaderTest, Backward) {
  {
    LogEntryReader reader;
    reader.OpenFile(base::FilePath("./testdata/TEST_NORMAL_LINES"), false);

    EXPECT_TRUE(reader.Backward().data() == nullptr);
    EXPECT_TRUE(reader.Backward().data() == nullptr);

    reader.SetPositionLast();

    for (int i = base::size(kNormalLines) - 1; i >= 0; i--) {
      RawLogLineUnsafe s = reader.Backward();
      EXPECT_TRUE(s.data() != nullptr);
      EXPECT_EQ(kNormalLines[i], s);
    }

    EXPECT_TRUE(reader.Backward().data() == nullptr);
    EXPECT_TRUE(reader.Backward().data() == nullptr);
  }

  {
    LogEntryReader reader;
    reader.OpenFile(base::FilePath("./testdata/TEST_CRAZY_LINES"), false);

    EXPECT_TRUE(reader.Backward().data() == nullptr);
    EXPECT_TRUE(reader.Backward().data() == nullptr);

    reader.SetPositionLast();

    for (int i = base::size(kCrazyLines) - 1; i >= 0; i--) {
      RawLogLineUnsafe s = reader.Backward();
      EXPECT_TRUE(s.data() != nullptr);
      EXPECT_EQ(kCrazyLines[i], s);
    }

    EXPECT_TRUE(reader.Backward().data() == nullptr);
    EXPECT_TRUE(reader.Backward().data() == nullptr);
  }

  {
    LogEntryReader reader;
    reader.OpenFile(base::FilePath("./testdata/TEST_EMPTY_LINES"), false);

    EXPECT_TRUE(reader.Backward().data() == nullptr);
    EXPECT_TRUE(reader.Backward().data() == nullptr);

    reader.SetPositionLast();

    for (int i = base::size(kEmptyLines) - 1; i >= 0; i--) {
      RawLogLineUnsafe s = reader.Backward();
      EXPECT_TRUE(s.data() != nullptr);
      EXPECT_EQ(kEmptyLines[i], s);
    }

    EXPECT_TRUE(reader.Backward().data() == nullptr);
    EXPECT_TRUE(reader.Backward().data() == nullptr);
  }
}

TEST_F(LogEntryReaderTest, ForwardAndBackward) {
  LogEntryReader reader;
  reader.OpenFile(base::FilePath("./testdata/TEST_NORMAL_LINES"), false);

  for (size_t i = 0; i < base::size(kNormalLines); i++) {
    RawLogLineUnsafe s = reader.Forward();
    EXPECT_TRUE(s.data() != nullptr);
    EXPECT_EQ(kNormalLines[i], s);
  }

  EXPECT_TRUE(reader.Forward().data() == nullptr);
  EXPECT_TRUE(reader.Forward().data() == nullptr);

  for (int i = base::size(kNormalLines) - 1; i >= 0; i--) {
    RawLogLineUnsafe s = reader.Backward();
    EXPECT_TRUE(s.data() != nullptr);
    EXPECT_EQ(kNormalLines[i], s);
  }

  EXPECT_TRUE(reader.Backward().data() == nullptr);
  EXPECT_TRUE(reader.Backward().data() == nullptr);
}

TEST_F(LogEntryReaderTest, AppendingLines) {
  LogEntryReader reader;
  reader.OpenMemoryBufferForTest("", 0);

  for (size_t i = 0; i < base::size(kAppendingLines); i++) {
    const char* logFileContent = kAppendingLines[i][1];
    reader.OpenMemoryBufferForTest(logFileContent, strlen(logFileContent));

    RawLogLineUnsafe s = reader.Forward();
    EXPECT_TRUE(s.data() != nullptr);
    EXPECT_EQ(kAppendingLines[i][0], s);

    EXPECT_TRUE(reader.Forward().data() == nullptr);
  }
}

TEST_F(LogEntryReaderTest, LastPosition) {
  LogEntryReader reader;

  SetLogContentText(&reader, "");
  reader.SetPositionLast();
  EXPECT_EQ(0u, reader.position());

  SetLogContentText(&reader, "A\nB\n");
  reader.SetPositionLast();
  EXPECT_EQ(4u, reader.position());

  SetLogContentText(&reader, "A\nB");
  reader.SetPositionLast();
  EXPECT_EQ(2u, reader.position());

  SetLogContentText(&reader, "A\n");
  reader.SetPositionLast();
  EXPECT_EQ(2u, reader.position());

  SetLogContentText(&reader, "\n");
  reader.SetPositionLast();
  EXPECT_EQ(1u, reader.position());
}

}  // namespace croslog
