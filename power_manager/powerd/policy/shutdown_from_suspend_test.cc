// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/shutdown_from_suspend.h"

#include <base/macros.h>
#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/test_main_loop_runner.h"
#include "power_manager/powerd/system/power_supply_stub.h"

namespace power_manager {
namespace policy {

namespace {
constexpr auto kRunLoopDelay = base::TimeDelta::FromMilliseconds(200);
}

class ShutdownFromSuspendTest : public ::testing::Test {
 public:
  ShutdownFromSuspendTest()
      : shutdown_from_suspend_(timers::SimpleAlarmTimer::CreateForTesting()) {}
  ShutdownFromSuspendTest(const ShutdownFromSuspendTest&) = delete;
  ShutdownFromSuspendTest& operator=(const ShutdownFromSuspendTest&) = delete;

  ~ShutdownFromSuspendTest() override = default;

 protected:
  void Init(bool enable_dark_resume, int64_t shutdown_after_secs) {
    prefs_.SetInt64(kShutdownFromSuspendSecPref, shutdown_after_secs);
    prefs_.SetInt64(kDisableDarkResumePref, enable_dark_resume ? 0 : 1);
    shutdown_from_suspend_.Init(&prefs_, &power_supply_);
  }

  void SetLinePower(bool line_power) {
    system::PowerStatus status;
    status.line_power_on = line_power;
    power_supply_.set_status(status);
  }

  ShutdownFromSuspend shutdown_from_suspend_;
  FakePrefs prefs_;
  system::PowerSupplyStub power_supply_;
  TestMainLoopRunner runner_;
};

// Test that ShutdownFromSuspend is enabled when
//  1. Dark resume is enabled
//  2. |kShutdownFromSuspendSecPref| value is set to positive integer.
TEST_F(ShutdownFromSuspendTest, TestEnable) {
  Init(true, 1);
  EXPECT_TRUE(shutdown_from_suspend_.enabled_for_testing());
}

// Test that ShutdownFromSuspend is disabled when dark resume is disabled.
TEST_F(ShutdownFromSuspendTest, TestDarkResumeDisabled) {
  Init(false, 1);
  EXPECT_FALSE(shutdown_from_suspend_.enabled_for_testing());
}

// Test that ShutdownFromSuspend is disabled when
// |kShutdownFromSuspendSecPref| value is set to 0.
TEST_F(ShutdownFromSuspendTest, TestkShutdownFromSuspendSecPref0) {
  Init(false, 1);
  EXPECT_FALSE(shutdown_from_suspend_.enabled_for_testing());
}

// Test that ShutdownFromSuspend asks the system to shut down when
// 1. ShutdownFromSuspend is enabled
// 2. Device has spent |kShutdownFromSuspendSecPref| in suspend
// 3. Device is not on line power when dark resumed.
TEST_F(ShutdownFromSuspendTest, TestShutdownPath) {
  int kShutdownAfterSecs = 1;
  Init(true, kShutdownAfterSecs);
  // First |PrepareForSuspendAttempt| after boot should always return
  // Action::SUSPEND
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
  base::TimeDelta run_loop_for =
      base::TimeDelta::FromSeconds(kShutdownAfterSecs) + kRunLoopDelay;
  runner_.StartLoop(run_loop_for);
  // Fake a dark resume.
  shutdown_from_suspend_.HandleDarkResume();
  // Now |PrepareForSuspendAttempt| should return Action::SHUT_DOWN
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SHUT_DOWN);
}

// Test that ShutdownFromSuspend asks the system to suspend if the device is on
// line power.
TEST_F(ShutdownFromSuspendTest, TestOnLinePower) {
  int kShutdownAfterSecs = 1;
  Init(true, kShutdownAfterSecs);
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  base::TimeDelta run_loop_for =
      base::TimeDelta::FromSeconds(kShutdownAfterSecs) + kRunLoopDelay;
  runner_.StartLoop(run_loop_for);
  // Fake a dark resume.
  shutdown_from_suspend_.HandleDarkResume();
  SetLinePower(true);
  // Now |PrepareForSuspendAttempt| should return Action::SUSPEND as it is on
  // line power.
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
  // Fake another dark resume without line power. PrepareForSuspendAttempt|
  // should return Action::SHUT_DOWN.
  SetLinePower(false);
  shutdown_from_suspend_.HandleDarkResume();
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SHUT_DOWN);
}

// Test that ShutdownFromSuspend asks the policy to suspend when in full
// resume.
TEST_F(ShutdownFromSuspendTest, TestFullResume) {
  int kShutdownAfterSecs = 1;
  Init(true, kShutdownAfterSecs);
  shutdown_from_suspend_.PrepareForSuspendAttempt();
  base::TimeDelta run_loop_for =
      base::TimeDelta::FromSeconds(kShutdownAfterSecs) + kRunLoopDelay;
  runner_.StartLoop(run_loop_for);
  // Fake a full resume.
  shutdown_from_suspend_.HandleFullResume();
  // Now |PrepareForSuspendAttempt| should return Action::SUSPEND
  EXPECT_EQ(shutdown_from_suspend_.PrepareForSuspendAttempt(),
            ShutdownFromSuspend::Action::SUSPEND);
}

}  // namespace policy
}  // namespace power_manager
