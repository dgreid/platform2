// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/arc_service_failure_collector.h"

#include <unistd.h>

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

using base::FilePath;

namespace {

bool s_metrics = false;

// Source tree log config file name.
const char kLogConfigFileName[] = "crash_reporter_logs.conf";

const char kTestFilename[] = "test-arc-service-failure";
const char kTestCrashDirectory[] = "test-crash-directory";

bool IsMetrics() {
  return s_metrics;
}

}  // namespace

class ArcServiceFailureCollectorMock : public ArcServiceFailureCollector {
 public:
  MOCK_METHOD(void, SetUpDBus, (), (override));
};

class ArcServiceFailureCollectorTest : public ::testing::Test {
  void SetUp() {
    s_metrics = true;

    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_.Initialize(IsMetrics, false);
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_path_ = scoped_temp_dir_.GetPath().Append(kTestFilename);
    collector_.failure_report_path_ = test_path_.value();

    test_crash_directory_ =
        scoped_temp_dir_.GetPath().Append(kTestCrashDirectory);
    CreateDirectory(test_crash_directory_);
    collector_.set_crash_directory_for_test(test_crash_directory_);
    collector_.set_log_config_path(
        test_util::GetTestDataPath(kLogConfigFileName).value());
  }

 protected:
  ArcServiceFailureCollectorMock collector_;
  base::ScopedTempDir scoped_temp_dir_;
  FilePath test_path_;
  FilePath test_crash_directory_;
};

TEST_F(ArcServiceFailureCollectorTest, CollectOK) {
  // Collector produces an ARC service failure report.
  const char kLog[] =
      "arc-crash main process (2563) terminated with status 2\n";
  ASSERT_TRUE(test_util::CreateFile(test_path_, kLog));
  EXPECT_TRUE(collector_.Collect("arc-crash"));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "arc_service_failure_arc_crash.*.meta", NULL));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "arc_service_failure_arc_crash.*.log", NULL));
  EXPECT_GT(collector_.get_bytes_written(), strlen(kLog));
}

TEST_F(ArcServiceFailureCollectorTest, FailureReportDoesNotExist) {
  // ARC service failure report file doesn't exist without a relevant log
  // message.
  EXPECT_TRUE(collector_.Collect("arc-crash"));
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
}
