// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/missed_crash_collector.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

using ::testing::HasSubstr;

namespace {

void RunTestWithLogContents(base::StringPiece log_contents) {
  base::ScopedTempDir tmp_dir;
  ASSERT_TRUE(tmp_dir.CreateUniqueTempDir());

  base::FilePath input = tmp_dir.GetPath().Append("input.txt");
  base::WriteFile(input, log_contents.data(), log_contents.length());

  base::ScopedFILE input_file(fopen(input.value().c_str(), "r"));
  ASSERT_TRUE(input_file.get());

  MissedCrashCollector collector;
  collector.set_crash_directory_for_test(tmp_dir.GetPath());
  collector.set_input_file_for_testing(input_file.get());
  collector.Initialize([]() { return true; }, false /*early*/);
  constexpr int kPid = 234;
  EXPECT_TRUE(collector.Collect(kPid));

  base::FilePath meta_path;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      tmp_dir.GetPath(), "missed_crash.*.234.meta", &meta_path));
  base::FilePath log_path;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      tmp_dir.GetPath(), "missed_crash.*.234.log.gz", &log_path));

  // Check log contents.
  int decompress_result = system(("gunzip " + log_path.value()).c_str());
  EXPECT_TRUE(WIFEXITED(decompress_result));
  EXPECT_EQ(WEXITSTATUS(decompress_result), 0);
  std::string actual_log_contents;
  EXPECT_TRUE(base::ReadFileToString(log_path.RemoveFinalExtension(),
                                     &actual_log_contents));
  EXPECT_EQ(log_contents, actual_log_contents);

  // Check meta contents.
  std::string meta_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_path, &meta_contents));
  EXPECT_THAT(
      meta_contents,
      HasSubstr(base::StrCat({"payload=", log_path.BaseName().value()})));
  EXPECT_THAT(meta_contents, HasSubstr("sig=missed-crash"));
  EXPECT_THAT(meta_contents, HasSubstr("upload_var_pid=234"));
}

}  // namespace

TEST(MissedCrashCollectorTest, Basic) {
  constexpr char kInput[] = R"(===stuff===
1 2 3
===more stuff===
hello
)";
  RunTestWithLogContents(kInput);
}

// Ensure our private ReadFILEToString handles files larger than
// kDefaultChunkSize correctly.
TEST(MissedCrashCollectorTest, LargeInput) {
  std::string contents;
  for (int i = 0; i < MissedCrashCollector::kDefaultChunkSize; ++i) {
    base::StrAppend(&contents, {base::NumberToString(i), "|"});
  }
  // Make sure this doesn't overlap with the ExactMultiple test below.
  ASSERT_NE(contents.size() % MissedCrashCollector::kDefaultChunkSize, 0);
  ASSERT_NE(contents.size() % MissedCrashCollector::kDefaultChunkSize, 1);
  ASSERT_NE(contents.size() % MissedCrashCollector::kDefaultChunkSize,
            MissedCrashCollector::kDefaultChunkSize - 1);
  RunTestWithLogContents(contents);
}

// Ensure our private ReadFILEToString handles files exactly equal to
// kDefaultChunkSize in size.
TEST(MissedCrashCollectorTest, OneChunk) {
  std::string contents;
  for (int i = 0; i < MissedCrashCollector::kDefaultChunkSize; ++i) {
    base::StrAppend(&contents, {base::NumberToString(i), "|"});
  }
  contents.resize(MissedCrashCollector::kDefaultChunkSize);
  RunTestWithLogContents(contents);
}

// Ensure our private ReadFILEToString handles files whose size is a multiple of
// kDefaultChunkSize.
TEST(MissedCrashCollectorTest, ExactMultiple) {
  std::string contents;
  for (int i = 0; i < 3 * MissedCrashCollector::kDefaultChunkSize; ++i) {
    base::StrAppend(&contents, {base::NumberToString(i), "|"});
  }
  contents.resize(3 * MissedCrashCollector::kDefaultChunkSize);
  RunTestWithLogContents(contents);
}

// Ensure our private ReadFILEToString handles files whose size is a 1 less
// than a multiple of kDefaultChunkSize.
TEST(MissedCrashCollectorTest, ExactMultipleLessOne) {
  std::string contents;
  for (int i = 0; i < 3 * MissedCrashCollector::kDefaultChunkSize; ++i) {
    base::StrAppend(&contents, {base::NumberToString(i), "|"});
  }
  contents.resize(3 * MissedCrashCollector::kDefaultChunkSize - 1);
  RunTestWithLogContents(contents);
}

// Ensure our private ReadFILEToString handles files whose size is a 1 greater
// than a multiple of kDefaultChunkSize.
TEST(MissedCrashCollectorTest, ExactMultiplePlusOne) {
  std::string contents;
  for (int i = 0; i < 3 * MissedCrashCollector::kDefaultChunkSize; ++i) {
    base::StrAppend(&contents, {base::NumberToString(i), "|"});
  }
  contents.resize(3 * MissedCrashCollector::kDefaultChunkSize + 1);
  RunTestWithLogContents(contents);
}
