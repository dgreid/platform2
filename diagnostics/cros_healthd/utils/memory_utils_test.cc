// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/utils/memory_utils.h"

namespace diagnostics {

namespace {

constexpr char kRelativeMeminfoPath[] = "proc/meminfo";
constexpr char kRelativeVmStatPath[] = "proc/vmstat";

constexpr char kFakeMeminfoContents[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 kB\nMemAvailable:      "
    "87980 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedFile[] =
    "Incorrectly formatted meminfo contents.\n";
constexpr char kFakeMeminfoContentsMissingMemtotal[] =
    "MemFree:      873180 kB\nMemAvailable:      87980 kB\n";
constexpr char kFakeMeminfoContentsMissingMemfree[] =
    "MemTotal:      3906320 kB\nMemAvailable:      87980 kB\n";
constexpr char kFakeMeminfoContentsMissingMemavailable[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedMemtotal[] =
    "MemTotal:      3906320kB\nMemFree:      873180 kB\nMemAvailable:      "
    "87980 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedMemfree[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 WrongUnits\nMemAvailable: "
    "     87980 kB\n";
constexpr char kFakeMeminfoContentsIncorrectlyFormattedMemavailable[] =
    "MemTotal:      3906320 kB\nMemFree:      873180 kB\nMemAvailable:      "
    "NotAnInteger kB\n";

constexpr char kFakeVmStatContents[] = "foo 98\npgfault 654654\n";
constexpr char kFakeVmStatContentsIncorrectlyFormattedFile[] =
    "NoKey\npgfault 71023\n";
constexpr char kFakeVmStatContentsMissingPgfault[] = "foo 9908\n";
constexpr char kFakeVmStatContentsIncorrectlyFormattedPgfault[] =
    "pgfault NotAnInteger\n";

}  // namespace

// Test that memory info can be read when it exists.
TEST(MemoryUtils, TestFetchMemoryInfo) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(root_dir.Append(kRelativeVmStatPath),
                                           kFakeVmStatContents));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_memory_info());
  const auto& memory_info = memory_result->get_memory_info();
  EXPECT_EQ(memory_info->total_memory_kib, 3906320);
  EXPECT_EQ(memory_info->free_memory_kib, 873180);
  EXPECT_EQ(memory_info->available_memory_kib, 87980);
  EXPECT_EQ(memory_info->page_faults_since_last_boot, 654654);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// exist.
TEST(MemoryUtils, TestFetchMemoryInfoNoProcMeminfo) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(root_dir.Append(kRelativeVmStatPath),
                                           kFakeVmStatContents));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kFileReadError);
}

// Test that fetching memory info returns an error when /proc/meminfo is
// formatted incorrectly.
TEST(MemoryUtils, TestFetchMemoryInfoProcMeminfoFormattedIncorrectly) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedFile));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// contain the MemTotal key.
TEST(MemoryUtils, TestFetchMemoryInfoProcMeminfoNoMemTotal) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir.Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemtotal));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(root_dir.Append(kRelativeVmStatPath),
                                           kFakeVmStatContents));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// contain the MemFree key.
TEST(MemoryUtils, TestFetchMemoryInfoProcMeminfoNoMemFree) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir.Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemfree));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(root_dir.Append(kRelativeVmStatPath),
                                           kFakeVmStatContents));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo doesn't
// contain the MemAvailable key.
TEST(MemoryUtils, TestFetchMemoryInfoProcMeminfoNoMemAvailable) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(
      WriteFileAndCreateParentDirs(root_dir.Append(kRelativeMeminfoPath),
                                   kFakeMeminfoContentsMissingMemavailable));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(root_dir.Append(kRelativeVmStatPath),
                                           kFakeVmStatContents));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo contains
// an incorrectly formatted MemTotal key.
TEST(MemoryUtils, TestFetchMemoryInfoProcMeminfoIncorrectlyFormattedMemTotal) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemtotal));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(root_dir.Append(kRelativeVmStatPath),
                                           kFakeVmStatContents));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo contains
// an incorrectly formatted MemFree key.
TEST(MemoryUtils, TestFetchMemoryInfoProcMeminfoIncorrectlyFormattedMemFree) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemfree));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(root_dir.Append(kRelativeVmStatPath),
                                           kFakeVmStatContents));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/meminfo contains
// an incorrectly formatted MemAvailable key.
TEST(MemoryUtils,
     TestFetchMemoryInfoProcMeminfoIncorrectlyFormattedMemAvailable) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeMeminfoPath),
      kFakeMeminfoContentsIncorrectlyFormattedMemavailable));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(root_dir.Append(kRelativeVmStatPath),
                                           kFakeVmStatContents));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat doesn't
// exist.
TEST(MemoryUtils, TestFetchMemoryInfoNoProcVmStat) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kFileReadError);
}

// Test that fetching memory info returns an error when /proc/vmstat is
// formatted incorrectly.
TEST(MemoryUtils, TestFetchMemoryInfoProcVmStatFormattedIncorrectly) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeVmStatPath),
      kFakeVmStatContentsIncorrectlyFormattedFile));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat doesn't
// contain the pgfault key.
TEST(MemoryUtils, TestFetchMemoryInfoProcVmStatNoPgfault) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(root_dir.Append(kRelativeVmStatPath),
                                           kFakeVmStatContentsMissingPgfault));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that fetching memory info returns an error when /proc/vmstat contains
// an incorrectly formatted pgfault key.
TEST(MemoryUtils, TestFetchMemoryInfoProcVmStatIncorrectlyFormattedPgfault) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath root_dir = temp_dir.GetPath();
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeMeminfoPath), kFakeMeminfoContents));
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      root_dir.Append(kRelativeVmStatPath),
      kFakeVmStatContentsIncorrectlyFormattedPgfault));
  auto memory_result = FetchMemoryInfo(root_dir);
  ASSERT_TRUE(memory_result->is_error());
  EXPECT_EQ(memory_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

}  // namespace diagnostics
