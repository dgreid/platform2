// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/anomaly_detector.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/test_util.h"

namespace {

using std::string_literals::operator""s;

using ::testing::_;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::SizeIs;

using anomaly::CrashReporterParser;
using anomaly::KernelParser;
using anomaly::SELinuxParser;
using anomaly::ServiceParser;
using anomaly::SuspendParser;
using anomaly::TerminaParser;
using test_util::AdvancingClock;

std::vector<std::string> GetTestLogMessages(base::FilePath input_file) {
  std::string contents;
  // Though ASSERT would be better here, we need to use EXPECT since functions
  // calling ASSERT must return void.
  EXPECT_TRUE(base::ReadFileToString(input_file, &contents));
  auto log_msgs = base::SplitString(contents, "\n", base::KEEP_WHITESPACE,
                                    base::SPLIT_WANT_ALL);
  EXPECT_GT(log_msgs.size(), 0);
  // Handle likely newline at end of file.
  if (log_msgs.back().empty())
    log_msgs.pop_back();
  return log_msgs;
}

void ReplaceMsgContent(std::vector<std::string>* log_msgs,
                       const std::string& find_this,
                       const std::string& replace_with) {
  for (auto& msg : *log_msgs)
    base::ReplaceSubstringsAfterOffset(&msg, 0, find_this, replace_with);
}

std::vector<anomaly::CrashReport> ParseLogMessages(
    anomaly::Parser* parser, const std::vector<std::string>& log_msgs) {
  std::vector<anomaly::CrashReport> crash_reports;
  for (auto& msg : log_msgs) {
    auto crash_report = parser->ParseLogEntry(msg);
    if (crash_report)
      crash_reports.push_back(std::move(*crash_report));
  }
  return crash_reports;
}

using base::nullopt;

struct ParserRun {
  base::Optional<std::string> find_this = nullopt;
  base::Optional<std::string> replace_with = nullopt;
  base::Optional<std::string> expected_text = nullopt;
  base::Optional<std::string> expected_flag = nullopt;
  size_t expected_size = 1;
};

const ParserRun simple_run;
const ParserRun empty{.expected_size = 0};

void ParserTest(const std::string& input_file_name,
                std::initializer_list<ParserRun> parser_runs,
                anomaly::Parser* parser) {
  auto log_msgs =
      GetTestLogMessages(test_util::GetTestDataPath(input_file_name));
  for (auto& run : parser_runs) {
    if (run.find_this && run.replace_with)
      ReplaceMsgContent(&log_msgs, *run.find_this, *run.replace_with);
    auto crash_reports = ParseLogMessages(parser, log_msgs);

    ASSERT_THAT(crash_reports, SizeIs(run.expected_size));
    if (run.expected_text)
      EXPECT_THAT(crash_reports[0].text, HasSubstr(*run.expected_text));
    if (run.expected_flag)
      EXPECT_EQ(crash_reports[0].flag, *run.expected_flag);
  }
}

template <class T>
void ParserTest(const std::string& input_file_name,
                std::initializer_list<ParserRun> parser_runs) {
  T parser;
  ParserTest(input_file_name, parser_runs, &parser);
}

// Call enough CrashReporterParser::PeriodicUpdate enough times that
// AdvancingClock advances at least CrashReporterParser::kTimeout.
void RunCrashReporterPeriodicUpdate(CrashReporterParser* parser) {
  // AdvancingClock advances 10 seconds per call. The "times 2" is to make sure
  // we get well past the timeout.
  const int kTimesToRun = 2 * CrashReporterParser::kTimeout.InSeconds() / 10;
  for (int count = 0; count < kTimesToRun; ++count) {
    parser->PeriodicUpdate();
  }
}
}  // namespace

TEST(AnomalyDetectorTest, KernelWarning) {
  ParserRun second{
      .find_this = "ttm_bo_vm.c"s,
      .replace_with = "file_one.c"s,
      .expected_text =
          "0x19e/0x1ab [ttm]()\n[ 3955.309298] Modules linked in"s};
  ParserTest<KernelParser>("TEST_WARNING", {simple_run, second});
}

TEST(AnomalyDetectorTest, KernelWarningNoDuplicate) {
  ParserRun identical_warning{.expected_size = 0};
  ParserTest<KernelParser>("TEST_WARNING", {simple_run, identical_warning});
}

TEST(AnomalyDetectorTest, KernelWarningHeader) {
  ParserRun warning_message{.expected_text = "Test Warning message asdfghjkl"s};
  ParserTest<KernelParser>("TEST_WARNING_HEADER", {warning_message});
}

TEST(AnomalyDetectorTest, KernelWarningOld) {
  ParserTest<KernelParser>("TEST_WARNING_OLD", {simple_run});
}

TEST(AnomalyDetectorTest, KernelWarningOldARM64) {
  ParserRun unknown_function{.expected_text = "-unknown-function\n"s};
  ParserTest<KernelParser>("TEST_WARNING_OLD_ARM64", {unknown_function});
}

TEST(AnomalyDetectorTest, KernelWarningWifi) {
  ParserRun wifi_warning = {.find_this = "gpu/drm/ttm"s,
                            .replace_with = "net/wireless"s,
                            .expected_flag = "--kernel_wifi_warning"s};
  ParserTest<KernelParser>("TEST_WARNING", {wifi_warning});
}

TEST(AnomalyDetectorTest, KernelWarningSuspend) {
  ParserRun suspend_warning = {.find_this = "gpu/drm/ttm"s,
                               .replace_with = "idle"s,
                               .expected_flag = "--kernel_suspend_warning"s};
  ParserTest<KernelParser>("TEST_WARNING", {suspend_warning});
}

TEST(AnomalyDetectorTest, CrashReporterCrash) {
  ParserRun crash_reporter_crash = {.expected_flag =
                                        "--crash_reporter_crashed"s};
  ParserTest<KernelParser>("TEST_CR_CRASH", {crash_reporter_crash});
}

TEST(AnomalyDetectorTest, CrashReporterCrashRateLimit) {
  ParserRun crash_reporter_crash = {.expected_flag =
                                        "--crash_reporter_crashed"s};
  ParserTest<KernelParser>("TEST_CR_CRASH",
                           {crash_reporter_crash, empty, empty});
}

TEST(AnomalyDetectorTest, ServiceFailure) {
  ParserRun one{.expected_text = "-exit2-"s};
  ParserRun two{.find_this = "crash-crash"s, .replace_with = "fresh-fresh"s};
  ParserTest<ServiceParser>("TEST_SERVICE_FAILURE", {one, two});
}

TEST(AnomalyDetectorTest, ServiceFailureArc) {
  ParserRun service_failure = {
      .find_this = "crash-crash"s,
      .replace_with = "arc-crash"s,
      .expected_text = "-exit2-arc-"s,
      .expected_flag = "--arc_service_failure=arc-crash"s};
  ParserTest<ServiceParser>("TEST_SERVICE_FAILURE", {service_failure});
}

TEST(AnomalyDetectorTest, SELinuxViolation) {
  ParserRun selinux_violation = {
      .expected_text =
          "-selinux-u:r:init:s0-u:r:kernel:s0-module_request-init-"s,
      .expected_flag = "--selinux_violation"s};
  ParserTest<SELinuxParser>("TEST_SELINUX", {selinux_violation});
}

TEST(AnomalyDetectorTest, SELinuxViolationPermissive) {
  ParserRun selinux_violation = {.find_this = "permissive=0",
                                 .replace_with = "permissive=1",
                                 .expected_size = 0};
  ParserTest<SELinuxParser>("TEST_SELINUX", {selinux_violation});
}

TEST(AnomalyDetectorTest, SuspendFailure) {
  ParserRun suspend_failure = {
      .expected_text =
          "-suspend failure: device: dummy_dev step: suspend errno: -22"s,
      .expected_flag = "--suspend_failure"s};
  ParserTest<SuspendParser>("TEST_SUSPEND_FAILURE", {suspend_failure});
}

TEST(CrashReporterParserTest, MatchedCrashTest) {
  auto metrics = std::make_unique<MetricsLibraryMock>();
  EXPECT_CALL(*metrics, SendCrosEventToUMA("Crash.Chrome.CrashesFromKernel"))
      .WillOnce(Return(true));
  EXPECT_CALL(*metrics, Init()).Times(1);
  CrashReporterParser parser(std::make_unique<AdvancingClock>(),
                             std::move(metrics));
  ParserTest("TEST_CHROME_CRASH_MATCH.txt", {empty}, &parser);

  // Calling PeriodicUpdate should not send new Cros events to UMA.
  RunCrashReporterPeriodicUpdate(&parser);
}

TEST(CrashReporterParserTest, ReverseMatchedCrashTest) {
  auto metrics = std::make_unique<MetricsLibraryMock>();
  EXPECT_CALL(*metrics, SendCrosEventToUMA("Crash.Chrome.CrashesFromKernel"))
      .WillOnce(Return(true));
  EXPECT_CALL(*metrics, Init()).Times(1);
  CrashReporterParser parser(std::make_unique<AdvancingClock>(),
                             std::move(metrics));
  ParserTest("TEST_CHROME_CRASH_MATCH_REVERSED.txt", {empty}, &parser);
  RunCrashReporterPeriodicUpdate(&parser);
}

TEST(CrashReporterParserTest, UnmatchedCallFromChromeTest) {
  auto metrics = std::make_unique<MetricsLibraryMock>();
  EXPECT_CALL(*metrics, SendCrosEventToUMA(_)).Times(0);
  EXPECT_CALL(*metrics, Init()).Times(1);
  CrashReporterParser parser(std::make_unique<AdvancingClock>(),
                             std::move(metrics));
  ParserRun no_kernel_call = empty;
  no_kernel_call.find_this = std::string(
      "Received crash notification for chrome[1570] sig 11, user 1000 group "
      "1000 (ignoring call by kernel - chrome crash");
  no_kernel_call.replace_with = std::string(
      "[user] Received crash notification for btdispatch[2734] sig 6, user 218 "
      "group 218");
  ParserTest("TEST_CHROME_CRASH_MATCH.txt", {no_kernel_call}, &parser);
  RunCrashReporterPeriodicUpdate(&parser);
}

TEST(CrashReporterParserTest, UnmatchedCallFromKernelTest) {
  auto metrics = std::make_unique<MetricsLibraryMock>();
  EXPECT_CALL(*metrics, SendCrosEventToUMA("Crash.Chrome.CrashesFromKernel"))
      .WillOnce(Return(true));
  EXPECT_CALL(*metrics, SendCrosEventToUMA("Crash.Chrome.MissedCrashes"))
      .WillOnce(Return(true));
  EXPECT_CALL(*metrics, Init()).Times(1);
  CrashReporterParser parser(std::make_unique<AdvancingClock>(),
                             std::move(metrics));
  ParserRun no_direct_call = empty;
  no_direct_call.find_this = std::string(
      "Received crash notification for chrome[1570] user 1000 (called "
      "directly)");
  no_direct_call.replace_with = std::string(
      "[user] Received crash notification for btdispatch[2734] sig 6, user 218 "
      "group 218");
  ParserTest("TEST_CHROME_CRASH_MATCH.txt", {no_direct_call}, &parser);
  RunCrashReporterPeriodicUpdate(&parser);
}

TEST(CrashReporterParserTest, InterleavedMessagesTest) {
  auto log_msgs = GetTestLogMessages(
      test_util::GetTestDataPath("TEST_CHROME_CRASH_MATCH_INTERLEAVED.txt"));
  std::sort(log_msgs.begin(), log_msgs.end());
  do {
    auto metrics = std::make_unique<MetricsLibraryMock>();
    EXPECT_CALL(*metrics, SendCrosEventToUMA("Crash.Chrome.CrashesFromKernel"))
        .Times(3)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*metrics, Init()).Times(1);
    CrashReporterParser parser(std::make_unique<AdvancingClock>(),
                               std::move(metrics));
    auto crash_reports = ParseLogMessages(&parser, log_msgs);
    EXPECT_THAT(crash_reports, IsEmpty()) << " for message set:\n"
                                          << base::JoinString(log_msgs, "\n");
    RunCrashReporterPeriodicUpdate(&parser);
  } while (std::next_permutation(log_msgs.begin(), log_msgs.end()));
}

TEST(CrashReporterParserTest, InterleavedMismatchedMessagesTest) {
  auto log_msgs = GetTestLogMessages(
      test_util::GetTestDataPath("TEST_CHROME_CRASH_MATCH_INTERLEAVED.txt"));

  ReplaceMsgContent(&log_msgs,
                    "Received crash notification for chrome[1570] user 1000 "
                    "(called directly)",
                    "Received crash notification for chrome[1571] user 1000 "
                    "(called directly)");
  std::sort(log_msgs.begin(), log_msgs.end());
  do {
    auto metrics = std::make_unique<MetricsLibraryMock>();
    EXPECT_CALL(*metrics, SendCrosEventToUMA("Crash.Chrome.CrashesFromKernel"))
        .Times(3)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*metrics, SendCrosEventToUMA("Crash.Chrome.MissedCrashes"))
        .WillOnce(Return(true));
    EXPECT_CALL(*metrics, Init()).Times(1);
    CrashReporterParser parser(std::make_unique<AdvancingClock>(),
                               std::move(metrics));
    auto crash_reports = ParseLogMessages(&parser, log_msgs);
    EXPECT_THAT(crash_reports, IsEmpty()) << " for message set:\n"
                                          << base::JoinString(log_msgs, "\n");
    RunCrashReporterPeriodicUpdate(&parser);
  } while (std::next_permutation(log_msgs.begin(), log_msgs.end()));
}

MATCHER_P2(SignalEq, interface, member, "") {
  return (arg->GetInterface() == interface && arg->GetMember() == member);
}

TEST(AnomalyDetectorTest, BTRFSExtentCorruption) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::MockBus> bus = new dbus::MockBus(options);

  auto obj_path = dbus::ObjectPath(anomaly_detector::kAnomalyEventServicePath);
  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus.get(), obj_path);

  EXPECT_CALL(*bus, GetExportedObject(Eq(obj_path)))
      .WillOnce(Return(exported_object.get()));
  EXPECT_CALL(*exported_object,
              SendSignal(SignalEq(
                  anomaly_detector::kAnomalyEventServiceInterface,
                  anomaly_detector::kAnomalyGuestFileCorruptionSignalName)))
      .Times(1);

  TerminaParser parser(bus);

  parser.ParseLogEntry(
      "VM(3)",
      "BTRFS warning (device vdb): csum failed root 5 ino 257 off 409600 csum "
      "0x76ad9387 expected csum 0xd8d34542 mirror 1");
}

TEST(AnomalyDetectorTest, BTRFSTreeCorruption) {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::MockBus> bus = new dbus::MockBus(options);

  auto obj_path = dbus::ObjectPath(anomaly_detector::kAnomalyEventServicePath);
  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus.get(), obj_path);

  EXPECT_CALL(*bus, GetExportedObject(Eq(obj_path)))
      .WillOnce(Return(exported_object.get()));
  EXPECT_CALL(*exported_object,
              SendSignal(SignalEq(
                  anomaly_detector::kAnomalyEventServiceInterface,
                  anomaly_detector::kAnomalyGuestFileCorruptionSignalName)))
      .Times(1);

  TerminaParser parser(bus);

  parser.ParseLogEntry("VM(3)",
                       "BTRFS warning (device vdb): vdb checksum verify failed "
                       "on 122798080 wanted 4E5B4C99 found 5F261FEB level 0");
}
