// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/policy_manager/chromeos_policy.h"

#include <string>

#include <base/time/time.h>
#include <gtest/gtest.h>

#include "update_engine/fake_clock.h"
#include "update_engine/policy_manager/evaluation_context.h"
#include "update_engine/policy_manager/fake_state.h"
#include "update_engine/policy_manager/pmtest_utils.h"

using base::Time;
using base::TimeDelta;
using chromeos_update_engine::FakeClock;
using std::string;

namespace chromeos_policy_manager {

class PmChromeOSPolicyTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    SetUpDefaultClock();
    eval_ctx_ = new EvaluationContext(&fake_clock_);
    SetUpDefaultState();
    SetUpDefaultDevicePolicy();
  }

  // Sets the clock to fixed values.
  void SetUpDefaultClock() {
    fake_clock_.SetMonotonicTime(Time::FromInternalValue(12345678L));
    fake_clock_.SetWallclockTime(Time::FromInternalValue(12345678901234L));
  }

  void SetUpDefaultState() {
    fake_state_.updater_provider()->var_updater_started_time()->reset(
        new Time(fake_clock_.GetWallclockTime()));
    fake_state_.updater_provider()->var_last_checked_time()->reset(
        new Time(fake_clock_.GetWallclockTime()));
    fake_state_.updater_provider()->var_consecutive_failed_update_checks()->
        reset(new unsigned int(0));

    fake_state_.random_provider()->var_seed()->reset(
        new uint64_t(4));  // chosen by fair dice roll.
                           // guaranteed to be random.

    // No device policy loaded by default.
    fake_state_.device_policy_provider()->var_device_policy_is_loaded()->reset(
        new bool(false));

    // For the purpose of the tests, this is an official build.
    fake_state_.system_provider()->var_is_official_build()->reset(
        new bool(true));
  }

  // Sets up a default device policy that does not impose any restrictions, nor
  // enables any features (HTTP, P2P).
  void SetUpDefaultDevicePolicy() {
    fake_state_.device_policy_provider()->var_device_policy_is_loaded()->reset(
        new bool(true));
    fake_state_.device_policy_provider()->var_update_disabled()->reset(
        new bool(false));
    fake_state_.device_policy_provider()->
        var_allowed_connection_types_for_update()->reset(nullptr);
    fake_state_.device_policy_provider()->var_scatter_factor()->reset(
        new TimeDelta());
    fake_state_.device_policy_provider()->var_http_downloads_enabled()->reset(
        new bool(false));
    fake_state_.device_policy_provider()->var_au_p2p_enabled()->reset(
        new bool(false));
    fake_state_.device_policy_provider()->var_release_channel_delegated()->
        reset(new bool(true));
  }

  // Configures the UpdateCheckAllowed policy to return a desired value by
  // faking the current wall clock time as needed. Restores the default state.
  // This is used when testing policies that depend on this one.
  void SetUpdateCheckAllowed(bool allow_check) {
    Time next_update_check;
    ExpectPolicyStatus(EvalStatus::kSucceeded,
                       &ChromeOSPolicy::NextUpdateCheckTime,
                       &next_update_check);
    SetUpDefaultState();
    SetUpDefaultDevicePolicy();
    Time curr_time = next_update_check;
    if (allow_check)
      curr_time += TimeDelta::FromSeconds(1);
    else
      curr_time -= TimeDelta::FromSeconds(1);
    fake_clock_.SetWallclockTime(curr_time);
  }

  // Returns a default UpdateState structure: first seen time is calculated
  // backward from the current wall clock time, update was seen just once, there
  // is no scattering wait period and the max allowed is 7 days, there is no
  // check threshold and none is allowed.
  UpdateState GetDefaultUpdateState(TimeDelta update_first_seen_period) {
    UpdateState update_state = {
      fake_clock_.GetWallclockTime() - update_first_seen_period, 1,
      TimeDelta(), TimeDelta::FromDays(7), 0, 0, 0
    };
    return update_state;
  }

  // Runs the passed |policy_method| policy and expects it to return the
  // |expected| return value.
  template<typename T, typename R, typename... Args>
  void ExpectPolicyStatus(
      EvalStatus expected,
      T policy_method,
      R* result, Args... args) {
    string error = "<None>";
    eval_ctx_->ResetEvaluation();
    EXPECT_EQ(expected,
              (policy_.*policy_method)(eval_ctx_, &fake_state_, &error, result,
                                       args...))
        << "Returned error: " << error
        << "\nEvaluation context: " << eval_ctx_->DumpContext();
  }

  FakeClock fake_clock_;
  FakeState fake_state_;
  scoped_refptr<EvaluationContext> eval_ctx_;
  ChromeOSPolicy policy_;  // ChromeOSPolicy under test.
};

TEST_F(PmChromeOSPolicyTest, FirstCheckIsAtMostInitialIntervalAfterStart) {
  Time next_update_check;

  ExpectPolicyStatus(EvalStatus::kSucceeded,
                     &ChromeOSPolicy::NextUpdateCheckTime, &next_update_check);

  EXPECT_LE(fake_clock_.GetWallclockTime(), next_update_check);
  EXPECT_GE(fake_clock_.GetWallclockTime() + TimeDelta::FromSeconds(
      ChromeOSPolicy::kTimeoutInitialInterval +
      ChromeOSPolicy::kTimeoutRegularFuzz), next_update_check);
}

TEST_F(PmChromeOSPolicyTest, ExponentialBackoffIsCapped) {
  Time next_update_check;

  fake_state_.updater_provider()->var_consecutive_failed_update_checks()->
      reset(new unsigned int(100));
  ExpectPolicyStatus(EvalStatus::kSucceeded,
                     &ChromeOSPolicy::NextUpdateCheckTime, &next_update_check);

  EXPECT_LE(fake_clock_.GetWallclockTime() + TimeDelta::FromSeconds(
      ChromeOSPolicy::kTimeoutMaxBackoffInterval -
      ChromeOSPolicy::kTimeoutRegularFuzz - 1), next_update_check);
  EXPECT_GE(fake_clock_.GetWallclockTime() + TimeDelta::FromSeconds(
      ChromeOSPolicy::kTimeoutMaxBackoffInterval +
      ChromeOSPolicy::kTimeoutRegularFuzz), next_update_check);
}

TEST_F(PmChromeOSPolicyTest, UpdateCheckAllowedWaitsForTheTimeout) {
  // We get the next update_check timestamp from the policy's private method
  // and then we check the public method respects that value on the normal
  // case.
  Time next_update_check;
  Time last_checked_time =
      fake_clock_.GetWallclockTime() + TimeDelta::FromMinutes(1234);

  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(last_checked_time));
  ExpectPolicyStatus(EvalStatus::kSucceeded,
                     &ChromeOSPolicy::NextUpdateCheckTime, &next_update_check);

  UpdateCheckParams result;

  // Check that the policy blocks until the next_update_check is reached.
  SetUpDefaultClock();
  SetUpDefaultState();
  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(last_checked_time));
  fake_clock_.SetWallclockTime(next_update_check - TimeDelta::FromSeconds(1));
  ExpectPolicyStatus(EvalStatus::kAskMeAgainLater,
                     &Policy::UpdateCheckAllowed, &result);

  SetUpDefaultClock();
  SetUpDefaultState();
  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(last_checked_time));
  fake_clock_.SetWallclockTime(next_update_check + TimeDelta::FromSeconds(1));
  ExpectPolicyStatus(EvalStatus::kSucceeded,
                     &Policy::UpdateCheckAllowed, &result);
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartFailsCheckAllowedError) {
  // The UpdateCanStart policy fails, not being able to query
  // UpdateCheckAllowed.

  // Configure the UpdateCheckAllowed policy to fail.
  fake_state_.updater_provider()->var_updater_started_time()->reset(nullptr);

  // Check that the UpdateCanStart fails.
  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromMinutes(10));
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kFailed,
                     &Policy::UpdateCanStart, &result, false, update_state);
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartNotAllowedCheckDue) {
  // The UpdateCanStart policy returns false because we are due for another
  // update check.

  SetUpdateCheckAllowed(true);

  // Check that the UpdateCanStart returns false.
  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromMinutes(10));
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded,
                     &Policy::UpdateCanStart, &result, false, update_state);
  EXPECT_FALSE(result.update_can_start);
  EXPECT_EQ(UpdateCannotStartReason::kCheckDue, result.cannot_start_reason);
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartAllowedNoDevicePolicy) {
  // The UpdateCanStart policy returns true; no device policy is loaded.

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_device_policy_is_loaded()->reset(
      new bool(false));

  // Check that the UpdateCanStart returns true with no further attributes.
  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromMinutes(10));
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded,
                     &Policy::UpdateCanStart, &result, false, update_state);
  EXPECT_TRUE(result.update_can_start);
  EXPECT_TRUE(result.http_allowed);
  EXPECT_FALSE(result.p2p_allowed);
  EXPECT_TRUE(result.target_channel.empty());
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartAllowedBlankPolicy) {
  // The UpdateCanStart policy returns true; device policy is loaded but imposes
  // no restrictions on updating.

  SetUpdateCheckAllowed(false);

  // Check that the UpdateCanStart returns true.
  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromMinutes(10));
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded,
                     &Policy::UpdateCanStart, &result, false, update_state);
  EXPECT_TRUE(result.update_can_start);
  EXPECT_FALSE(result.http_allowed);
  EXPECT_FALSE(result.p2p_allowed);
  EXPECT_TRUE(result.target_channel.empty());
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartNotAllowedUpdatesDisabled) {
  // The UpdateCanStart should return false (kAskMeAgainlater) because a device
  // policy is loaded and prohibits updates.

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_update_disabled()->reset(
      new bool(true));

  // Check that the UpdateCanStart returns false.
  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromMinutes(10));
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kAskMeAgainLater,
                     &Policy::UpdateCanStart, &result, false, update_state);
  EXPECT_FALSE(result.update_can_start);
  EXPECT_EQ(UpdateCannotStartReason::kDisabledByPolicy,
            result.cannot_start_reason);
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartFailsScatteringFailed) {
  // The UpdateCanStart policy fails because the UpdateScattering policy it
  // depends on fails (unset variable).

  SetUpdateCheckAllowed(false);

  // Override the default seed variable with a null value so that the policy
  // request would fail.
  fake_state_.random_provider()->var_seed()->reset(nullptr);

  // Check that the UpdateCanStart fails.
  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromSeconds(1));
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kFailed,
                     &Policy::UpdateCanStart, &result, false, update_state);
}

TEST_F(PmChromeOSPolicyTest,
       UpdateCanStartNotAllowedScatteringNewWaitPeriodApplies) {
  // The UpdateCanStart policy returns false; device policy is loaded and
  // scattering applies due to an unsatisfied wait period, which was newly
  // generated.

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_scatter_factor()->reset(
      new TimeDelta(TimeDelta::FromMinutes(2)));


  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromSeconds(1));

  // Check that the UpdateCanStart returns false and a new wait period
  // generated.
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded, &Policy::UpdateCanStart, &result,
                     false, update_state);
  EXPECT_FALSE(result.update_can_start);
  EXPECT_EQ(UpdateCannotStartReason::kScattering, result.cannot_start_reason);
  EXPECT_LT(TimeDelta(), result.scatter_wait_period);
  EXPECT_EQ(0, result.scatter_check_threshold);
}

TEST_F(PmChromeOSPolicyTest,
       UpdateCanStartNotAllowedScatteringPrevWaitPeriodStillApplies) {
  // The UpdateCanStart policy returns false w/ kAskMeAgainLater; device policy
  // is loaded and a previously generated scattering period still applies, none
  // of the scattering values has changed.

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_scatter_factor()->reset(
      new TimeDelta(TimeDelta::FromMinutes(2)));

  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromSeconds(1));
  update_state.scatter_wait_period = TimeDelta::FromSeconds(35);

  // Check that the UpdateCanStart returns false and a new wait period
  // generated.
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kAskMeAgainLater, &Policy::UpdateCanStart,
                     &result, false, update_state);
  EXPECT_FALSE(result.update_can_start);
  EXPECT_EQ(UpdateCannotStartReason::kScattering, result.cannot_start_reason);
  EXPECT_EQ(TimeDelta::FromSeconds(35), result.scatter_wait_period);
  EXPECT_EQ(0, result.scatter_check_threshold);
}

TEST_F(PmChromeOSPolicyTest,
       UpdateCanStartNotAllowedScatteringNewCountThresholdApplies) {
  // The UpdateCanStart policy returns false; device policy is loaded and
  // scattering applies due to an unsatisfied update check count threshold.
  //
  // This ensures a non-zero check threshold, which may or may not be combined
  // with a non-zero wait period (for which we cannot reliably control).

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_scatter_factor()->reset(
      new TimeDelta(TimeDelta::FromSeconds(1)));

  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromSeconds(1));
  update_state.scatter_check_threshold_min = 2;
  update_state.scatter_check_threshold_max = 5;

  // Check that the UpdateCanStart returns false.
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded, &Policy::UpdateCanStart, &result,
                     false, update_state);
  EXPECT_FALSE(result.update_can_start);
  EXPECT_EQ(UpdateCannotStartReason::kScattering, result.cannot_start_reason);
  EXPECT_LE(2, result.scatter_check_threshold);
  EXPECT_GE(5, result.scatter_check_threshold);
}

TEST_F(PmChromeOSPolicyTest,
       UpdateCanStartNotAllowedScatteringPrevCountThresholdStillApplies) {
  // The UpdateCanStart policy returns false; device policy is loaded and
  // scattering due to a previously generated count threshold still applies.

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_scatter_factor()->reset(
      new TimeDelta(TimeDelta::FromSeconds(1)));

  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromSeconds(1));
  update_state.scatter_check_threshold = 3;
  update_state.scatter_check_threshold_min = 2;
  update_state.scatter_check_threshold_max = 5;

  // Check that the UpdateCanStart returns false.
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded, &Policy::UpdateCanStart, &result,
                     false, update_state);
  EXPECT_FALSE(result.update_can_start);
  EXPECT_EQ(UpdateCannotStartReason::kScattering, result.cannot_start_reason);
  EXPECT_EQ(3, result.scatter_check_threshold);
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartAllowedScatteringSatisfied) {
  // The UpdateCanStart policy returns true; device policy is loaded and
  // scattering is enabled, but both wait period and check threshold are
  // satisfied.

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_scatter_factor()->reset(
      new TimeDelta(TimeDelta::FromSeconds(120)));

  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromSeconds(75));
  update_state.num_checks = 4;
  update_state.scatter_wait_period = TimeDelta::FromSeconds(60);
  update_state.scatter_check_threshold = 3;
  update_state.scatter_check_threshold_min = 2;
  update_state.scatter_check_threshold_max = 5;

  // Check that the UpdateCanStart returns true.
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded, &Policy::UpdateCanStart, &result,
                     false, update_state);
  EXPECT_TRUE(result.update_can_start);
  EXPECT_EQ(TimeDelta(), result.scatter_wait_period);
  EXPECT_EQ(0, result.scatter_check_threshold);
}

TEST_F(PmChromeOSPolicyTest,
       UpdateCanStartAllowedInteractivePreventsScattering) {
  // The UpdateCanStart policy returns true; device policy is loaded and
  // scattering would have applied, except that the update check is interactive
  // and so it is suppressed.

  SetUpdateCheckAllowed(false);
  fake_state_.device_policy_provider()->var_scatter_factor()->reset(
      new TimeDelta(TimeDelta::FromSeconds(1)));

  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromSeconds(1));
  update_state.scatter_check_threshold = 0;
  update_state.scatter_check_threshold_min = 2;
  update_state.scatter_check_threshold_max = 5;

  // Check that the UpdateCanStart returns true.
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded, &Policy::UpdateCanStart, &result,
                     true, update_state);
  EXPECT_TRUE(result.update_can_start);
  EXPECT_EQ(TimeDelta(), result.scatter_wait_period);
  EXPECT_EQ(0, result.scatter_check_threshold);
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartAllowedWithAttributes) {
  // The UpdateCanStart policy returns true; device policy permits both HTTP and
  // P2P updates, as well as a non-empty target channel string.

  SetUpdateCheckAllowed(false);

  // Override specific device policy attributes.
  fake_state_.device_policy_provider()->var_http_downloads_enabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()->var_au_p2p_enabled()->reset(
      new bool(true));
  fake_state_.device_policy_provider()->var_release_channel_delegated()->
      reset(new bool(false));
  fake_state_.device_policy_provider()->var_release_channel()->
      reset(new string("foo-channel"));

  // Check that the UpdateCanStart returns true.
  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromMinutes(10));
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded, &Policy::UpdateCanStart, &result,
                     false, update_state);
  EXPECT_TRUE(result.update_can_start);
  EXPECT_TRUE(result.http_allowed);
  EXPECT_TRUE(result.p2p_allowed);
  EXPECT_EQ("foo-channel", result.target_channel);
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartAllowedWithP2PFromUpdater) {
  // The UpdateCanStart policy returns true; device policy forbids both HTTP and
  // P2P updates, but the updater is configured to allow P2P and overrules the
  // setting.

  SetUpdateCheckAllowed(false);

  // Override specific device policy attributes.
  fake_state_.device_policy_provider()->var_release_channel_delegated()->
      reset(new bool(false));
  fake_state_.device_policy_provider()->var_release_channel()->
      reset(new string("foo-channel"));
  fake_state_.updater_provider()->var_p2p_enabled()->reset(new bool(true));

  // Check that the UpdateCanStart returns true.
  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromMinutes(10));
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded, &Policy::UpdateCanStart, &result,
                     false, update_state);
  EXPECT_TRUE(result.update_can_start);
  EXPECT_FALSE(result.http_allowed);
  EXPECT_TRUE(result.p2p_allowed);
  EXPECT_EQ("foo-channel", result.target_channel);
}

TEST_F(PmChromeOSPolicyTest, UpdateCanStartAllowedWithHttpForUnofficialBuild) {
  // The UpdateCanStart policy returns true; device policy forbids both HTTP and
  // P2P updates, but marking this an unofficial build overrules the HTTP
  // setting.

  SetUpdateCheckAllowed(false);

  // Override specific device policy attributes.
  fake_state_.device_policy_provider()->var_release_channel_delegated()->
      reset(new bool(false));
  fake_state_.device_policy_provider()->var_release_channel()->
      reset(new string("foo-channel"));
  fake_state_.system_provider()->var_is_official_build()->
      reset(new bool(false));

  // Check that the UpdateCanStart returns true.
  UpdateState update_state = GetDefaultUpdateState(TimeDelta::FromMinutes(10));
  UpdateCanStartResult result;
  ExpectPolicyStatus(EvalStatus::kSucceeded, &Policy::UpdateCanStart, &result,
                     false, update_state);
  EXPECT_TRUE(result.update_can_start);
  EXPECT_TRUE(result.http_allowed);
  EXPECT_FALSE(result.p2p_allowed);
  EXPECT_EQ("foo-channel", result.target_channel);
}

}  // namespace chromeos_policy_manager
