// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_line_reader.h"

#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "gtest/gtest.h"

#include "croslog/file_map_reader.h"

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

class LogLineReaderTest : public ::testing::Test,
                          public LogLineReader::Observer {
 public:
  LogLineReaderTest() = default;
  LogLineReaderTest(const LogLineReaderTest&) = delete;
  LogLineReaderTest& operator=(const LogLineReaderTest&) = delete;

  void SetLogContentText(LogLineReader* reader, const char* text) {
    reader->OpenMemoryBufferForTest(text, strlen(text));
  }

  void OnFileChanged(LogLineReader* reader) override {
    changed_event_receieved_++;
  }
  int changed_event_receieved_ = 0;

  int changed_event_receieved() const { return changed_event_receieved_; }

  bool WaitForChangeEvent(int previous_value) {
    base::RunLoop().RunUntilIdle();

    const int kTinyTimeoutMs = 100;
    int max_try = 50;
    while (previous_value == changed_event_receieved_) {
      base::PlatformThread::Sleep(
          base::TimeDelta::FromMilliseconds(kTinyTimeoutMs));
      base::RunLoop().RunUntilIdle();
      max_try--;
      EXPECT_NE(0u, max_try);
      if (max_try == 0)
        return false;
    }
    return true;
  }
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

  {
    LogLineReader reader(LogLineReader::Backend::FILE);
    reader.OpenFile(base::FilePath("./testdata/TEST_EMPTY_FILE"));

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

  {
    LogLineReader reader(LogLineReader::Backend::FILE);
    reader.OpenFile(base::FilePath("./testdata/TEST_EMPTY_FILE"));

    EXPECT_FALSE(reader.Backward().has_value());
    EXPECT_FALSE(reader.Backward().has_value());

    reader.SetPositionLast();

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

TEST_F(LogLineReaderTest, ReadFileBeingWritten) {
  // This is not used explicitly but necessary for FileChange class.
  // base::MessageLoopForIO message_loop;

  base::FilePath temp_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));
  ASSERT_FALSE(temp_path.empty());

  LogLineReader reader(LogLineReader::Backend::FILE_FOLLOW);
  reader.AddObserver(this);
  reader.OpenFile(temp_path);

  base::File file(temp_path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  // Nothing to be read, since the file is empty.
  EXPECT_FALSE(reader.Forward().has_value());

  // Write and read
  {
    std::string test_string("TESTTEST");
    std::string test_string_with_lf = test_string + "\n";
    int previous_change_event_counter = changed_event_receieved();
    EXPECT_EQ(file.WriteAtCurrentPos(test_string_with_lf.c_str(),
                                     test_string_with_lf.length()),
              test_string_with_lf.length());
    WaitForChangeEvent(previous_change_event_counter);

    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string, s.value());
    EXPECT_FALSE(reader.Forward().has_value());
  }

  // Write and read
  {
    std::string test_string("HOGEHOGE");
    std::string test_string_with_lf = test_string + "\n";
    int previous_change_event_counter = changed_event_receieved();
    EXPECT_EQ(file.WriteAtCurrentPos(test_string_with_lf.c_str(),
                                     test_string_with_lf.length()),
              test_string_with_lf.length());
    WaitForChangeEvent(previous_change_event_counter);

    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string, s.value());
    EXPECT_FALSE(reader.Forward().has_value());
  }

  reader.RemoveObserver(this);
}

TEST_F(LogLineReaderTest, ReadFileRotated) {
  base::FilePath temp_path;
  base::FilePath temp_path2;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path2));

  LogLineReader reader(LogLineReader::Backend::FILE_FOLLOW);
  reader.AddObserver(this);
  reader.OpenFile(temp_path);

  base::File file(temp_path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  // Nothing to be read, since the file is empty.
  EXPECT_FALSE(reader.Forward().has_value());

  // Write and read
  {
    std::string test_string1("TESTTEST");
    std::string test_string1_with_lf = test_string1 + "\n";
    int previous_change_event_counter = changed_event_receieved();
    EXPECT_EQ(file.WriteAtCurrentPos(test_string1_with_lf.c_str(),
                                     test_string1_with_lf.length()),
              test_string1_with_lf.length());
    WaitForChangeEvent(previous_change_event_counter);

    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string1, s.value());
    EXPECT_FALSE(reader.Forward().has_value());
  }

  // Rotate
  {
    // Rename the old file.
    base::File::Error rename_error;
    int previous_change_event_counter = changed_event_receieved();
    ASSERT_TRUE(base::ReplaceFile(temp_path, temp_path2, &rename_error));
    WaitForChangeEvent(previous_change_event_counter);

    // Create a new file with the same file name.
    file = base::File(temp_path,
                      base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_WRITE);
    EXPECT_TRUE(file.IsValid());
    // Nothing to be read, since the new file is empty.
    EXPECT_FALSE(reader.Forward().has_value());
  }

  // Write and read
  {
    std::string test_string2("FUGAFUGA");
    std::string test_string2_with_lf = test_string2 + "\n";
    int previous_change_event_counter = changed_event_receieved();
    EXPECT_EQ(file.WriteAtCurrentPos(test_string2_with_lf.c_str(),
                                     test_string2_with_lf.length()),
              test_string2_with_lf.length());
    WaitForChangeEvent(previous_change_event_counter);

    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string2, s.value());
    EXPECT_FALSE(reader.Forward().has_value());
  }
  reader.RemoveObserver(this);
}

TEST_F(LogLineReaderTest, ReadFileRotatedMisorder) {
  base::FilePath temp_path;
  base::FilePath temp_path2;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path2));

  std::string test_string1("TESTTEST");
  std::string test_string1_with_lf = test_string1 + "\n";
  std::string test_string2("FUGAFUGA");
  std::string test_string2_with_lf = test_string2 + "\n";

  LogLineReader reader(LogLineReader::Backend::FILE_FOLLOW);
  reader.AddObserver(this);
  reader.OpenFile(temp_path);

  base::File file(temp_path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);

  // Write to the first file.
  {
    int previous_change_event_counter = changed_event_receieved();
    EXPECT_EQ(file.WriteAtCurrentPos(test_string1_with_lf.c_str(),
                                     test_string1_with_lf.length()),
              test_string1_with_lf.length());
    WaitForChangeEvent(previous_change_event_counter);
  }

  // Rotate
  {
    base::File::Error rename_error;
    int previous_change_event_counter = changed_event_receieved();
    ASSERT_TRUE(base::ReplaceFile(temp_path, temp_path2, &rename_error));
    WaitForChangeEvent(previous_change_event_counter);

    file = base::File(temp_path,
                      base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_WRITE);
    EXPECT_TRUE(file.IsValid());
  }

  // Write to the second file.
  {
    int previous_change_event_counter = changed_event_receieved();
    EXPECT_EQ(file.WriteAtCurrentPos(test_string2_with_lf.c_str(),
                                     test_string2_with_lf.length()),
              test_string2_with_lf.length());
    EXPECT_EQ(previous_change_event_counter, changed_event_receieved());
  }

  // First read, should be from the first file.
  {
    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string1, s.value());
  }

  // First read, should be from the second file.
  {
    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string2, s.value());
  }

  EXPECT_FALSE(reader.Forward().has_value());

  reader.RemoveObserver(this);
}

TEST_F(LogLineReaderTest, ReadFileRotatedWithoutLf) {
  base::FilePath temp_path;
  base::FilePath temp_path2;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path2));

  // The first file doesn't end with '\n' but the whole can be read since the
  // file is rotated to new file.
  std::string test_string1("TESTTEST");

  std::string test_string2("FUGAFUGA");
  std::string test_string2_with_lf = test_string2 + "\n";

  LogLineReader reader(LogLineReader::Backend::FILE_FOLLOW);
  reader.AddObserver(this);
  reader.OpenFile(temp_path);

  base::File file(temp_path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);

  // Write to the first file.
  {
    int previous_change_event_counter = changed_event_receieved();
    EXPECT_EQ(
        file.WriteAtCurrentPos(test_string1.c_str(), test_string1.length()),
        test_string1.length());
    WaitForChangeEvent(previous_change_event_counter);
  }

  // Rotate
  {
    base::File::Error rename_error;
    int previous_change_event_counter = changed_event_receieved();
    ASSERT_TRUE(base::ReplaceFile(temp_path, temp_path2, &rename_error));
    WaitForChangeEvent(previous_change_event_counter);

    file = base::File(temp_path,
                      base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_WRITE);
    EXPECT_TRUE(file.IsValid());
  }

  // Write to the second file.
  {
    int previous_change_event_counter = changed_event_receieved();
    EXPECT_EQ(file.WriteAtCurrentPos(test_string2_with_lf.c_str(),
                                     test_string2_with_lf.length()),
              test_string2_with_lf.length());
    EXPECT_EQ(previous_change_event_counter, changed_event_receieved());
  }

  // First read, should be from the first file.
  {
    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string1, s.value());
  }

  // First read, should be from the second file.
  {
    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string2, s.value());
  }

  EXPECT_FALSE(reader.Forward().has_value());

  reader.RemoveObserver(this);
}

TEST_F(LogLineReaderTest, ReadLarge) {
  LogLineReader::SetMaxLineLengthForTest(8 * 1024);
  FileMapReader::SetBlockSizesForTest(8 * 1024, 2);

  base::FilePath temp_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));

  base::File file(temp_path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);

  // Write
  for (int i = 0; i < (10 * 1024); i++) {
    std::string test_string = base::StringPrintf("%019d\n", i);
    EXPECT_EQ(file.WriteAtCurrentPos(test_string.c_str(), test_string.length()),
              test_string.length());
  }

  LogLineReader reader(LogLineReader::Backend::FILE_FOLLOW);
  reader.OpenFile(temp_path);

  // Read
  for (int i = 0; i < (10 * 1024); i++) {
    std::string test_string = base::StringPrintf("%019d", i);

    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string, s.value());
  }
  EXPECT_FALSE(reader.Forward().has_value());
}

TEST_F(LogLineReaderTest, ReadLargeAppend) {
  LogLineReader::SetMaxLineLengthForTest(8 * 1024);
  FileMapReader::SetBlockSizesForTest(8 * 1024, 2);

  base::FilePath temp_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));

  LogLineReader reader(LogLineReader::Backend::FILE_FOLLOW);
  reader.AddObserver(this);
  reader.OpenFile(temp_path);

  base::File file(temp_path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  // Nothing to be read, since the file is empty.
  EXPECT_FALSE(reader.Forward().has_value());

  // Write and read
  for (int i = 0; i < 10; i++) {
    // 2000 byte line including LF.
    std::string test_string = base::StringPrintf("%1999d", i);
    std::string test_string_with_lf = test_string + "\n";

    int previous_change_event_counter = changed_event_receieved();
    EXPECT_EQ(file.WriteAtCurrentPos(test_string_with_lf.c_str(),
                                     test_string_with_lf.length()),
              test_string_with_lf.length());
    WaitForChangeEvent(previous_change_event_counter);

    base::Optional<std::string> s = reader.Forward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string, s.value());
    EXPECT_FALSE(reader.Forward().has_value());
  }

  EXPECT_FALSE(reader.Forward().has_value());
  reader.RemoveObserver(this);
}

TEST_F(LogLineReaderTest, ReadLargeBackward) {
  LogLineReader::SetMaxLineLengthForTest(8 * 1024);
  FileMapReader::SetBlockSizesForTest(8 * 1024, 2);

  base::FilePath temp_path;
  ASSERT_TRUE(base::CreateTemporaryFile(&temp_path));

  base::File file(temp_path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);

  // Write
  for (int i = 0; i < (10 * 1024); i++) {
    std::string test_string = base::StringPrintf("%019d\n", i);
    EXPECT_EQ(file.WriteAtCurrentPos(test_string.c_str(), test_string.length()),
              test_string.length());
  }

  LogLineReader reader(LogLineReader::Backend::FILE_FOLLOW);
  reader.OpenFile(temp_path);
  reader.SetPositionLast();

  // Read
  for (int i = 10 * 1024 - 1; i >= 0; i--) {
    std::string test_string = base::StringPrintf("%019d", i);

    base::Optional<std::string> s = reader.Backward();
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(test_string, s.value());
  }
  EXPECT_FALSE(reader.Backward().has_value());
}

}  // namespace croslog
