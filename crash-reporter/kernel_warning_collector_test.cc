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

TEST_F(KernelWarningCollectorTest, CollectBad) {
  // Collector fails to collect a single line without newline
  ASSERT_TRUE(
      test_util::CreateFile(test_path_,
                            "[    0.000000] percpu: Embedded 32 pages/cpu "
                            "s91880 r8192 d31000 u131072"));
  EXPECT_FALSE(
      collector_.Collect(KernelWarningCollector::WarningType::kGeneric));
  EXPECT_TRUE(IsDirectoryEmpty(test_crash_directory_));
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

TEST_F(KernelWarningCollectorTest, CollectUMACOK) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "iwlwifi 0000:00:14.3: Microcode SW error detected. Restarting 0x0.\n"
      "iwlwifi 0000:00:14.3: Start IWL Error Log Dump:\n"
      "iwlwifi 0000:00:14.3: Status: 0x00000040, count: 6\n"
      "iwlwifi 0000:00:14.3: Loaded firmware version: 53.c31ac674.0 "
      "QuZ-a0-hr-b0-53.ucode\n"
      "iwlwifi 0000:00:14.3: 0x00000071 | NMI_INTERRUPT_UMAC_FATAL    \n"
      "iwlwifi 0000:00:14.3: 0x000022F0 | trm_hw_status0\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | trm_hw_status1\n"
      "iwlwifi 0000:00:14.3: 0x004C9C3A | branchlink2\n"
      "iwlwifi 0000:00:14.3: 0x00016176 | interruptlink1\n"
      "iwlwifi 0000:00:14.3: 0x00016176 | interruptlink2\n"
      "iwlwifi 0000:00:14.3: 0x004C496C | data1\n"
      "iwlwifi 0000:00:14.3: 0x00001000 | data2\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | data3\n"
      "iwlwifi 0000:00:14.3: 0x2D807673 | beacon time\n"
      "iwlwifi 0000:00:14.3: 0x95C4099B | tsf low\n"
      "iwlwifi 0000:00:14.3: 0x00000002 | tsf hi\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | time gp1\n"
      "iwlwifi 0000:00:14.3: 0x011EEC18 | time gp2\n"
      "iwlwifi 0000:00:14.3: 0x00000001 | uCode revision type\n"
      "iwlwifi 0000:00:14.3: 0x00000035 | uCode version major\n"
      "iwlwifi 0000:00:14.3: 0xC31AC674 | uCode version minor\n"
      "iwlwifi 0000:00:14.3: 0x00000351 | hw version\n"
      "iwlwifi 0000:00:14.3: 0x00C89004 | board version\n"
      "iwlwifi 0000:00:14.3: 0x80B1FC19 | hcmd\n"
      "iwlwifi 0000:00:14.3: 0x00020000 | isr0\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | isr1\n"
      "iwlwifi 0000:00:14.3: 0x08F00002 | isr2\n"
      "iwlwifi 0000:00:14.3: 0x04C37FCC | isr3\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | isr4\n"
      "iwlwifi 0000:00:14.3: 0x003B019C | last cmd Id\n"
      "iwlwifi 0000:00:14.3: 0x004C496C | wait_event\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | l2p_control\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | l2p_duration\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | l2p_mhvalid\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | l2p_addr_match\n"
      "iwlwifi 0000:00:14.3: 0x0000004B | lmpm_pmg_sel\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | timestamp\n"
      "iwlwifi 0000:00:14.3: 0x000050A8 | flow_handler\n"
      "iwlwifi 0000:00:14.3: Start IWL Error Log Dump:\n"
      "iwlwifi 0000:00:14.3: Status: 0x00000040, count: 7\n"
      "iwlwifi 0000:00:14.3: 0x201002FF | ADVANCED_SYSASSERT\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | umac branchlink1\n"
      "iwlwifi 0000:00:14.3: 0x80467A40 | umac branchlink2\n"
      "iwlwifi 0000:00:14.3: 0xC00866A8 | umac interruptlink1\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | umac interruptlink2\n"
      "iwlwifi 0000:00:14.3: 0x003C0102 | umac data1\n"
      "iwlwifi 0000:00:14.3: 0xDEADBEEF | umac data2\n"
      "iwlwifi 0000:00:14.3: 0xDEADBEEF | umac data3\n"
      "iwlwifi 0000:00:14.3: 0x00000035 | umac major\n"
      "iwlwifi 0000:00:14.3: 0xC31AC674 | umac minor\n"
      "iwlwifi 0000:00:14.3: 0x011EEC0D | frame pointer\n"
      "iwlwifi 0000:00:14.3: 0xC0886C40 | stack pointer\n"
      "iwlwifi 0000:00:14.3: 0x003C0102 | last host cmd\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | isr status reg\n"
      "iwlwifi 0000:00:14.3: Fseq Registers:\n"
      "iwlwifi 0000:00:14.3: 0x60000000 | FSEQ_ERROR_CODE\n"
      "iwlwifi 0000:00:14.3: 0x80290033 | FSEQ_TOP_INIT_VERSION\n"
      "iwlwifi 0000:00:14.3: 0x00090006 | FSEQ_CNVIO_INIT_VERSION\n"
      "iwlwifi 0000:00:14.3: 0x0000A481 | FSEQ_OTP_VERSION\n"
      "iwlwifi 0000:00:14.3: 0x00000003 | FSEQ_TOP_CONTENT_VERSION\n"
      "iwlwifi 0000:00:14.3: 0x4552414E | FSEQ_ALIVE_TOKEN\n"
      "iwlwifi 0000:00:14.3: 0x20000302 | FSEQ_CNVI_ID\n"
      "iwlwifi 0000:00:14.3: 0x01300504 | FSEQ_CNVR_ID\n"
      "iwlwifi 0000:00:14.3: 0x20000302 | CNVI_AUX_MISC_CHIP\n"
      "iwlwifi 0000:00:14.3: 0x01300504 | CNVR_AUX_MISC_CHIP\n"
      "iwlwifi 0000:00:14.3: 0x05B0905B | "
      "CNVR_SCU_SD_REGS_SD_REG_DIG_DCDC_VTRIM\n"
      "iwlwifi 0000:00:14.3: 0x0000025B | "
      "CNVR_SCU_SD_REGS_SD_REG_ACTIVE_VDIG_MIRROR\n"
      "iwlwifi 0000:00:14.3: Collecting data: trigger 2 fired.\n"
      "<remaining log contents>"));
  EXPECT_TRUE(
      collector_.Collect(KernelWarningCollector::WarningType::kIwlwifi));
  EXPECT_TRUE(DirectoryHasFileWithPatternAndContents(
      test_crash_directory_, "kernel_iwlwifi_error_ADVANCED_SYSASSERT.*.meta",
      "sig=iwlwifi 0000:00:14.3: 0x201002FF | ADVANCED_SYSASSERT"));
}

TEST_F(KernelWarningCollectorTest, CollectLMACOK) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "iwlwifi 0000:00:14.3: Microcode SW error detected. Restarting 0x0.\n"
      "iwlwifi 0000:00:14.3: Start IWL Error Log Dump:\n"
      "iwlwifi 0000:00:14.3: Status: 0x00000040, count: 6\n"
      "iwlwifi 0000:00:14.3: Loaded firmware version: 53.c31ac674.0 "
      "QuZ-a0-hr-b0-53.ucode\n"
      "iwlwifi 0000:00:14.3: 0x00000084 | NMI_INTERRUPT_UNKNOWN       \n"
      "iwlwifi 0000:00:14.3: 0x000022F0 | trm_hw_status0\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | trm_hw_status1\n"
      "iwlwifi 0000:00:14.3: 0x004C9C3A | branchlink2\n"
      "iwlwifi 0000:00:14.3: 0x0000890E | interruptlink1\n"
      "iwlwifi 0000:00:14.3: 0x0000890E | interruptlink2\n"
      "iwlwifi 0000:00:14.3: 0x004C492A | data1\n"
      "iwlwifi 0000:00:14.3: 0xFF000000 | data2\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | data3\n"
      "iwlwifi 0000:00:14.3: 0xB180600C | beacon time\n"
      "iwlwifi 0000:00:14.3: 0x94A49FFF | tsf low\n"
      "iwlwifi 0000:00:14.3: 0x00000002 | tsf hi\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | time gp1\n"
      "iwlwifi 0000:00:14.3: 0x10D5DCA1 | time gp2\n"
      "iwlwifi 0000:00:14.3: 0x00000001 | uCode revision type\n"
      "iwlwifi 0000:00:14.3: 0x00000035 | uCode version major\n"
      "iwlwifi 0000:00:14.3: 0xC31AC674 | uCode version minor\n"
      "iwlwifi 0000:00:14.3: 0x00000351 | hw version\n"
      "iwlwifi 0000:00:14.3: 0x00C89004 | board version\n"
      "iwlwifi 0000:00:14.3: 0x80F3FC19 | hcmd\n"
      "iwlwifi 0000:00:14.3: 0x00020000 | isr0\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | isr1\n"
      "iwlwifi 0000:00:14.3: 0x08F04002 | isr2\n"
      "iwlwifi 0000:00:14.3: 0x04C01FCC | isr3\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | isr4\n"
      "iwlwifi 0000:00:14.3: 0x00E4019C | last cmd Id\n"
      "iwlwifi 0000:00:14.3: 0x004C492A | wait_event\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | l2p_control\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | l2p_duration\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | l2p_mhvalid\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | l2p_addr_match\n"
      "iwlwifi 0000:00:14.3: 0x00000048 | lmpm_pmg_sel\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | timestamp\n"
      "iwlwifi 0000:00:14.3: 0x0000A8B8 | flow_handler\n"
      "iwlwifi 0000:00:14.3: Start IWL Error Log Dump:\n"
      "iwlwifi 0000:00:14.3: Status: 0x00000040, count: 7\n"
      "iwlwifi 0000:00:14.3: 0x20000066 | NMI_INTERRUPT_HOST\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | umac branchlink1\n"
      "iwlwifi 0000:00:14.3: 0x80467A40 | umac branchlink2\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | umac interruptlink1\n"
      "iwlwifi 0000:00:14.3: 0x80475DFC | umac interruptlink2\n"
      "iwlwifi 0000:00:14.3: 0x01000000 | umac data1\n"
      "iwlwifi 0000:00:14.3: 0x80475DFC | umac data2\n"
      "iwlwifi 0000:00:14.3: 0x00000000 | umac data3\n"
      "iwlwifi 0000:00:14.3: 0x00000035 | umac major\n"
      "iwlwifi 0000:00:14.3: 0xC31AC674 | umac minor\n"
      "iwlwifi 0000:00:14.3: 0x10D5DCA0 | frame pointer\n"
      "iwlwifi 0000:00:14.3: 0xC088621C | stack pointer\n"
      "iwlwifi 0000:00:14.3: 0x00E60400 | last host cmd\n"
      "iwlwifi 0000:00:14.3: 0x00000009 | isr status reg\n"
      "iwlwifi 0000:00:14.3: Fseq Registers:\n"
      "iwlwifi 0000:00:14.3: 0x60000000 | FSEQ_ERROR_CODE\n"
      "iwlwifi 0000:00:14.3: 0x80290033 | FSEQ_TOP_INIT_VERSION\n"
      "iwlwifi 0000:00:14.3: 0x00090006 | FSEQ_CNVIO_INIT_VERSION\n"
      "iwlwifi 0000:00:14.3: 0x0000A481 | FSEQ_OTP_VERSION\n"
      "iwlwifi 0000:00:14.3: 0x00000003 | FSEQ_TOP_CONTENT_VERSION\n"
      "iwlwifi 0000:00:14.3: 0x4552414E | FSEQ_ALIVE_TOKEN\n"
      "iwlwifi 0000:00:14.3: 0x20000302 | FSEQ_CNVI_ID\n"
      "iwlwifi 0000:00:14.3: 0x01300504 | FSEQ_CNVR_ID\n"
      "iwlwifi 0000:00:14.3: 0x20000302 | CNVI_AUX_MISC_CHIP\n"
      "iwlwifi 0000:00:14.3: 0x01300504 | CNVR_AUX_MISC_CHIP\n"
      "iwlwifi 0000:00:14.3: 0x05B0905B | "
      "CNVR_SCU_SD_REGS_SD_REG_DIG_DCDC_VTRIM\n"
      "iwlwifi 0000:00:14.3: 0x0000025B | "
      "CNVR_SCU_SD_REGS_SD_REG_ACTIVE_VDIG_MIRROR\n"
      "<remaining log contents>"));
  EXPECT_TRUE(
      collector_.Collect(KernelWarningCollector::WarningType::kIwlwifi));
  EXPECT_TRUE(DirectoryHasFileWithPatternAndContents(
      test_crash_directory_,
      "kernel_iwlwifi_error_NMI_INTERRUPT_UNKNOWN.*.meta",
      "sig=iwlwifi 0000:00:14.3: 0x00000084 | NMI_INTERRUPT_UNKNOWN       "));
}

TEST_F(KernelWarningCollectorTest, CollectOKBadIwlwifiSig) {
  // Collector produces a crash report.
  ASSERT_TRUE(test_util::CreateFile(
      test_path_,
      "iwlwifi 0000:01:00.0: Microcode SW error detected. Restarting 0x0.\n"
      "iwlwifi 0000:01:00.0: Start IWL Error Log Dump:\n"
      "iwlwifi 0000:01:00.0: Status: 0x00000100, count: 6\n"
      "iwlwifi 0000:01:00.0: Loaded firmware version: 43.95eb4e97.0\n"
      "iwlwifi 0000:01:00.0: 0x00000071 | BAD_COMMAND\n"
      "iwlwifi 0000:01:00.0: 0x000022F0 | trm_hw_status0\n"
      "iwlwifi 0000:01:00.0: 0x00000000 | trm_hw_status1\n"
      "iwlwifi 0000:01:00.0: 0x0000C860 | flow_handler\n"
      "iwlwifi 0000:01:00.0: Start IWL Error Log Dump:\n"
      "iwlwifi 0000:01:00.0: Status: 0x00000100, count: 7\n"
      "iwlwifi 0000:01:00.0: 0x20000079 | \n"
      "iwlwifi 0000:01:00.0: 0x00000000 | umac branchlink1\n"
      "<remaining log contents>"));
  EXPECT_TRUE(
      collector_.Collect(KernelWarningCollector::WarningType::kIwlwifi));
  EXPECT_TRUE(DirectoryHasFileWithPatternAndContents(
      test_crash_directory_, "kernel_iwlwifi_error.*.meta",
      "sig=iwlwifi 0000:01:00.0: Microcode SW error detected. Restarting "
      "0x0."));
}
