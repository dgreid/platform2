// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/arcvm_native_collector.h"

#include <fcntl.h>
#include <memory>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>

#include "crash-reporter/arc_util.h"
#include "crash-reporter/test_util.h"

namespace {

constexpr char kDevice[] = "Device";
constexpr char kBoard[] = "Board";
constexpr char kCpuAbi[] = "CPUABI";
constexpr char kFingerprint[] = "Fingerprint";

// 1546300800 is unixtime of 2019-01-01 00:00:00
constexpr time_t kTime = 1546300800;
constexpr pid_t kPid = 1234;
constexpr char kExecName[] = "execname";

constexpr char kTestCrashDirectory[] = "test-crash-directory";
constexpr char kBasenameWithoutExt[] = "execname.20190101.000000.1234";

constexpr char kMinidumpSampleContent[] = "*minidump*";

arc_util::BuildProperty GetBuildProperty() {
  return {.device = kDevice,
          .board = kBoard,
          .cpu_abi = kCpuAbi,
          .fingerprint = kFingerprint};
}
ArcvmNativeCollector::CrashInfo GetCrashInfo() {
  return {.time = kTime, .pid = kPid, .exec_name = kExecName};
}

}  // namespace

class TestArcvmNativeCollector : public ArcvmNativeCollector {
 public:
  explicit TestArcvmNativeCollector(const base::FilePath& crash_directory) {
    Initialize(&IsFeedbackAllowed, false /* early */);
    set_crash_directory_for_test(crash_directory);
  }
  ~TestArcvmNativeCollector() override = default;

  bool HasMetaData(const std::string& key, const std::string& value) const {
    const std::string metadata =
        base::StringPrintf("%s=%s\n", key.c_str(), value.c_str());
    return extra_metadata_.find(metadata) != std::string::npos;
  }

 private:
  static bool IsFeedbackAllowed() { return true; }
  void SetUpDBus() override {}
};

class ArcvmNativeCollectorTest : public ::testing::Test {
 public:
  ~ArcvmNativeCollectorTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());

    base::FilePath minidump_path =
        scoped_temp_dir_.GetPath().Append("minidump.dmp");
    ASSERT_TRUE(test_util::CreateFile(minidump_path, kMinidumpSampleContent));
    minidump_fd_ = HANDLE_EINTR(open(minidump_path.value().c_str(), O_RDONLY));
    ASSERT_NE(minidump_fd_, -1);

    test_crash_directory_ =
        scoped_temp_dir_.GetPath().Append(kTestCrashDirectory);
    ASSERT_TRUE(base::CreateDirectory(test_crash_directory_));
    collector_ =
        std::make_unique<TestArcvmNativeCollector>(test_crash_directory_);
  }

  void TearDown() override { close(minidump_fd_); }

 protected:
  std::unique_ptr<TestArcvmNativeCollector> collector_;
  base::ScopedTempDir scoped_temp_dir_;
  base::FilePath test_crash_directory_;
  // TODO(kimiyuki): Use base::ScopedFD. However, the reason is unknown but, if
  // simple replacing with ScopedFD causes "Bad file descriptor".
  int minidump_fd_;
};

TEST_F(ArcvmNativeCollectorTest, HandleCrash) {
  ASSERT_TRUE(collector_->HandleCrash(GetBuildProperty(), GetCrashInfo(),
                                      minidump_fd_));
  base::FilePath metadata_path =
      test_crash_directory_.Append(std::string(kBasenameWithoutExt) + ".meta");
  EXPECT_TRUE(base::PathExists(metadata_path));

  base::FilePath minidump_path =
      test_crash_directory_.Append(std::string(kBasenameWithoutExt) + ".dmp");
  std::string minidump_content;
  EXPECT_TRUE(base::ReadFileToString(minidump_path, &minidump_content));
  EXPECT_EQ(minidump_content, kMinidumpSampleContent);
}

TEST_F(ArcvmNativeCollectorTest, AddArcMetadata) {
  collector_->AddArcMetadata(GetBuildProperty(), GetCrashInfo());
  EXPECT_TRUE(collector_->HasMetaData(arc_util::kProcessField, kExecName));
  EXPECT_TRUE(
      collector_->HasMetaData(arc_util::kArcVersionField, kFingerprint));
  EXPECT_TRUE(collector_->HasMetaData(arc_util::kDeviceField, kDevice));
  EXPECT_TRUE(collector_->HasMetaData(arc_util::kBoardField, kBoard));
  EXPECT_TRUE(collector_->HasMetaData(arc_util::kCpuAbiField, kCpuAbi));
}
