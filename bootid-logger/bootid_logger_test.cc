// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootid-logger/bootid_logger.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <gtest/gtest.h>

class BootidLoggerTest : public ::testing::Test {};

TEST_F(BootidLoggerTest, WriteEntry) {
  base::FilePath temporary_file;
  EXPECT_TRUE(base::CreateTemporaryFile(&temporary_file));

  const std::string kBootID = "12345678901234567890123456789012";

  const base::Time::Exploded exploded = {2020, 12, 1, 1, 0, 0, 0, 0};
  base::Time time;
  EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));

  EXPECT_TRUE(WriteBootEntry(temporary_file, kBootID, time, 100));

  const std::string expected_entry =
      "2020-12-01T00:00:00.000000+00:00 INFO boot_id: " + kBootID + "\n";
  std::string file_contents;
  EXPECT_TRUE(base::ReadFileToString(temporary_file, &file_contents));
  EXPECT_EQ(expected_entry, file_contents);
}

TEST_F(BootidLoggerTest, WriteDuplicatedEntries) {
  base::FilePath temporary_file;
  EXPECT_TRUE(base::CreateTemporaryFile(&temporary_file));

  const std::string kBootID = "12345678901234567890123456789012";

  {
    const base::Time::Exploded exploded = {2020, 12, 1, 1, 0, 0, 0, 0};
    base::Time time;
    EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
    EXPECT_TRUE(WriteBootEntry(temporary_file, kBootID, time, 100));
  }

  {
    const base::Time::Exploded exploded = {2020, 12, 2, 2, 0, 0, 0, 0};
    base::Time time;
    EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
    // Should return true, since the ID is duplicated but this is not a failure.
    EXPECT_TRUE(WriteBootEntry(temporary_file, kBootID, time, 100));
  }

  const std::string expected_entry =
      "2020-12-01T00:00:00.000000+00:00 INFO boot_id: " + kBootID + "\n";
  std::string file_contents;
  EXPECT_TRUE(base::ReadFileToString(temporary_file, &file_contents));
  EXPECT_EQ(expected_entry, file_contents);
}

TEST_F(BootidLoggerTest, WriteMultipleEntries) {
  base::FilePath temporary_file;
  EXPECT_TRUE(base::CreateTemporaryFile(&temporary_file));

  const size_t kMaxEntryNum = 3;
  const std::string kBootID1 = "12345678901234567890123456789012";
  const std::string kBootID2 = "12345678901234567890123456789013";
  const std::string kBootID3 = "12345678901234567890123456789014";
  const std::string kBootID4 = "12345678901234567890123456789015";

  {
    const base::Time::Exploded exploded = {2020, 12, 1, 1, 0, 0, 0, 0};
    base::Time time;
    EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
    EXPECT_TRUE(WriteBootEntry(temporary_file, kBootID1, time, kMaxEntryNum));
  }

  {
    const base::Time::Exploded exploded = {2020, 12, 2, 2, 0, 0, 0, 0};
    base::Time time;
    EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
    EXPECT_TRUE(WriteBootEntry(temporary_file, kBootID2, time, kMaxEntryNum));
  }

  {
    const base::Time::Exploded exploded = {2020, 12, 3, 3, 0, 0, 0, 0};
    base::Time time;
    EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
    EXPECT_TRUE(WriteBootEntry(temporary_file, kBootID3, time, kMaxEntryNum));
  }

  {
    const base::Time::Exploded exploded = {2020, 12, 4, 4, 0, 0, 0, 0};
    base::Time time;
    EXPECT_TRUE(base::Time::FromUTCExploded(exploded, &time));
    EXPECT_TRUE(WriteBootEntry(temporary_file, kBootID4, time, kMaxEntryNum));
  }

  const std::string expected_entry =
      "2020-12-02T00:00:00.000000+00:00 INFO boot_id: " + kBootID2 +
      "\n"
      "2020-12-03T00:00:00.000000+00:00 INFO boot_id: " +
      kBootID3 +
      "\n"
      "2020-12-04T00:00:00.000000+00:00 INFO boot_id: " +
      kBootID4 + "\n";
  std::string file_contents;
  EXPECT_TRUE(base::ReadFileToString(temporary_file, &file_contents));
  EXPECT_EQ(expected_entry, file_contents);
}

TEST_F(BootidLoggerTest, WriteCurrentBootEntry) {
  base::FilePath temporary_file;
  EXPECT_TRUE(base::CreateTemporaryFile(&temporary_file));
  const size_t kMaxEntryNum = 1;

  WriteCurrentBootEntry(temporary_file, kMaxEntryNum);

  std::string file_contents;
  EXPECT_TRUE(base::ReadFileToString(temporary_file, &file_contents));
  std::string boot_entry =
      base::TrimWhitespaceASCII(file_contents,
                                base::TrimPositions::TRIM_TRAILING)
          .as_string();
  EXPECT_TRUE(ValidateBootEntry(boot_entry));
  std::string boot_id = ExtractBootId(boot_entry);
  EXPECT_EQ(boot_id, GetCurrentBootId());
}
