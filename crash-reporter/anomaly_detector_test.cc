// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/anomaly_detector.h"

#include <base/files/file_path.h>
#include <base/optional.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/anomaly_detector_test_utils.h"

namespace {

using std::string_literals::operator""s;

using ::testing::_;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Return;

using ::anomaly::KernelParser;
using ::anomaly::ParserRun;
using ::anomaly::ParserTest;
using ::anomaly::SELinuxParser;
using ::anomaly::ServiceParser;
using ::anomaly::SuspendParser;
using ::anomaly::TerminaParser;

const ParserRun simple_run;
const ParserRun empty{.expected_size = 0};

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
  ServiceParser parser(true);
  ParserTest("TEST_SERVICE_FAILURE", {one, two}, &parser);
}

TEST(AnomalyDetectorTest, ServiceFailureArc) {
  ParserRun service_failure = {
      .find_this = "crash-crash"s,
      .replace_with = "arc-crash"s,
      .expected_text = "-exit2-arc-"s,
      .expected_flag = "--arc_service_failure=arc-crash"s};
  ServiceParser parser(true);
  ParserTest("TEST_SERVICE_FAILURE", {service_failure}, &parser);
}

TEST(AnomalyDetectorTest, SELinuxViolation) {
  ParserRun selinux_violation = {
      .expected_text =
          "-selinux-u:r:init:s0-u:r:kernel:s0-module_request-init-"s,
      .expected_flag = "--selinux_violation"s};
  SELinuxParser parser(true);
  ParserTest("TEST_SELINUX", {selinux_violation}, &parser);
}

TEST(AnomalyDetectorTest, SELinuxViolationPermissive) {
  ParserRun selinux_violation = {.find_this = "permissive=0",
                                 .replace_with = "permissive=1",
                                 .expected_size = 0};
  SELinuxParser parser(true);
  ParserTest("TEST_SELINUX", {selinux_violation}, &parser);
}

TEST(AnomalyDetectorTest, SuspendFailure) {
  ParserRun suspend_failure = {
      .expected_text =
          "-suspend failure: device: dummy_dev step: suspend errno: -22"s,
      .expected_flag = "--suspend_failure"s};
  ParserTest<SuspendParser>("TEST_SUSPEND_FAILURE", {suspend_failure});
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
