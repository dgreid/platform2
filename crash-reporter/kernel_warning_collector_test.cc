// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/kernel_warning_collector.h"

#include <unistd.h>

#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

using base::FilePath;

namespace {

bool s_metrics = false;

const char kTestFilename[] = "test-kernel-warning";
const char kTestCrashDirectory[] = "test-crash-directory";

bool IsMetrics() {
  return s_metrics;
}

// Returns true if at least one file in this directory matches the pattern.
bool DirectoryHasFileWithPattern(const FilePath& directory,
                                 const std::string& pattern) {
  base::FileEnumerator enumerator(
      directory, false, base::FileEnumerator::FileType::FILES, pattern);
  FilePath path = enumerator.Next();
  return !path.empty();
}

bool DirectoryHasFileWithPatternAndContents(const FilePath& directory,
                                            const std::string& pattern,
                                            const std::string& contents) {
  base::FileEnumerator enumerator(
      directory, false, base::FileEnumerator::FileType::FILES, pattern);
  for (FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    LOG(INFO) << "Checking " << path.value();
    std::string actual_contents;
    if (!base::ReadFileToString(path, &actual_contents)) {
      LOG(ERROR) << "Failed to read file " << path.value();
      return false;
    }
    if (actual_contents.find(contents)) {
      return true;
    }
  }
  return false;
}

}  // namespace

class KernelWarningCollectorMock : public KernelWarningCollector {
 public:
  MOCK_METHOD(void, SetUpDBus, (), (override));
};

class KernelWarningCollectorTest : public ::testing::Test {
  void SetUp() {
    s_metrics = true;

    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    collector_.Initialize(IsMetrics, false);
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_path_ = scoped_temp_dir_.GetPath().Append(kTestFilename);
    collector_.warning_report_path_ = test_path_.value();

    test_crash_directory_ =
        scoped_temp_dir_.GetPath().Append(kTestCrashDirectory);
    CreateDirectory(test_crash_directory_);
    collector_.set_crash_directory_for_test(test_crash_directory_);
  }

 protected:
  KernelWarningCollectorMock collector_;
  base::ScopedTempDir scoped_temp_dir_;
  FilePath test_path_;
  FilePath test_crash_directory_;
};

TEST_F(KernelWarningCollectorTest, CollectOK) {
  // Collector produces a crash report.
  ASSERT_TRUE(
      test_util::CreateFile(test_path_,
                            "70e67541-iwl_mvm_rm_sta+0x161/0x344 [iwlmvm]()\n"
                            "\n"
                            "<remaining log contents>"));
  EXPECT_TRUE(
      collector_.Collect(KernelWarningCollector::WarningType::kGeneric));
  EXPECT_TRUE(DirectoryHasFileWithPatternAndContents(
      test_crash_directory_, "kernel_warning_iwl_mvm_rm_sta.*.meta",
      "sig=70e67541-iwl_mvm_rm_sta+0x161/0x344 [iwlmvm]()"));
}

TEST_F(KernelWarningCollectorTest, CollectOKMultiline) {
  // Collector produces a crash report.
  ASSERT_TRUE(
      test_util::CreateFile(test_path_,
                            "Warning message trigger count: 0\n"
                            "70e67541-iwl_mvm_rm_sta+0x161/0x344 [iwlmvm]()\n"
                            "\n"
                            "<remaining log contents>"));
  EXPECT_TRUE(
      collector_.Collect(KernelWarningCollector::WarningType::kGeneric));
  EXPECT_TRUE(DirectoryHasFileWithPatternAndContents(
      test_crash_directory_, "kernel_warning_iwl_mvm_rm_sta.*.meta",
      "sig=70e67541-iwl_mvm_rm_sta+0x161/0x344 [iwlmvm]()"));
}

TEST_F(KernelWarningCollectorTest, CollectOKUnknownFunc) {
  // Collector produces a crash report.
  ASSERT_TRUE(
      test_util::CreateFile(test_path_,
                            "70e67541-unknown-function+0x161/0x344 [iwlmvm]()\n"
                            "\n"
                            "<remaining log contents>"));
  EXPECT_TRUE(
      collector_.Collect(KernelWarningCollector::WarningType::kGeneric));
  EXPECT_TRUE(DirectoryHasFileWithPatternAndContents(
      test_crash_directory_, "kernel_warning_unknown_function.*.meta",
      "sig=70e67541-unknown-function+0x161/0x344 [iwlmvm]()"));
}

TEST_F(KernelWarningCollectorTest, CollectOKBadSig) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(test_path_,
                                    "70e67541-0x161/0x344 [iwlmvm]()\n"
                                    "\n"
                                    "<remaining log contents>"));
  EXPECT_TRUE(
      collector_.Collect(KernelWarningCollector::WarningType::kGeneric));
  EXPECT_TRUE(DirectoryHasFileWithPatternAndContents(
      test_crash_directory_, "kernel_warning.*.meta",
      "sig=70e67541-0x161/0x344 [iwlmvm]()"));
}

TEST_F(KernelWarningCollectorTest, CollectWifiWarningOK) {
  // Collector produces a crash report with a different exec name.
  ASSERT_TRUE(
      test_util::CreateFile(test_path_,
                            "70e67541-iwl_mvm_rm_sta+0x161/0x344 [iwlmvm]()\n"
                            "\n"
                            "<remaining log contents>"));
  EXPECT_TRUE(collector_.Collect(KernelWarningCollector::WarningType::kWifi));
  EXPECT_TRUE(DirectoryHasFileWithPattern(
      test_crash_directory_, "kernel_wifi_warning_iwl_mvm_rm_sta.*.meta"));
}

TEST_F(KernelWarningCollectorTest, FeedbackNotAllowed) {
  // Feedback not allowed.
  s_metrics = false;
  ASSERT_TRUE(
      test_util::CreateFile(test_path_,
                            "70e67541-iwl_mvm_rm_sta+0x161/0x344 [iwlmvm]()\n"
                            "\n"
                            "<remaining log contents>"));
  EXPECT_TRUE(
      collector_.Collect(KernelWarningCollector::WarningType::kGeneric));
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
}
