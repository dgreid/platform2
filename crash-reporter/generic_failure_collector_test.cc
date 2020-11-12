// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/generic_failure_collector.h"

#include <unistd.h>

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

using base::FilePath;

namespace {

// Source tree log config file name.
const char kLogConfigFileName[] = "crash_reporter_logs.conf";

const char kTestFilename[] = "test-generic-failure";
const char kTestFailureDirectory[] = "test-failure_directory";

}  // namespace

class GenericFailureCollectorMock : public GenericFailureCollector {
 public:
  GenericFailureCollectorMock() : GenericFailureCollector() {}

  MOCK_METHOD(void, SetUpDBus, (), (override));
};

class GenericFailureCollectorTest : public ::testing::Test {
 public:
  GenericFailureCollectorTest() {}

  void SetUp() {
    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_.Initialize(false);
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_path_ = scoped_temp_dir_.GetPath().Append(kTestFilename);
    collector_.failure_report_path_ = test_path_.value();

    test_failure_directory_ =
        scoped_temp_dir_.GetPath().Append(kTestFailureDirectory);
    CreateDirectory(test_failure_directory_);
    collector_.set_crash_directory_for_test(test_failure_directory_);
    collector_.set_log_config_path(
        test_util::GetTestDataPath(kLogConfigFileName).value());
  }

 protected:
  GenericFailureCollectorMock collector_;
  base::ScopedTempDir scoped_temp_dir_;
  FilePath test_path_;
  FilePath test_failure_directory_;
};

TEST_F(GenericFailureCollectorTest, CollectOKMain) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(test_path_,
                                    "generic failure for testing purposes\n"));
  EXPECT_TRUE(collector_.Collect("generic-failure"));
  EXPECT_FALSE(IsDirectoryEmpty(test_failure_directory_));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "generic_failure.*.meta", NULL));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "generic_failure.*.log", NULL));
}

TEST_F(GenericFailureCollectorTest, SuspendExecName) {
  // Check that the suspend-failure exec name is used
  ASSERT_TRUE(test_util::CreateFile(test_path_,
                                    "suspend failure for testing purposes\n"));
  EXPECT_TRUE(collector_.Collect(GenericFailureCollector::kSuspendFailure));
  EXPECT_FALSE(IsDirectoryEmpty(test_failure_directory_));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "suspend_failure.*.meta", NULL));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "suspend_failure.*.log", NULL));
}

TEST_F(GenericFailureCollectorTest, FailureReportDoesNotExist) {
  // Generic failure report file doesn't exist.
  EXPECT_TRUE(collector_.Collect("generic-failure"));
  EXPECT_TRUE(IsDirectoryEmpty(test_failure_directory_));
}

TEST_F(GenericFailureCollectorTest, EmptyFailureReport) {
  // Generic failure report file exists, but doesn't have the expected contents.
  ASSERT_TRUE(test_util::CreateFile(test_path_, ""));
  EXPECT_TRUE(collector_.Collect("generic-failure"));
  EXPECT_TRUE(IsDirectoryEmpty(test_failure_directory_));
}

TEST_F(GenericFailureCollectorTest, CollectOKMainServiceFailure) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "crash-crash main process (2563) terminated with status 2\n"));
  EXPECT_TRUE(collector_.CollectFull("service-failure-crash-crash",
                                     GenericFailureCollector::kServiceFailure,
                                     /*weight=*/50));
  EXPECT_FALSE(IsDirectoryEmpty(test_failure_directory_));

  base::FilePath meta_path;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "service_failure_crash_crash.*.meta",
      &meta_path));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "service_failure_crash_crash.*.log", NULL));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(meta_path, &contents));
  LOG(INFO) << contents;
  EXPECT_TRUE(contents.find("upload_var_weight=50") != std::string::npos)
      << contents;
}

TEST_F(GenericFailureCollectorTest, CollectOKPreStart) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "crash-crash pre-start process (2563) terminated with status 2\n"));
  EXPECT_TRUE(collector_.CollectFull("service-failure-crash-crash",
                                     GenericFailureCollector::kServiceFailure,
                                     /*weight=*/50));
  EXPECT_FALSE(IsDirectoryEmpty(test_failure_directory_));

  base::FilePath meta_path;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "service_failure_crash_crash.*.meta",
      &meta_path));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_failure_directory_, "service_failure_crash_crash.*.log", NULL));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(meta_path, &contents));
  EXPECT_TRUE(contents.find("upload_var_weight=50") != std::string::npos)
      << contents;
}
