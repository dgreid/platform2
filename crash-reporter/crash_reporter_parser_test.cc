// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_reporter_parser.h"

#include <algorithm>
#include <utility>

#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/anomaly_detector_test_utils.h"
#include "crash-reporter/test_util.h"

using ::testing::_;
using ::testing::IsEmpty;
using ::testing::Return;

using ::anomaly::CrashReporterParser;
using ::anomaly::GetTestLogMessages;
using ::anomaly::ParserRun;
using ::anomaly::ParserTest;
using ::anomaly::ReplaceMsgContent;
using ::test_util::AdvancingClock;

namespace {

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

const ParserRun empty{.expected_size = 0};

}  // namespace

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
