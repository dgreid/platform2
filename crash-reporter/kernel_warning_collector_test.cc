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
  EXPECT_TRUE(DirectoryHasFileWithPattern(
      test_crash_directory_, "kernel_warning_iwl_mvm_rm_sta.*.meta"));
}

TEST_F(KernelWarningCollectorTest, CollectOKBadSig) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(test_path_,
                                    "70e67541-0x161/0x344 [iwlmvm]()\n"
                                    "\n"
                                    "<remaining log contents>"));
  EXPECT_TRUE(
      collector_.Collect(KernelWarningCollector::WarningType::kGeneric));
  EXPECT_TRUE(DirectoryHasFileWithPattern(test_crash_directory_,
                                          "kernel_warning.*.meta"));
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
