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

TEST(AnomalyDetectorTest, KernelIwlwifiErrorLmacUmac) {
  ParserRun wifi_error = {
      .expected_text =
          "[15883.337270] iwlwifi 0000:00:0c.0: Microcode SW error detected. "
          "Restarting 0x0.\n"
          "[15883.337346] iwlwifi 0000:00:0c.0: Start IWL Error Log Dump:\n"
          "[15883.337349] iwlwifi 0000:00:0c.0: Status: 0x00000100, count: 6\n"
          "[15883.337352] iwlwifi 0000:00:0c.0: Loaded firmware version: "
          "46.b20aefee.0\n"
          "[15883.337355] iwlwifi 0000:00:0c.0: 0x00000084 | "
          "NMI_INTERRUPT_UNKNOWN\n"
          "[15883.337357] iwlwifi 0000:00:0c.0: 0x000022F0 | trm_hw_status0\n"
          "[15883.337359] iwlwifi 0000:00:0c.0: 0x00000000 | trm_hw_status1\n"
          "[15883.337362] iwlwifi 0000:00:0c.0: 0x0048751E | branchlink2\n"
          "[15883.337364] iwlwifi 0000:00:0c.0: 0x00479236 | interruptlink1\n"
          "[15883.337366] iwlwifi 0000:00:0c.0: 0x0000AE00 | interruptlink2\n"
          "[15883.337369] iwlwifi 0000:00:0c.0: 0x0001A2D6 | data1\n"
          "[15883.337371] iwlwifi 0000:00:0c.0: 0xFF000000 | data2\n"
          "[15883.337373] iwlwifi 0000:00:0c.0: 0xF0000000 | data3\n"
          "[15883.337376] iwlwifi 0000:00:0c.0: 0x00000000 | beacon time\n"
          "[15883.337378] iwlwifi 0000:00:0c.0: 0x158DE6F7 | tsf low\n"
          "[15883.337380] iwlwifi 0000:00:0c.0: 0x00000000 | tsf hi\n"
          "[15883.337383] iwlwifi 0000:00:0c.0: 0x00000000 | time gp1\n"
          "[15883.337385] iwlwifi 0000:00:0c.0: 0x158DE6F9 | time gp2\n"
          "[15883.337388] iwlwifi 0000:00:0c.0: 0x00000001 | uCode revision "
          "type\n"
          "[15883.337390] iwlwifi 0000:00:0c.0: 0x0000002E | uCode version "
          "major\n"
          "[15883.337392] iwlwifi 0000:00:0c.0: 0xB20AEFEE | uCode version "
          "minor\n"
          "[15883.337394] iwlwifi 0000:00:0c.0: 0x00000312 | hw version\n"
          "[15883.337397] iwlwifi 0000:00:0c.0: 0x00C89008 | board version\n"
          "[15883.337399] iwlwifi 0000:00:0c.0: 0x007B019C | hcmd\n"
          "[15883.337401] iwlwifi 0000:00:0c.0: 0x00022000 | isr0\n"
          "[15883.337404] iwlwifi 0000:00:0c.0: 0x00000000 | isr1\n"
          "[15883.337406] iwlwifi 0000:00:0c.0: 0x08001802 | isr2\n"
          "[15883.337408] iwlwifi 0000:00:0c.0: 0x40400180 | isr3\n"
          "[15883.337411] iwlwifi 0000:00:0c.0: 0x00000000 | isr4\n"
          "[15883.337413] iwlwifi 0000:00:0c.0: 0x007B019C | last cmd Id\n"
          "[15883.337415] iwlwifi 0000:00:0c.0: 0x0001A2D6 | wait_event\n"
          "[15883.337417] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_control\n"
          "[15883.337420] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_duration\n"
          "[15883.337422] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_mhvalid\n"
          "[15883.337424] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_addr_match\n"
          "[15883.337427] iwlwifi 0000:00:0c.0: 0x0000008F | lmpm_pmg_sel\n"
          "[15883.337429] iwlwifi 0000:00:0c.0: 0x24021230 | timestamp\n"
          "[15883.337432] iwlwifi 0000:00:0c.0: 0x0000B0D8 | flow_handler\n"
          "[15883.337464] iwlwifi 0000:00:0c.0: Start IWL Error Log Dump:\n"
          "[15883.337467] iwlwifi 0000:00:0c.0: Status: 0x00000100, count: 7\n"
          "[15883.337470] iwlwifi 0000:00:0c.0: 0x20000066 | "
          "NMI_INTERRUPT_HOST\n"
          "[15883.337472] iwlwifi 0000:00:0c.0: 0x00000000 | umac branchlink1\n"
          "[15883.337475] iwlwifi 0000:00:0c.0: 0xC008821A | umac branchlink2\n"
          "[15883.337477] iwlwifi 0000:00:0c.0: 0x00000000 | umac "
          "interruptlink1\n"
          "[15883.337479] iwlwifi 0000:00:0c.0: 0x8044FBD2 | umac "
          "interruptlink2\n"
          "[15883.337481] iwlwifi 0000:00:0c.0: 0x01000000 | umac data1\n"
          "[15883.337484] iwlwifi 0000:00:0c.0: 0x8044FBD2 | umac data2\n"
          "[15883.337486] iwlwifi 0000:00:0c.0: 0xDEADBEEF | umac data3\n"
          "[15883.337488] iwlwifi 0000:00:0c.0: 0x0000002E | umac major\n"
          "[15883.337490] iwlwifi 0000:00:0c.0: 0xB20AEFEE | umac minor\n"
          "[15883.337493] iwlwifi 0000:00:0c.0: 0x158DE6F4 | frame pointer\n"
          "[15883.337511] iwlwifi 0000:00:0c.0: 0xC088627C | stack pointer\n"
          "[15883.337514] iwlwifi 0000:00:0c.0: 0x007B019C | last host cmd\n"
          "[15883.337516] iwlwifi 0000:00:0c.0: 0x00000000 | isr status reg\n",
      .expected_flags = {{"--kernel_iwlwifi_error"}}};
  ParserTest<KernelParser>("TEST_IWLWIFI_LMAC_UMAC", {wifi_error});
}

TEST(AnomalyDetectorTest, KernelIwlwifiErrorLmacTwoSpace) {
  ParserRun wifi_error = {
      .expected_text =
          "[79553.430686] iwlwifi 0000:02:00.0: Microcode SW error detected.  "
          "Restarting 0x2000000.\n"
          "[79553.430913] iwlwifi 0000:02:00.0: Start IWL Error Log Dump:\n"
          "[79553.430919] iwlwifi 0000:02:00.0: Status: 0x00000040, count: 6\n"
          "[79553.430924] iwlwifi 0000:02:00.0: Loaded firmware version: "
          "29.116a852a.0 7265D-29.ucode\n"
          "[79553.430930] iwlwifi 0000:02:00.0: 0x00000084 | "
          "NMI_INTERRUPT_UNKNOWN       \n"
          "[79553.430935] iwlwifi 0000:02:00.0: 0x00A002F0 | trm_hw_status0\n"
          "[79553.430939] iwlwifi 0000:02:00.0: 0x00000000 | trm_hw_status1\n"
          "[79553.430944] iwlwifi 0000:02:00.0: 0x00043D6C | branchlink2\n"
          "[79553.430948] iwlwifi 0000:02:00.0: 0x0004AFD6 | interruptlink1\n"
          "[79553.430953] iwlwifi 0000:02:00.0: 0x0004AFD6 | interruptlink2\n"
          "[79553.430957] iwlwifi 0000:02:00.0: 0x00000000 | data1\n"
          "[79553.430961] iwlwifi 0000:02:00.0: 0x00000080 | data2\n"
          "[79553.430966] iwlwifi 0000:02:00.0: 0x07230000 | data3\n"
          "[79553.430970] iwlwifi 0000:02:00.0: 0x1E00B95C | beacon time\n"
          "[79553.430975] iwlwifi 0000:02:00.0: 0xE6A38917 | tsf low\n"
          "[79553.430979] iwlwifi 0000:02:00.0: 0x00000011 | tsf hi\n"
          "[79553.430983] iwlwifi 0000:02:00.0: 0x00000000 | time gp1\n"
          "[79553.430988] iwlwifi 0000:02:00.0: 0x8540E3A4 | time gp2\n"
          "[79553.430992] iwlwifi 0000:02:00.0: 0x00000001 | uCode revision "
          "type\n"
          "[79553.430996] iwlwifi 0000:02:00.0: 0x0000001D | uCode version "
          "major\n"
          "[79553.431013] iwlwifi 0000:02:00.0: 0x116A852A | uCode version "
          "minor\n"
          "[79553.431017] iwlwifi 0000:02:00.0: 0x00000210 | hw version\n"
          "[79553.431021] iwlwifi 0000:02:00.0: 0x00489200 | board version\n"
          "[79553.431025] iwlwifi 0000:02:00.0: 0x0000001C | hcmd\n"
          "[79553.431030] iwlwifi 0000:02:00.0: 0x00022000 | isr0\n"
          "[79553.431034] iwlwifi 0000:02:00.0: 0x00000000 | isr1\n"
          "[79553.431039] iwlwifi 0000:02:00.0: 0x0000000A | isr2\n"
          "[79553.431043] iwlwifi 0000:02:00.0: 0x0041D4C0 | isr3\n"
          "[79553.431047] iwlwifi 0000:02:00.0: 0x00000000 | isr4\n"
          "[79553.431052] iwlwifi 0000:02:00.0: 0x00230151 | last cmd Id\n"
          "[79553.431056] iwlwifi 0000:02:00.0: 0x00000000 | wait_event\n"
          "[79553.431060] iwlwifi 0000:02:00.0: 0x0000A8CB | l2p_control\n"
          "[79553.431082] iwlwifi 0000:02:00.0: 0x00000020 | l2p_duration\n"
          "[79553.431086] iwlwifi 0000:02:00.0: 0x00000003 | l2p_mhvalid\n"
          "[79553.431091] iwlwifi 0000:02:00.0: 0x000000CE | l2p_addr_match\n"
          "[79553.431095] iwlwifi 0000:02:00.0: 0x00000005 | lmpm_pmg_sel\n"
          "[79553.431100] iwlwifi 0000:02:00.0: 0x07071159 | timestamp\n"
          "[79553.431104] iwlwifi 0000:02:00.0: 0x00340010 | flow_handler\n",
      .expected_flags = {{"--kernel_iwlwifi_error"}}};
  ParserTest<KernelParser>("TEST_IWLWIFI_LMAC_TWO_SPACE", {wifi_error});
}

TEST(AnomalyDetectorTest, KernelIwlwifiErrorLmac) {
  ParserRun wifi_error = {
      .expected_text =
          "[15883.337270] iwlwifi 0000:00:0c.0: Microcode SW error detected. "
          "Restarting 0x0.\n"
          "[15883.337346] iwlwifi 0000:00:0c.0: Start IWL Error Log Dump:\n"
          "[15883.337349] iwlwifi 0000:00:0c.0: Status: 0x00000100, count: 6\n"
          "[15883.337352] iwlwifi 0000:00:0c.0: Loaded firmware version: "
          "46.b20aefee.0\n"
          "[15883.337355] iwlwifi 0000:00:0c.0: 0x00000084 | "
          "NMI_INTERRUPT_UNKNOWN\n"
          "[15883.337357] iwlwifi 0000:00:0c.0: 0x000022F0 | trm_hw_status0\n"
          "[15883.337359] iwlwifi 0000:00:0c.0: 0x00000000 | trm_hw_status1\n"
          "[15883.337362] iwlwifi 0000:00:0c.0: 0x0048751E | branchlink2\n"
          "[15883.337364] iwlwifi 0000:00:0c.0: 0x00479236 | interruptlink1\n"
          "[15883.337366] iwlwifi 0000:00:0c.0: 0x0000AE00 | interruptlink2\n"
          "[15883.337369] iwlwifi 0000:00:0c.0: 0x0001A2D6 | data1\n"
          "[15883.337371] iwlwifi 0000:00:0c.0: 0xFF000000 | data2\n"
          "[15883.337373] iwlwifi 0000:00:0c.0: 0xF0000000 | data3\n"
          "[15883.337376] iwlwifi 0000:00:0c.0: 0x00000000 | beacon time\n"
          "[15883.337378] iwlwifi 0000:00:0c.0: 0x158DE6F7 | tsf low\n"
          "[15883.337380] iwlwifi 0000:00:0c.0: 0x00000000 | tsf hi\n"
          "[15883.337383] iwlwifi 0000:00:0c.0: 0x00000000 | time gp1\n"
          "[15883.337385] iwlwifi 0000:00:0c.0: 0x158DE6F9 | time gp2\n"
          "[15883.337388] iwlwifi 0000:00:0c.0: 0x00000001 | uCode revision "
          "type\n"
          "[15883.337390] iwlwifi 0000:00:0c.0: 0x0000002E | uCode version "
          "major\n"
          "[15883.337392] iwlwifi 0000:00:0c.0: 0xB20AEFEE | uCode version "
          "minor\n"
          "[15883.337394] iwlwifi 0000:00:0c.0: 0x00000312 | hw version\n"
          "[15883.337397] iwlwifi 0000:00:0c.0: 0x00C89008 | board version\n"
          "[15883.337399] iwlwifi 0000:00:0c.0: 0x007B019C | hcmd\n"
          "[15883.337401] iwlwifi 0000:00:0c.0: 0x00022000 | isr0\n"
          "[15883.337404] iwlwifi 0000:00:0c.0: 0x00000000 | isr1\n"
          "[15883.337406] iwlwifi 0000:00:0c.0: 0x08001802 | isr2\n"
          "[15883.337408] iwlwifi 0000:00:0c.0: 0x40400180 | isr3\n"
          "[15883.337411] iwlwifi 0000:00:0c.0: 0x00000000 | isr4\n"
          "[15883.337413] iwlwifi 0000:00:0c.0: 0x007B019C | last cmd Id\n"
          "[15883.337415] iwlwifi 0000:00:0c.0: 0x0001A2D6 | wait_event\n"
          "[15883.337417] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_control\n"
          "[15883.337420] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_duration\n"
          "[15883.337422] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_mhvalid\n"
          "[15883.337424] iwlwifi 0000:00:0c.0: 0x00000000 | l2p_addr_match\n"
          "[15883.337427] iwlwifi 0000:00:0c.0: 0x0000008F | lmpm_pmg_sel\n"
          "[15883.337429] iwlwifi 0000:00:0c.0: 0x24021230 | timestamp\n"
          "[15883.337432] iwlwifi 0000:00:0c.0: 0x0000B0D8 | flow_handler\n",
      .expected_flags = {{"--kernel_iwlwifi_error"}}};
  ParserTest<KernelParser>("TEST_IWLWIFI_LMAC", {wifi_error});
}

TEST(AnomalyDetectorTest, KernelWarning) {
  ParserRun second{
      .find_this = "ttm_bo_vm.c",
      .replace_with = "file_one.c",
      .expected_text = "0x19e/0x1ab [ttm]()\n[ 3955.309298] Modules linked in"};
  ParserTest<KernelParser>("TEST_WARNING", {simple_run, second});
}

TEST(AnomalyDetectorTest, KernelWarningNoDuplicate) {
  ParserRun identical_warning{.expected_size = 0};
  ParserTest<KernelParser>("TEST_WARNING", {simple_run, identical_warning});
}

TEST(AnomalyDetectorTest, KernelWarningHeader) {
  ParserRun warning_message{.expected_text = "Test Warning message asdfghjkl"};
  ParserTest<KernelParser>("TEST_WARNING_HEADER", {warning_message});
}

TEST(AnomalyDetectorTest, KernelWarningOld) {
  ParserTest<KernelParser>("TEST_WARNING_OLD", {simple_run});
}

TEST(AnomalyDetectorTest, KernelWarningOldARM64) {
  ParserRun unknown_function{.expected_text = "-unknown-function\n"};
  ParserTest<KernelParser>("TEST_WARNING_OLD_ARM64", {unknown_function});
}

TEST(AnomalyDetectorTest, KernelWarningWifi) {
  ParserRun wifi_warning = {.find_this = "gpu/drm/ttm",
                            .replace_with = "net/wireless",
                            .expected_flags = {{"--kernel_wifi_warning"}}};
  ParserTest<KernelParser>("TEST_WARNING", {wifi_warning});
}

TEST(AnomalyDetectorTest, KernelWarningSuspend) {
  ParserRun suspend_warning = {
      .find_this = "gpu/drm/ttm",
      .replace_with = "idle",
      .expected_flags = {{"--kernel_suspend_warning"}}};
  ParserTest<KernelParser>("TEST_WARNING", {suspend_warning});
}

TEST(AnomalyDetectorTest, CrashReporterCrash) {
  ParserRun crash_reporter_crash = {
      .expected_flags = {{"--crash_reporter_crashed"}}};
  ParserTest<KernelParser>("TEST_CR_CRASH", {crash_reporter_crash});
}

TEST(AnomalyDetectorTest, CrashReporterCrashRateLimit) {
  ParserRun crash_reporter_crash = {
      .expected_flags = {{"--crash_reporter_crashed"}}};
  ParserTest<KernelParser>("TEST_CR_CRASH",
                           {crash_reporter_crash, empty, empty});
}

TEST(AnomalyDetectorTest, ServiceFailure) {
  ParserRun one{.expected_text = "-exit2-"};
  ParserRun two{.find_this = "crash-crash", .replace_with = "fresh-fresh"};
  ServiceParser parser(true);
  ParserTest("TEST_SERVICE_FAILURE", {one, two}, &parser);
}

TEST(AnomalyDetectorTest, ServiceFailureArc) {
  ParserRun service_failure = {
      .find_this = "crash-crash",
      .replace_with = "arc-crash",
      .expected_text = "-exit2-arc-",
      .expected_flags = {{"--arc_service_failure=arc-crash"}}};
  ServiceParser parser(true);
  ParserTest("TEST_SERVICE_FAILURE", {service_failure}, &parser);
}

TEST(AnomalyDetectorTest, SELinuxViolation) {
  ParserRun selinux_violation = {
      .expected_text =
          "-selinux-u:r:init:s0-u:r:kernel:s0-module_request-init-",
      .expected_flags = {{"--selinux_violation"}}};
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
          "-suspend failure: device: dummy_dev step: suspend errno: -22",
      .expected_flags = {{"--suspend_failure"}}};
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
