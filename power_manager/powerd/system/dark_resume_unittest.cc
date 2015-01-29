// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/dark_resume.h"

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <base/timer/mock_timer.h>
#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/power_supply_stub.h"

namespace power_manager {
namespace system {

namespace {

// This class hierarchy exists so we can test both the synchronous and
// asynchronous dark resume pathways using the same tests. Since the
// two pathways perform their state changes at different times in the
// suspend/resume process and report actions back to Suspender in
// different ways, we need an adapter to make sure they are making the
// same decisions and we are going to see the same behavior across each.
class SystemAbstraction {
 public:
  virtual ~SystemAbstraction() {}

  // Get a timer.
  virtual scoped_ptr<base::Timer> CreateTimer() = 0;

  // Perform a fake suspend preparation using |dark_resume|,
  // putting the next action to take and time to suspend for
  // in |action| and |suspend_duration|, respectively.
  // If we are in "dark resume" and |woken_by_timer| is set,
  // then we will pretend that this timer fired to wake us up
  // from the suspend.
  virtual void Suspend(DarkResume* dark_resume,
                       DarkResumeInterface::Action* action,
                       base::TimeDelta* suspend_duration,
                       bool woken_by_timer) = 0;
};

// Test fixture helper for pre-3.11 kernel functionality.
class LegacySystem : public SystemAbstraction {
 public:
  static const char kStateFile[];
  static const char kSourceFile[];
  // Values for kStateFile.
  static const char kInDarkResume[];
  static const char kNotInDarkResume[];
  // Values for kSourceFile.
  static const char kEnabled[];
  static const char kDisabled[];

  scoped_ptr<base::Timer> CreateTimer() override {
    return scoped_ptr<base::Timer>();
  }

  // In the legacy case, we prepare the action synchronously,
  // so just call into |dark_resume|.
  void Suspend(DarkResume* dark_resume,
               DarkResumeInterface::Action* action,
               base::TimeDelta* suspend_duration,
               bool woken_by_timer) override {
    dark_resume->GetActionForSuspendAttempt(action, suspend_duration);
  }
};

const char LegacySystem::kStateFile[] = "dark_resume_state";
const char LegacySystem::kSourceFile[] = "dark_resume_source";
const char LegacySystem::kInDarkResume[] = "1";
const char LegacySystem::kNotInDarkResume[] = "0";
const char LegacySystem::kEnabled[] = "enabled";
const char LegacySystem::kDisabled[] = "disabled";

// Test fixture helper for functionality from 3.11 kernel onwards.
class WakeupTypeSystem : public SystemAbstraction {
 public:
  WakeupTypeSystem() : timer_(NULL) {}

  static const char kStateFile[];
  static const char kSourceFile[];
  // Values for kStateFile.
  static const char kInDarkResume[];
  static const char kNotInDarkResume[];
  // Values for kSourceFile.
  static const char kEnabled[];
  static const char kDisabled[];

  scoped_ptr<base::Timer> CreateTimer() override {
    timer_ = new base::MockTimer(true /* retain_user_task */,
                                 false /* is_repeating */);
    return scoped_ptr<base::Timer>(timer_);
  }

  // In the asynchronous case, we are actually scheduling the action
  // to be set next time we resume. In order to make sure we are
  // keeping the right behavior, we need to 1. make sure we have
  // scheduled the work for the right time in the future, and 2. make
  // sure the work will perform the action we expect it to. We then
  // return these in a way compatible with the legacy case so we know
  // we are doing the right thing without changing the tests.
  // For 1, we ask the timer what the delay is before the work will run.
  // For 2, we fire the timer and then check what it set next_action_
  // to.
  void Suspend(DarkResume* dark_resume,
               DarkResumeInterface::Action* action,
               base::TimeDelta* suspend_duration,
               bool woken_by_timer) override {
    dark_resume->GetActionForSuspendAttempt(action, suspend_duration);
    // In this pathway, we should never set |suspend_duration|.
    // Setting the wake alarm has already been done by DarkResume.
    EXPECT_EQ(0, suspend_duration->InSeconds());
    // Set up |action| and |suspend_duration| as if this was the
    // legacy pathway.
    if (timer_->IsRunning()) {
      *suspend_duration = timer_->GetCurrentDelay();
      if (dark_resume->InDarkResume() && woken_by_timer)
        timer_->Fire();
    }
    *action = dark_resume->next_action_for_testing();
  }

 private:
  // THIS IS A WEAK POINTER.
  // The timer will be owned by the DarkResume class.
  base::MockTimer* timer_;

  DISALLOW_COPY_AND_ASSIGN(WakeupTypeSystem);
};

const char WakeupTypeSystem::kStateFile[] = "wakeup_type";
const char WakeupTypeSystem::kSourceFile[] = "wakeup_type";
const char WakeupTypeSystem::kInDarkResume[] = "automatic";
const char WakeupTypeSystem::kNotInDarkResume[] = "user";
const char WakeupTypeSystem::kEnabled[] = "automatic";
const char WakeupTypeSystem::kDisabled[] = "unknown";

}  // namespace

template <typename SystemType>
class DarkResumeTest : public ::testing::Test {
 public:
  DarkResumeTest() : dark_resume_(new DarkResume) {
    CHECK(temp_dir_.CreateUniqueTempDir());
    CHECK(temp_dir_.IsValid());
  }

 protected:
  // Initializes |dark_resume_|.
  void Init() {
    WriteDarkResumeState(false);
    SetBattery(100.0, false);

    // Override both the legacy and wakeup-type paths to ensure that DarkResume
    // doesn't actually read from sysfs.
    dark_resume_->set_legacy_state_path_for_testing(
        temp_dir_.path().Append(LegacySystem::kStateFile));
    dark_resume_->set_wakeup_state_path_for_testing(
        temp_dir_.path().Append(WakeupTypeSystem::kStateFile));

    dark_resume_->Init(&power_supply_, &prefs_);
    dark_resume_->set_timer_for_testing(system_.CreateTimer());
  }

  // Writes the passed-in dark resume state to the in-use state file.
  void WriteDarkResumeState(bool in_dark_resume) {
    const char* state = in_dark_resume ? SystemType::kInDarkResume :
        SystemType::kNotInDarkResume;
    ASSERT_TRUE(util::WriteFileFully(
        temp_dir_.path().Append(SystemType::kStateFile),
        state, strlen(state)));
  }

  // Updates the status returned by |power_supply_|.
  void SetBattery(double charge_percent, bool ac_online) {
    PowerStatus status;
    status.battery_percentage = charge_percent;
    status.line_power_on = ac_online;
    power_supply_.set_status(status);
  }

  // Returns the contents of |path|, or an empty string if the file doesn't
  // exist.
  std::string ReadFile(const base::FilePath& path) {
    std::string value;
    base::ReadFileToString(path, &value);
    return value;
  }

  void Suspend(DarkResumeInterface::Action* action,
               base::TimeDelta* suspend_duration,
               bool woken_by_timer) {
    system_.Suspend(dark_resume_.get(), action, suspend_duration,
                             woken_by_timer);
    dark_resume_->HandleSuccessfulResume();
  }

  base::ScopedTempDir temp_dir_;
  FakePrefs prefs_;
  PowerSupplyStub power_supply_;
  scoped_ptr<DarkResume> dark_resume_;

  SystemType system_;
};

// We want to test both pathways.
typedef ::testing::Types<LegacySystem, WakeupTypeSystem> Pathways;
TYPED_TEST_CASE(DarkResumeTest, Pathways);

// Tests.
// Note the heavy use of "this->" everywhere. Sadly, since we are subclassing
// a class that was templated, this is necessary. If you leave it out, the
// compiler will complain that it can't find the identifier.
TYPED_TEST(DarkResumeTest, SuspendAndShutDown) {
  this->prefs_.SetString(kDarkResumeSuspendDurationsPref, "0.0 10");
  this->Init();

  // When suspending from a non-dark-resume state, the system shouldn't shut
  // down.
  this->WriteDarkResumeState(false);
  this->SetBattery(60.0, false);
  DarkResumeInterface::Action action;
  base::TimeDelta suspend_duration;

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(10, suspend_duration.InSeconds());
  EXPECT_FALSE(this->dark_resume_->InDarkResume());

  // If the battery charge increases before a dark resume, the system should
  // resuspend.
  this->WriteDarkResumeState(true);
  this->SetBattery(61.0, false);

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, true);
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(10, suspend_duration.InSeconds());
  EXPECT_TRUE(this->dark_resume_->InDarkResume());

  // The higher battery charge should be used as the new shutdown threshold for
  // the next dark resume.
  this->SetBattery(60.5, false);
  this->Suspend(&action, &suspend_duration, true);
  EXPECT_EQ(DarkResumeInterface::SHUT_DOWN, action);
  EXPECT_TRUE(this->dark_resume_->InDarkResume());
}

// Test that a new shutdown threshold is calculated when suspending from outside
// of dark resume.
TYPED_TEST(DarkResumeTest, UserResumes) {
  this->prefs_.SetString(kDarkResumeSuspendDurationsPref, "0.0 10\n"
                                                          "20.0 50\n"
                                                          "50.0 100\n"
                                                          "80.0 500\n");
  this->Init();
  DarkResumeInterface::Action action;
  base::TimeDelta suspend_duration;

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(500, suspend_duration.InSeconds());

  this->SetBattery(80.0, false);

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(500, suspend_duration.InSeconds());

  this->SetBattery(50.0, false);

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(100, suspend_duration.InSeconds());

  this->SetBattery(25.0, false);

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(50, suspend_duration.InSeconds());

  this->SetBattery(20.0, false);

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(50, suspend_duration.InSeconds());

  this->SetBattery(5.0, false);
  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();

  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(10, suspend_duration.InSeconds());
  this->SetBattery(1.0, false);

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(10, suspend_duration.InSeconds());
}

// Check that we don't shut down when on line power (regardless of the battery
// level).
TYPED_TEST(DarkResumeTest, LinePower) {
  this->prefs_.SetString(kDarkResumeSuspendDurationsPref, "0.0 10");
  this->Init();
  DarkResumeInterface::Action action;
  base::TimeDelta suspend_duration;

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, true);
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);

  // We'll dark resume in a lower battery state.
  this->WriteDarkResumeState(true);
  this->SetBattery(50.0, true);

  this->Suspend(&action, &suspend_duration, true);
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
}

TYPED_TEST(DarkResumeTest, EnableAndDisable) {
  const base::FilePath kDeviceDir = this->temp_dir_.path().Append("foo");
  const base::FilePath kPowerDir = kDeviceDir.Append(DarkResume::kPowerDir);
  const base::FilePath kActivePath = kPowerDir.Append(DarkResume::kActiveFile);
  const base::FilePath kSourcePath = kPowerDir.Append(TypeParam::kSourceFile);
  ASSERT_TRUE(base::CreateDirectory(kPowerDir));

  this->prefs_.SetString(kDarkResumeDevicesPref, kDeviceDir.value());
  this->prefs_.SetString(kDarkResumeSourcesPref, kDeviceDir.value());
  this->prefs_.SetString(kDarkResumeSuspendDurationsPref, "0.0 10");

  // Dark resume should be enabled when the object is initialized.
  this->Init();
  EXPECT_EQ(DarkResume::kEnabled, this->ReadFile(kActivePath));
  EXPECT_EQ(TypeParam::kEnabled, this->ReadFile(kSourcePath));

  // Dark resume should be disabled when the object is destroyed.
  this->dark_resume_.reset();
  EXPECT_EQ(DarkResume::kDisabled, this->ReadFile(kActivePath));
  EXPECT_EQ(TypeParam::kDisabled, this->ReadFile(kSourcePath));

  // Set the "disable" pref and check that the files aren't set to the enabled
  // state after initializing a new object.
  this->prefs_.SetInt64(kDisableDarkResumePref, 1);
  this->dark_resume_.reset(new DarkResume);
  this->Init();
  EXPECT_EQ(DarkResume::kDisabled, this->ReadFile(kActivePath));
  EXPECT_EQ(TypeParam::kDisabled, this->ReadFile(kSourcePath));
  DarkResumeInterface::Action action;
  base::TimeDelta suspend_duration;

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(0, suspend_duration.InSeconds());

  // When the "disable" pref is set to 0, dark resume should be enabled.
  this->prefs_.SetInt64(kDisableDarkResumePref, 0);
  this->dark_resume_.reset(new DarkResume);
  this->Init();
  EXPECT_EQ(DarkResume::kEnabled, this->ReadFile(kActivePath));
  EXPECT_EQ(TypeParam::kEnabled, this->ReadFile(kSourcePath));

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(10, suspend_duration.InSeconds());

  // An empty suspend durations pref should result in dark resume being
  // disabled.
  this->prefs_.SetString(kDarkResumeSuspendDurationsPref, std::string());
  this->dark_resume_.reset(new DarkResume);
  this->Init();
  EXPECT_EQ(DarkResume::kDisabled, this->ReadFile(kActivePath));
  EXPECT_EQ(TypeParam::kDisabled, this->ReadFile(kSourcePath));

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(0, suspend_duration.InSeconds());
}

TYPED_TEST(DarkResumeTest, PowerStatusRefreshFails) {
  this->prefs_.SetString(kDarkResumeSuspendDurationsPref, "0.0 10");
  this->Init();

  // If refreshing the power status fails, the system should suspend
  // indefinitely.
  this->SetBattery(80.0, false);
  this->power_supply_.set_refresh_result(false);
  DarkResumeInterface::Action action;
  base::TimeDelta suspend_duration;

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(0, suspend_duration.InSeconds());

  // Now let the system suspend.
  this->power_supply_.set_refresh_result(true);

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(10, suspend_duration.InSeconds());

  // If the refresh fails while in dark resume, the system should again suspend
  // indefinitely.
  this->WriteDarkResumeState(true);
  this->power_supply_.set_refresh_result(false);
  this->Suspend(&action, &suspend_duration, true);
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(0, suspend_duration.InSeconds());
}

// Check that we don't fail to schedule work after a full resume.
TYPED_TEST(DarkResumeTest, FullResumeReschedule) {
  this->prefs_.SetString(kDarkResumeSuspendDurationsPref, "0.0 10\n"
                                                          "50.0 20\n");
  this->Init();

  // This "full resume" will not have fired the timer.
  this->SetBattery(80.0, false);
  DarkResumeInterface::Action action;
  base::TimeDelta suspend_duration;

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(20, suspend_duration.InSeconds());

  // Next time we suspend, we should have rescheduled work.
  this->SetBattery(40.0, false);

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(10, suspend_duration.InSeconds());
}

// Check that we don't reschedule work when we dark resume for another reason.
TYPED_TEST(DarkResumeTest, InterruptedDarkResume) {
  this->prefs_.SetString(kDarkResumeSuspendDurationsPref, "0.0 10\n"
                                                          "50.0 20\n");
  this->Init();

  // This doesn't apply to legacy pathways, since there are no dark resumes
  // for other reasons.
  if (base::is_same<TypeParam, LegacySystem>::value)
    return;

  // We'll dark resume, but it will be for another reason, so the callback
  // won't be run yet.
  this->SetBattery(80.0, false);
  this->WriteDarkResumeState(true);
  DarkResumeInterface::Action action;
  base::TimeDelta suspend_duration;

  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(20, suspend_duration.InSeconds());

  // Next time we suspend, we should not have rescheduled work.
  this->SetBattery(40.0, false);

  this->Suspend(&action, &suspend_duration, false);
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(20, suspend_duration.InSeconds());
}

// Check that we suspend normally after exiting dark resume after user activity.
TYPED_TEST(DarkResumeTest, ExitDarkResume) {
  this->prefs_.SetString(kDarkResumeSuspendDurationsPref, "0.0 10\n"
                                                          "50.0 20\n");
  this->Init();
  this->SetBattery(80.0, false);
  DarkResumeInterface::Action action;
  base::TimeDelta suspend_duration;

  this->dark_resume_->PrepareForSuspendRequest();
  // Set dark resume state for suspend.
  this->WriteDarkResumeState(true);
  this->Suspend(&action, &suspend_duration, false);

  // This should bring the system out of dark resume.
  this->dark_resume_->UndoPrepareForSuspendRequest();
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(20, suspend_duration.InSeconds());

  this->SetBattery(40.0, false);
  this->dark_resume_->PrepareForSuspendRequest();
  this->Suspend(&action, &suspend_duration, false);
  EXPECT_EQ(DarkResumeInterface::SUSPEND, action);
  EXPECT_EQ(10, suspend_duration.InSeconds());
}

}  // namespace system
}  // namespace power_manager
