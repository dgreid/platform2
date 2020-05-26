// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_line_reader.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "gtest/gtest.h"

namespace croslog {

namespace {

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

}  // anonymous namespace

class LogLineReaderTest : public ::testing::Test {
 public:
  LogLineReaderTest() = default;

  void SetLogContentText(LogLineReader* reader, const char* text) {
    reader->OpenMemoryBufferForTest(text, strlen(text));
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(LogLineReaderTest);
};

TEST_F(LogLineReaderTest, Forward) {
  {
    LogLineReader reader(LogLineReader::Backend::FILE);
    reader.OpenFile(base::FilePath("./testdata/TEST_NORMAL_LINES"));

    for (size_t i = 0; i < base::size(kNormalLines); i++) {
      base::Optional<std::string> s = reader.Forward();
      EXPECT_TRUE(s.has_value());
      EXPECT_EQ(kNormalLines[i], s.value());
    }

    EXPECT_FALSE(reader.Forward().has_value());
    EXPECT_FALSE(reader.Forward().has_value());
  }

  {
    LogLineReader reader(LogLineReader::Backend::FILE);
    reader.OpenFile(base::FilePath("./testdata/TEST_CRAZY_LINES"));

    for (int i = 0; i < base::size(kCrazyLines); i++) {
      base::Optional<std::string> s = reader.Forward();
      EXPECT_TRUE(s.has_value());
      EXPECT_EQ(kCrazyLines[i], s);
    }

    EXPECT_FALSE(reader.Forward().has_value());
    EXPECT_FALSE(reader.Forward().has_value());
  }

  {
    LogLineReader reader(LogLineReader::Backend::FILE);
    reader.OpenFile(base::FilePath("./testdata/TEST_EMPTY_LINES"));

    for (int i = 0; i < base::size(kEmptyLines); i++) {
      base::Optional<std::string> s = reader.Forward();
      EXPECT_TRUE(s.has_value());
      EXPECT_EQ(kEmptyLines[i], s);
    }

    EXPECT_FALSE(reader.Forward().has_value());
    EXPECT_FALSE(reader.Forward().has_value());
  }
}

TEST_F(LogLineReaderTest, Backward) {
  {
    LogLineReader reader(LogLineReader::Backend::FILE);
    reader.OpenFile(base::FilePath("./testdata/TEST_NORMAL_LINES"));

    EXPECT_FALSE(reader.Backward().has_value());
    EXPECT_FALSE(reader.Backward().has_value());

    reader.SetPositionLast();

    for (int i = base::size(kNormalLines) - 1; i >= 0; i--) {
      base::Optional<std::string> s = reader.Backward();
      EXPECT_TRUE(s.has_value());
      EXPECT_EQ(kNormalLines[i], s);
    }

    EXPECT_FALSE(reader.Backward().has_value());
    EXPECT_FALSE(reader.Backward().has_value());
  }

  {
    LogLineReader reader(LogLineReader::Backend::FILE);
    reader.OpenFile(base::FilePath("./testdata/TEST_CRAZY_LINES"));

    EXPECT_FALSE(reader.Backward().has_value());
    EXPECT_FALSE(reader.Backward().has_value());

    reader.SetPositionLast();

    for (int i = base::size(kCrazyLines) - 1; i >= 0; i--) {
      base::Optional<std::string> s = reader.Backward();
      EXPECT_TRUE(s.has_value());
      EXPECT_EQ(kCrazyLines[i], s);
    }

    EXPECT_FALSE(reader.Backward().has_value());
    EXPECT_FALSE(reader.Backward().has_value());
  }

  {
    LogLineReader reader(LogLineReader::Backend::FILE);
    reader.OpenFile(base::FilePath("./testdata/TEST_EMPTY_LINES"));

    EXPECT_FALSE(reader.Backward().has_value());
    EXPECT_FALSE(reader.Backward().has_value());

    reader.SetPositionLast();

    for (int i = base::size(kEmptyLines) - 1; i >= 0; i--) {
      base::Optional<std::string> s = reader.Backward();
      EXPECT_TRUE(s.has_value());
      EXPECT_EQ(kEmptyLines[i], s);
    }

    EXPECT_FALSE(reader.Backward().has_value());
    EXPECT_FALSE(reader.Backward().has_value());
  }
}

TEST_F(LogLineReaderTest, ForwardAndBackward) {
  LogLineReader reader(LogLineReader::Backend::FILE);
  reader.OpenFile(base::FilePath("./testdata/TEST_NORMAL_LINES"));

  for (size_t i = 0; i < base::size(kNormalLines); i++) {
    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(kNormalLines[i], s);
  }

  EXPECT_FALSE(reader.Forward().has_value());
  EXPECT_FALSE(reader.Forward().has_value());

  for (int i = base::size(kNormalLines) - 1; i >= 0; i--) {
    base::Optional<std::string> s = reader.Backward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(kNormalLines[i], s);
  }

  EXPECT_FALSE(reader.Backward().has_value());
  EXPECT_FALSE(reader.Backward().has_value());
}

TEST_F(LogLineReaderTest, AppendingLines) {
  LogLineReader reader(LogLineReader::Backend::MEMORY_FOR_TEST);
  reader.OpenMemoryBufferForTest("", 0);

  for (size_t i = 0; i < base::size(kAppendingLines); i++) {
    const char* logFileContent = kAppendingLines[i][1];
    reader.OpenMemoryBufferForTest(logFileContent, strlen(logFileContent));

    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(kAppendingLines[i][0], s);

    EXPECT_FALSE(reader.Forward().has_value());
  }
}

TEST_F(LogLineReaderTest, LastPosition) {
  LogLineReader reader(LogLineReader::Backend::MEMORY_FOR_TEST);

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

TEST_F(LogLineReaderTest, ReadEmptyFile) {
  base::FilePath temp_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));
  ASSERT_FALSE(temp_path.empty());

  LogLineReader reader(LogLineReader::Backend::FILE);
  reader.OpenFile(temp_path);

  // Nothing to be read, since the file is empty.
  EXPECT_FALSE(reader.Forward().has_value());
  EXPECT_FALSE(reader.Forward().has_value());
}

}  // namespace croslog
