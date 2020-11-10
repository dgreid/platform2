// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/suspender.h"

#include <base/bind.h>
#include <base/callback.h>
#include <base/compiler_specific.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "power_manager/common/action_recorder.h"
#include "power_manager/common/clock.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/shutdown_from_suspend_stub.h"
#include "power_manager/powerd/system/dark_resume_stub.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/powerd/system/display/display_watcher_stub.h"
#include "power_manager/powerd/system/wakeup_source_identifier_stub.h"
#include "power_manager/proto_bindings/suspend.pb.h"

namespace power_manager {
namespace policy {

namespace {

// Various actions that can be returned by TestDelegate::GetActions().
const char kPrepare[] = "prepare";
const char kSuspend[] = "suspend";
const char kUnprepare[] = "unprepare";
const char kShutDown[] = "shut_down";
const char kGenerateDarkResumeMetrics[] = "generate_dark_resume_metrics";
const char kNoActions[] = "";

// Test implementation of Suspender::Delegate that just records the actions it
// was asked to perform.
class TestDelegate : public Suspender::Delegate, public ActionRecorder {
 public:
  TestDelegate() = default;
  TestDelegate(const TestDelegate&) = delete;
  TestDelegate& operator=(const TestDelegate&) = delete;

  ~TestDelegate() override = default;

  void set_lid_closed(bool closed) { lid_closed_ = closed; }
  void set_report_success_for_read_wakeup_count(bool success) {
    report_success_for_read_wakeup_count_ = success;
  }
  void set_suspend_announced(bool announced) { suspend_announced_ = announced; }
  void set_suspend_result(SuspendResult result) { suspend_result_ = result; }
  void set_wakeup_count(uint64_t count) { wakeup_count_ = count; }
  void set_clock(Clock* clock) { clock_ = clock; }
  // TODO(chromeos-power): Delete this and use set_suspend_callback instead.
  void set_suspend_advance_time(base::TimeDelta delta) {
    suspend_advance_time_ = delta;
  }
  void set_completion_callback(base::Closure callback) {
    completion_callback_ = callback;
  }
  void set_shutdown_callback(base::Closure callback) {
    shutdown_callback_ = callback;
  }
  void set_suspend_callback(base::Closure callback) {
    suspend_callback_ = callback;
  }

  bool suspend_announced() const { return suspend_announced_; }
  uint64_t suspend_wakeup_count() const { return suspend_wakeup_count_; }
  bool suspend_wakeup_count_valid() const {
    return suspend_wakeup_count_valid_;
  }
  const base::TimeDelta& suspend_duration() const { return suspend_duration_; }
  bool suspend_was_successful() const { return suspend_was_successful_; }
  int num_suspend_attempts() const { return num_suspend_attempts_; }

  const std::vector<Suspender::DarkResumeInfo>& dark_resume_wake_durations()
      const {
    return dark_resume_wake_durations_;
  }
  base::TimeDelta last_suspend_duration() const {
    return last_suspend_duration_;
  }

  // Delegate implementation:
  int GetInitialSuspendId() override { return 1; }
  int GetInitialDarkSuspendId() override { return 12701; }

  bool IsLidClosedForSuspend() override { return lid_closed_; }

  bool ReadSuspendWakeupCount(uint64_t* wakeup_count) override {
    if (!report_success_for_read_wakeup_count_)
      return false;
    *wakeup_count = wakeup_count_;
    return true;
  }

  void SetSuspendAnnounced(bool announced) override {
    suspend_announced_ = announced;
  }

  bool GetSuspendAnnounced() override { return suspend_announced_; }

  void PrepareToSuspend() override { AppendAction(kPrepare); }

  SuspendResult DoSuspend(uint64_t wakeup_count,
                          bool wakeup_count_valid,
                          base::TimeDelta duration) override {
    AppendAction(kSuspend);
    suspend_wakeup_count_ = wakeup_count;
    suspend_wakeup_count_valid_ = wakeup_count_valid;
    suspend_duration_ = duration;
    if (clock_)
      clock_->advance_current_boot_time_for_testing(suspend_advance_time_);

    if (suspend_callback_)
      suspend_callback_.Run();

    return suspend_result_;
  }

  void UndoPrepareToSuspend(bool success, int num_suspend_attempts) override {
    AppendAction(kUnprepare);
    suspend_was_successful_ = success;
    num_suspend_attempts_ = num_suspend_attempts;
    if (!completion_callback_.is_null())
      completion_callback_.Run();
  }

  void GenerateDarkResumeMetrics(
      const std::vector<Suspender::DarkResumeInfo>& dark_resume_wake_durations,
      base::TimeDelta suspend_duration) override {
    AppendAction(kGenerateDarkResumeMetrics);
    dark_resume_wake_durations_ = dark_resume_wake_durations;
    last_suspend_duration_ = suspend_duration;
  }

  void ShutDownForFailedSuspend() override {
    AppendAction(kShutDown);
    if (!shutdown_callback_.is_null())
      shutdown_callback_.Run();
  }

  void ShutDownFromSuspend() override {
    AppendAction(kShutDown);
    if (!shutdown_callback_.is_null())
      shutdown_callback_.Run();
  }

 private:
  // Value returned by IsLidClosedForSuspend().
  bool lid_closed_ = false;

  // Should ReadSuspendWakeupCount() and DoSuspend() report success?
  bool report_success_for_read_wakeup_count_ = true;
  SuspendResult suspend_result_ = SuspendResult::SUCCESS;

  // Count that should be returned by ReadSuspendWakeupCount().
  uint64_t wakeup_count_ = 0;

  // Updated by SetSuspendAnnounced() and returned by GetSuspendAnnounced().
  bool suspend_announced_ = false;

  // If non-null, |clock_|'s boot time is advanced by |suspend_advance_time_|
  // each time DoSuspend() is called.
  Clock* clock_ = nullptr;
  base::TimeDelta suspend_advance_time_;

  // Callback to run each time UndoPrepareToSuspend() is called.
  base::Closure completion_callback_;

  // Callback to run each time ShutDown*() is called.
  base::Closure shutdown_callback_;

  // Callback to run each time DoSuspend() is called.
  base::Closure suspend_callback_;

  // Arguments passed to last invocation of DoSuspend().
  uint64_t suspend_wakeup_count_ = 0;
  bool suspend_wakeup_count_valid_ = false;
  base::TimeDelta suspend_duration_;

  // Arguments passed to last invocation of UndoPrepareToSuspend().
  bool suspend_was_successful_ = false;
  int num_suspend_attempts_ = 0;

  // Dark resume wake data provided to GenerateDarkResumeMetrics().
  std::vector<Suspender::DarkResumeInfo> dark_resume_wake_durations_;
  base::TimeDelta last_suspend_duration_;
};

}  // namespace

class SuspenderTest : public testing::Test {
 public:
  SuspenderTest()
      : test_api_(&suspender_),
        pref_retry_delay_ms_(10000),
        pref_num_retries_(10) {}
  SuspenderTest(const SuspenderTest&) = delete;
  SuspenderTest& operator=(const SuspenderTest&) = delete;

 protected:
  void Init() {
    prefs_.SetInt64(kRetrySuspendMsPref, pref_retry_delay_ms_);
    prefs_.SetInt64(kRetrySuspendAttemptsPref, pref_num_retries_);
    test_api_.clock()->set_current_boot_time_for_testing(
        base::TimeTicks() + base::TimeDelta::FromHours(1));
    delegate_.set_clock(test_api_.clock());
    suspender_.Init(&delegate_, &dbus_wrapper_, &dark_resume_,
                    &display_watcher_, &wakeup_source_identifier_,
                    &shutdown_from_suspend_, &prefs_);
  }

  // Returns the ID from a SuspendImminent signal at |position|.
  int GetSuspendImminentId(int position) {
    SuspendImminent proto;
    EXPECT_TRUE(dbus_wrapper_.GetSentSignal(position, kSuspendImminentSignal,
                                            &proto, nullptr));
    return proto.suspend_id();
  }

  // Returns the reason from a SuspendImminent signal at |position|.
  SuspendImminent::Reason GetSuspendImminentReason(int position) {
    SuspendImminent proto;
    EXPECT_TRUE(dbus_wrapper_.GetSentSignal(position, kSuspendImminentSignal,
                                            &proto, nullptr));
    return proto.reason();
  }

  // Returns the ID from a DarkSuspendImminent signal at |position|.
  int GetDarkSuspendImminentId(int position) {
    SuspendImminent proto;
    EXPECT_TRUE(dbus_wrapper_.GetSentSignal(
        position, kDarkSuspendImminentSignal, &proto, nullptr));
    return proto.suspend_id();
  }

  // Returns the ID from a SuspendDone signal at |position|.
  int GetSuspendDoneId(int position) {
    SuspendDone proto;
    EXPECT_TRUE(dbus_wrapper_.GetSentSignal(position, kSuspendDoneSignal,
                                            &proto, nullptr));
    return proto.suspend_id();
  }

  // Announces the readiness of registered delays for a regular suspend request.
  void AnnounceReadyForSuspend(int suspend_request_id) {
    suspender_.OnReadyForSuspend(test_api_.suspend_delay_controller(),
                                 suspend_request_id);
  }

  // Announces the readiness of registered delays for a suspend from dark
  // resume.
  void AnnounceReadyForDarkSuspend(int dark_suspend_id) {
    suspender_.OnReadyForSuspend(test_api_.dark_suspend_delay_controller(),
                                 dark_suspend_id);
  }

  // Records the |last_dark_resume_wake_reason_| in |suspender_|.  Takes a
  // shortcut and sets the member rather than simulating the actual DBus method
  // call handling.
  void RecordDarkResumeWakeReason(const std::string& wake_reason) {
    test_api_.set_last_dark_resume_wake_reason(wake_reason);
  }

  FakePrefs prefs_;
  TestDelegate delegate_;
  system::DBusWrapperStub dbus_wrapper_;
  system::DarkResumeStub dark_resume_;
  system::DisplayWatcherStub display_watcher_;
  system::WakeupSourceIdentifierStub wakeup_source_identifier_;
  policy::ShutdownFromSuspendStub shutdown_from_suspend_;
  Suspender suspender_;
  Suspender::TestApi test_api_;

  int64_t pref_retry_delay_ms_;
  int64_t pref_num_retries_;
};

// Tests the standard suspend/resume cycle.
TEST_F(SuspenderTest, SuspendResume) {
  Init();

  // Suspender shouldn't run powerd_suspend until it receives notice that
  // SuspendDelayController is ready.
  const uint64_t kWakeupCount = 452;
  delegate_.set_wakeup_count(kWakeupCount);
  suspender_.RequestSuspend(SuspendImminent_Reason_IDLE, base::TimeDelta());
  const int suspend_id = test_api_.suspend_id();
  EXPECT_EQ(suspend_id, GetSuspendImminentId(0));
  EXPECT_EQ(SuspendImminent_Reason_IDLE, GetSuspendImminentReason(0));
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_TRUE(delegate_.suspend_announced());

  // Simulate suspending for 20 minutes.
  const base::TimeDelta kDuration = base::TimeDelta::FromMinutes(20);
  delegate_.set_suspend_advance_time(kDuration);

  // Indicate to suspender that input device triggered the wake.
  wakeup_source_identifier_.SetInputDeviceCausedLastWake(false);

  // When Suspender receives notice that the system is ready to be
  // suspended, it should immediately suspend the system.
  dbus_wrapper_.ClearSentSignals();
  AnnounceReadyForSuspend(suspend_id);
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_EQ(kWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());
  EXPECT_TRUE(delegate_.suspend_was_successful());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());

  // A SuspendDone signal should be emitted to announce that the attempt is
  // complete.
  SuspendDone done_proto;
  EXPECT_TRUE(
      dbus_wrapper_.GetSentSignal(0, kSuspendDoneSignal, &done_proto, nullptr));
  EXPECT_EQ(suspend_id, done_proto.suspend_id());
  EXPECT_EQ(kDuration.ToInternalValue(), done_proto.suspend_duration());
  EXPECT_EQ(done_proto.wakeup_type(), SuspendDone_WakeupType_OTHER);
  EXPECT_FALSE(delegate_.suspend_announced());

  // A resuspend timeout shouldn't be set.
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
}

// Tests that Suspender doesn't pass a wakeup count to the delegate when it was
// unable to fetch one.
TEST_F(SuspenderTest, MissingWakeupCount) {
  Init();

  delegate_.set_report_success_for_read_wakeup_count(false);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_wakeup_count_valid());
}

// Tests that calls to RequestSuspend() are ignored when a suspend request is
// already underway.
TEST_F(SuspenderTest, IgnoreDuplicateSuspendRequests) {
  Init();

  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  const int orig_suspend_id = test_api_.suspend_id();

  // The suspend ID should be left unchanged after a second call to
  // RequestSuspend().
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  EXPECT_EQ(orig_suspend_id, test_api_.suspend_id());
}

// Tests that suspend cancel due to wake event from input device is treated as
// successful resume.
TEST_F(SuspenderTest, SuspendCancelDueToInputDeviceWakeEvent) {
  Init();

  const uint64_t kOrigWakeupCount = 46;
  delegate_.set_wakeup_count(kOrigWakeupCount);
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::CANCELED);
  suspender_.RequestSuspend(SuspendImminent_Reason_LID_CLOSED,
                            base::TimeDelta());
  const int suspend_id = test_api_.suspend_id();
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_TRUE(delegate_.suspend_announced());
  wakeup_source_identifier_.SetInputDeviceCausedLastWake(true);
  dbus_wrapper_.ClearSentSignals();
  AnnounceReadyForSuspend(suspend_id);
  EXPECT_EQ(kOrigWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());
  EXPECT_TRUE(delegate_.suspend_was_successful());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());

  // A SuspendDone signal should be emitted to announce that the attempt is
  // complete.
  SuspendDone done_proto;
  EXPECT_TRUE(
      dbus_wrapper_.GetSentSignal(0, kSuspendDoneSignal, &done_proto, nullptr));
  EXPECT_EQ(suspend_id, done_proto.suspend_id());
  EXPECT_EQ(done_proto.wakeup_type(), SuspendDone_WakeupType_INPUT);
  EXPECT_FALSE(delegate_.suspend_announced());

  // A resuspend timeout shouldn't be set.
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
}

// Tests that suspend is retried on failure.
TEST_F(SuspenderTest, RetryOnFailure) {
  Init();

  const uint64_t kOrigWakeupCount = 46;
  delegate_.set_wakeup_count(kOrigWakeupCount);
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::FAILURE);
  suspender_.RequestSuspend(SuspendImminent_Reason_LID_CLOSED,
                            base::TimeDelta());
  const int suspend_id = test_api_.suspend_id();
  EXPECT_EQ(suspend_id, GetSuspendImminentId(0));
  EXPECT_EQ(SuspendImminent_Reason_LID_CLOSED, GetSuspendImminentReason(0));
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_TRUE(delegate_.suspend_announced());

  const uint64_t kRetryWakeupCount = 67;
  delegate_.set_wakeup_count(kRetryWakeupCount);
  dbus_wrapper_.ClearSentSignals();
  AnnounceReadyForSuspend(suspend_id);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  EXPECT_EQ(kOrigWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());

  // The timeout should trigger another suspend attempt.
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  EXPECT_EQ(kRetryWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());

  // A second suspend request should be ignored so we'll avoid trying to
  // re-suspend immediately if an attempt fails while the lid is closed
  // (http://crbug.com/384610). Also check that an external wakeup count passed
  // in the request gets ignored for the eventual retry.
  const uint64_t kExternalWakeupCount = 32542;
  suspender_.RequestSuspendWithExternalWakeupCount(
      SuspendImminent_Reason_IDLE, kExternalWakeupCount, base::TimeDelta());
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());

  // Report success this time and check that the timer isn't running.
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::SUCCESS);
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_NE(kExternalWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_was_successful());
  EXPECT_EQ(3, delegate_.num_suspend_attempts());
  EXPECT_EQ(suspend_id, GetSuspendDoneId(0));
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // Suspend successfully again and check that the number of attempts are
  // reported as 1 now.
  dbus_wrapper_.ClearSentSignals();
  suspender_.RequestSuspend(SuspendImminent_Reason_IDLE, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  const int new_suspend_id = test_api_.suspend_id();
  EXPECT_EQ(new_suspend_id, GetSuspendImminentId(0));
  EXPECT_EQ(SuspendImminent_Reason_IDLE, GetSuspendImminentReason(0));

  dbus_wrapper_.ClearSentSignals();
  AnnounceReadyForSuspend(new_suspend_id);
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_TRUE(delegate_.suspend_was_successful());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());
  EXPECT_EQ(new_suspend_id, GetSuspendDoneId(0));
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
}

// Tests that the system is shut down after repeated suspend failures.
TEST_F(SuspenderTest, ShutDownAfterRepeatedFailures) {
  pref_num_retries_ = 5;
  Init();

  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::FAILURE);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Proceed through all retries, reporting failure each time.
  for (int i = 1; i <= pref_num_retries_ - 1; ++i) {
    EXPECT_TRUE(test_api_.TriggerResuspendTimeout()) << "Retry #" << i;
    EXPECT_EQ(kSuspend, delegate_.GetActions()) << "Retry #" << i;
  }

  // Check that another suspend request doesn't reset the retry count
  // (http://crbug.com/384610).
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // After the last failed attempt, the system should shut down immediately.
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(JoinActions(kSuspend, kShutDown, nullptr), delegate_.GetActions());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
}

// Tests that announcing suspend readiness doesn't trigger a call to Suspend()
// if activity that should cancel the current suspend attempt was previously
// received.
TEST_F(SuspenderTest, CancelBeforeSuspend) {
  Init();

  // User activity should cancel suspending.
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendImminentId(0));
  EXPECT_TRUE(delegate_.suspend_announced());

  suspender_.HandleUserActivity();
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(1));
  EXPECT_FALSE(delegate_.suspend_announced());
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(0, delegate_.num_suspend_attempts());

  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // The lid being opened should also cancel.
  dbus_wrapper_.ClearSentSignals();
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendImminentId(0));
  suspender_.HandleLidOpened();
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(1));
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(0, delegate_.num_suspend_attempts());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // A wake notification should also cancel.
  dbus_wrapper_.ClearSentSignals();
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendImminentId(0));
  suspender_.HandleWakeNotification();
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(1));
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(0, delegate_.num_suspend_attempts());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // The request should also be canceled if the system starts shutting down.
  dbus_wrapper_.ClearSentSignals();
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendImminentId(0));
  suspender_.HandleShutdown();
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(1));
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(0, delegate_.num_suspend_attempts());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // Subsequent requests after shutdown has started should be ignored.
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kNoActions, delegate_.GetActions());
}

// Tests that a suspend-canceling action (user activity) after a failed suspend
// attempt should remove the retry timeout.
TEST_F(SuspenderTest, CancelAfterSuspend) {
  Init();
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::FAILURE);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendImminentId(0));
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Fail a second time.
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // This time, report user activity first, which should cancel the request.
  suspender_.HandleUserActivity();
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(2, delegate_.num_suspend_attempts());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(1));
}

// Tests that Chrome-reported user activity received while suspending with
// a closed lid doesn't abort the suspend attempt (http://crosbug.com/38819).
TEST_F(SuspenderTest, DontCancelForUserActivityWhileLidClosed) {
  delegate_.set_lid_closed(true);
  Init();

  // Report user activity before powerd_suspend is executed and check that
  // Suspender still suspends when suspend readiness is announced.
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  suspender_.HandleUserActivity();
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());

  // Report user activity after powerd_suspend fails and check that the
  // resuspend timer isn't stopped.
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::CANCELED);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  suspender_.HandleUserActivity();

  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::SUCCESS);
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());

  // Report user activity after powerd_suspend fails when the system can safely
  // wake from dark resume and check that the suspend attempt is not aborted.
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::CANCELED);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  suspender_.HandleUserActivity();

  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::SUCCESS);
  AnnounceReadyForDarkSuspend(test_api_.dark_suspend_id());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
}

// Tests that announcing suspend readiness doesn't trigger a call to Suspend()
// if user activity that should cancel the current suspend attempt was
// previously received when the device is docked.
TEST_F(SuspenderTest, CancelForUserActivityWhileDocked) {
  delegate_.set_lid_closed(true);
  Init();
  suspender_.HandleDisplayModeChange(DisplayMode::PRESENTATION);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendImminentId(0));
  EXPECT_TRUE(delegate_.suspend_announced());

  suspender_.HandleUserActivity();
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(1));
  EXPECT_FALSE(delegate_.suspend_announced());
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(0, delegate_.num_suspend_attempts());

  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
}

// Tests that expected wakeup counts passed to
// RequestSuspendWithExternalWakeupCount() are honored.
TEST_F(SuspenderTest, ExternalWakeupCount) {
  Init();

  // Pass a wakeup count less than the one that the delegate returns.
  const uint64_t kWakeupCount = 452;
  delegate_.set_wakeup_count(kWakeupCount);
  suspender_.RequestSuspendWithExternalWakeupCount(
      SuspendImminent_Reason_OTHER, kWakeupCount - 1, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());

  // Make the delegate report that powerd_suspend reported a wakeup count
  // mismatch. Suspender should avoid retrying after a mismatch when using an
  // external wakeup count.
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::CANCELED);
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_EQ(kWakeupCount - 1, delegate_.suspend_wakeup_count());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // Send another suspend request with the current wakeup count. Report failure
  // and check that the suspend attempt is retried using the external wakeup
  // count.
  suspender_.RequestSuspendWithExternalWakeupCount(
      SuspendImminent_Reason_OTHER, kWakeupCount, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::FAILURE);
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_EQ(kWakeupCount, delegate_.suspend_wakeup_count());

  // A retry at this point shouldn't attempt suspend again.
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // Now do a successful suspend and check that another retry isn't scheduled.
  suspender_.RequestSuspendWithExternalWakeupCount(
      SuspendImminent_Reason_OTHER, kWakeupCount, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::SUCCESS);
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_EQ(kWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
}

// Tests that things don't go haywire when
// Suspender::Delegate::UndoPrepareToSuspend() synchronously starts another
// suspend request. Previously, this could result in the new request being
// started before the previous one had completed.
TEST_F(SuspenderTest, EventReceivedWhileHandlingEvent) {
  // Instruct the delegate to send another suspend request when the current one
  // finishes.
  Init();
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendImminentId(0));
  delegate_.set_completion_callback(
      base::Bind(&Suspender::RequestSuspend, base::Unretained(&suspender_),
                 SuspendImminent_Reason_OTHER, base::TimeDelta()));

  // Check that the SuspendDone signal from the first request contains the first
  // request's ID, and that a second request was started immediately.
  dbus_wrapper_.ClearSentSignals();
  const int kOldSuspendId = test_api_.suspend_id();
  AnnounceReadyForSuspend(kOldSuspendId);
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, kPrepare, nullptr),
            delegate_.GetActions());
  EXPECT_EQ(kOldSuspendId, GetSuspendDoneId(0));
  const int kNewSuspendId = test_api_.suspend_id();
  EXPECT_NE(kOldSuspendId, kNewSuspendId);
  EXPECT_EQ(kNewSuspendId, GetSuspendImminentId(1));

  // Don't send additional suspend requests automatically.
  delegate_.set_completion_callback(base::Closure());

  // Finish the second request.
  dbus_wrapper_.ClearSentSignals();
  AnnounceReadyForSuspend(kNewSuspendId);
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_EQ(kNewSuspendId, GetSuspendDoneId(0));
  dbus_wrapper_.ClearSentSignals();

  // Now make the delegate's shutdown method report that the system is shutting
  // down.
  delegate_.set_shutdown_callback(
      base::Bind(&Suspender::HandleShutdown, base::Unretained(&suspender_)));
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  shutdown_from_suspend_.set_action(
      policy::ShutdownFromSuspendInterface::Action::SHUT_DOWN);
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kShutDown, delegate_.GetActions());
}

// Tests that a SuspendDone signal is emitted at startup and the "suspend
// announced" state is cleared if the delegate claims that a previous suspend
// attempt was abandoned after being announced.
TEST_F(SuspenderTest, SendSuspendDoneAtStartupForAbandonedAttempt) {
  delegate_.set_suspend_announced(true);
  Init();
  SuspendDone proto;
  EXPECT_TRUE(
      dbus_wrapper_.GetSentSignal(0, kSuspendDoneSignal, &proto, nullptr));
  EXPECT_EQ(0, proto.suspend_id());
  EXPECT_EQ(base::TimeDelta().ToInternalValue(), proto.suspend_duration());
  EXPECT_FALSE(delegate_.suspend_announced());
}

TEST_F(SuspenderTest, DarkResume) {
  Init();
  const int kWakeupCount = 45;
  delegate_.set_wakeup_count(kWakeupCount);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  const int kSuspendId = test_api_.suspend_id();
  EXPECT_EQ(kSuspendId, GetSuspendImminentId(0));

  shutdown_from_suspend_.set_action(
      policy::ShutdownFromSuspendInterface::Action::SUSPEND);
  dark_resume_.set_in_dark_resume(true);
  dbus_wrapper_.ClearSentSignals();
  AnnounceReadyForSuspend(kSuspendId);

  const int64_t kDarkSuspendId = test_api_.dark_suspend_id();
  EXPECT_EQ(kDarkSuspendId, GetDarkSuspendImminentId(0));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  EXPECT_EQ(kWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());

  // The system should resuspend but without using the wakeup count this time.
  // Make it do a normal resume.
  dark_resume_.set_in_dark_resume(false);
  AnnounceReadyForDarkSuspend(kDarkSuspendId);
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());
  EXPECT_EQ(kSuspendId, GetSuspendDoneId(1));
}

TEST_F(SuspenderTest, DarkResumeShutDown) {
  Init();
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  shutdown_from_suspend_.set_action(
      policy::ShutdownFromSuspendInterface::Action::SHUT_DOWN);
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kShutDown, delegate_.GetActions());
}

TEST_F(SuspenderTest, DarkResumeRetry) {
  pref_num_retries_ = 2;
  Init();
  const uint64_t kOrigWakeupCount = 42;
  delegate_.set_wakeup_count(kOrigWakeupCount);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());

  const uint64_t kDarkWakeupCount = 71;
  delegate_.set_wakeup_count(kDarkWakeupCount);
  shutdown_from_suspend_.set_action(
      policy::ShutdownFromSuspendInterface::Action::SUSPEND);
  dark_resume_.set_in_dark_resume(true);
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  EXPECT_EQ(kOrigWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());

  // Now make two resuspend attempts while in dark resume fail and a third
  // attempt succeed. The successful attempt should reset the retry counter.
  const uint64_t kOnReadyWakeupCount = 102;
  delegate_.set_wakeup_count(kOnReadyWakeupCount);
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::FAILURE);
  AnnounceReadyForDarkSuspend(test_api_.dark_suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // In dark resume, we read the wakeup count after all delays have reported
  // ready.  This is because the wakeup count may naturally increase due to the
  // work that clients do in dark resume and we don't want that to incorrectly
  // make the suspend attempt fail.
  EXPECT_EQ(kOnReadyWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());

  const uint64_t kRetryWakeupCount = 105;
  delegate_.set_wakeup_count(kRetryWakeupCount);
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  EXPECT_EQ(kOnReadyWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());

  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::SUCCESS);
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  EXPECT_EQ(kRetryWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());

  // Fail to resuspend one time short of the retry limit.
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::FAILURE);
  AnnounceReadyForDarkSuspend(test_api_.dark_suspend_id());
  for (int i = 0; i < pref_num_retries_; ++i) {
    SCOPED_TRACE(base::StringPrintf("Attempt #%d", i));
    EXPECT_EQ(kSuspend, delegate_.GetActions());
    EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());
    EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  }

  // The next failure should result in the system shutting down.
  EXPECT_EQ(JoinActions(kSuspend, kShutDown, nullptr), delegate_.GetActions());
}

TEST_F(SuspenderTest, DarkResumeCancelBeforeResuspend) {
  Init();

  shutdown_from_suspend_.set_action(
      policy::ShutdownFromSuspendInterface::Action::SUSPEND);

  // Simulate being in dark resume after each suspend attempt.
  delegate_.set_suspend_callback(base::Bind(
      [](system::DarkResumeStub* dark_resume) {
        dark_resume->set_in_dark_resume(true);
      },
      &dark_resume_));

  // User activity should trigger the transition to fully resumed.
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  suspender_.HandleUserActivity();
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(2));
  EXPECT_FALSE(delegate_.suspend_announced());
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // Clear dark resume state now that the device is transitioned to State::IDLE.
  // This mimics real world behavior.
  dark_resume_.set_in_dark_resume(false);

  // Opening the lid should also trigger the transition.
  dbus_wrapper_.ClearSentSignals();
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  suspender_.HandleLidOpened();
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(2));
  EXPECT_FALSE(delegate_.suspend_announced());
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // Clear dark resume state now that the device is transitioned to State::IDLE.
  // This mimics real world behavior.
  dark_resume_.set_in_dark_resume(false);

  // A wake notification should also trigger the transition.
  dbus_wrapper_.ClearSentSignals();
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  suspender_.HandleWakeNotification();
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(2));
  EXPECT_FALSE(delegate_.suspend_announced());
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // Clear dark resume state now that the device is transitioned to State::IDLE.
  // This mimics real world behavior.
  dark_resume_.set_in_dark_resume(false);

  // Shutting down the system will also trigger the transition so that clients
  // can perform cleanup.
  dbus_wrapper_.ClearSentSignals();
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Dark resume is true at this point.
  suspender_.HandleShutdown();
  EXPECT_EQ(test_api_.suspend_id(), GetSuspendDoneId(2));
  EXPECT_FALSE(delegate_.suspend_announced());
  EXPECT_EQ(kUnprepare, delegate_.GetActions());
  EXPECT_FALSE(delegate_.suspend_was_successful());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
}

// Test that we re-run the registered dark suspend delays if a resuspend attempt
// is canceled due to a wake event but not if the suspend failed for any other
// reason.
TEST_F(SuspenderTest, RerunDarkSuspendDelaysForCanceledSuspend) {
  Init();

  shutdown_from_suspend_.set_action(
      policy::ShutdownFromSuspendInterface::Action::SUSPEND);

  // Simulate being in dark resume after each suspend attempt.
  delegate_.set_suspend_callback(base::Bind(
      [](system::DarkResumeStub* dark_resume) {
        dark_resume->set_in_dark_resume(true);
      },
      &dark_resume_));

  // Do the initial suspend.
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  dbus_wrapper_.ClearSentSignals();

  // The resuspend attempt is canceled due to a wake event.
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::CANCELED);
  AnnounceReadyForDarkSuspend(test_api_.dark_suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  EXPECT_TRUE(dbus_wrapper_.GetSentSignal(0, kDarkSuspendImminentSignal,
                                          nullptr, nullptr));
  dbus_wrapper_.ClearSentSignals();

  // The resuspend attempt fails due to a transient kernel error.
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::FAILURE);
  AnnounceReadyForDarkSuspend(test_api_.dark_suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  EXPECT_EQ(0, dbus_wrapper_.num_sent_signals());

  // The resuspend attempt is finally sucessful. Reset the suspend callback so
  // that dark resume is not set to true after Suspend() runs.
  delegate_.set_suspend_callback(base::Closure());
  dark_resume_.set_in_dark_resume(false);
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::SUCCESS);
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
}

// Test that we report dark resume metrics only if it is enabled.
TEST_F(SuspenderTest, GenerateDarkResumeMetricsOnlyIfEnabled) {
  Init();

  // Don't report metrics when dark resume is disabled.
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(JoinActions(kPrepare, kSuspend, kUnprepare, nullptr),
            delegate_.GetActions());

  // Report metrics when dark resume is enabled.
  dark_resume_.set_enabled(true);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(JoinActions(kPrepare, kSuspend, kUnprepare,
                        kGenerateDarkResumeMetrics, nullptr),
            delegate_.GetActions());
}

// Tests that dark resume wake data is correct when no dark resumes occur.
TEST_F(SuspenderTest, DarkResumeWakeDataNoDarkResume) {
  Init();
  dark_resume_.set_enabled(true);

  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  const base::TimeDelta kSuspendDuration = base::TimeDelta::FromMinutes(24);
  delegate_.set_suspend_advance_time(kSuspendDuration);
  AnnounceReadyForSuspend(test_api_.suspend_id());

  EXPECT_EQ(0, delegate_.dark_resume_wake_durations().size());
  EXPECT_EQ(kSuspendDuration, delegate_.last_suspend_duration());
}

// Tests that dark resume wake data is correct when one dark resume occurs.
TEST_F(SuspenderTest, DarkResumeWakeDataOneDarkResume) {
  Init();
  dark_resume_.set_enabled(true);

  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  dark_resume_.set_in_dark_resume(true);
  const base::TimeDelta kInitialSuspendDuration =
      base::TimeDelta::FromMinutes(7);
  delegate_.set_suspend_advance_time(kInitialSuspendDuration);
  AnnounceReadyForSuspend(test_api_.suspend_id());

  // Simulate the system being briefly awake in the dark resume state.
  const base::TimeDelta kDarkResumeDuration =
      base::TimeDelta::FromMilliseconds(4566);
  test_api_.clock()->advance_current_boot_time_for_testing(kDarkResumeDuration);

  dark_resume_.set_in_dark_resume(false);
  const base::TimeDelta kDarkSuspendDuration = base::TimeDelta::FromMinutes(3);
  delegate_.set_suspend_advance_time(kDarkSuspendDuration);
  AnnounceReadyForDarkSuspend(test_api_.dark_suspend_id());

  ASSERT_EQ(1, delegate_.dark_resume_wake_durations().size());
  EXPECT_STREQ(test_api_.GetDefaultWakeReason().c_str(),
               delegate_.dark_resume_wake_durations().at(0).first.c_str());
  EXPECT_EQ(kDarkResumeDuration,
            delegate_.dark_resume_wake_durations().at(0).second);
  EXPECT_EQ(
      kInitialSuspendDuration + kDarkResumeDuration + kDarkSuspendDuration,
      delegate_.last_suspend_duration());
}

// Tests that dark resume wake data is correct when the initial resuspend fails.
TEST_F(SuspenderTest, DarkResumeWakeDataFailedResuspend) {
  Init();
  dark_resume_.set_enabled(true);

  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  dark_resume_.set_in_dark_resume(true);
  const base::TimeDelta kInitialSuspendDuration =
      base::TimeDelta::FromMinutes(7);
  delegate_.set_suspend_advance_time(kInitialSuspendDuration);
  AnnounceReadyForSuspend(test_api_.suspend_id());

  // Simulate the system being briefly awake in the dark resume state.
  const base::TimeDelta kDarkResumeDuration = base::TimeDelta::FromSeconds(5);
  test_api_.clock()->advance_current_boot_time_for_testing(kDarkResumeDuration);

  // Now simulate a failed resuspend.
  const std::string kWakeReason = "WiFi.Pattern";
  RecordDarkResumeWakeReason(kWakeReason);
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::CANCELED);
  delegate_.set_suspend_advance_time(base::TimeDelta());
  AnnounceReadyForDarkSuspend(test_api_.dark_suspend_id());

  // Finally, resuspend successfully after 10 seconds.
  dark_resume_.set_in_dark_resume(false);
  const base::TimeDelta kResuspendRetryDuration =
      base::TimeDelta::FromSeconds(10);
  test_api_.clock()->advance_current_boot_time_for_testing(
      kResuspendRetryDuration);
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::SUCCESS);
  const base::TimeDelta kResuspendDuration = base::TimeDelta::FromMinutes(3);
  delegate_.set_suspend_advance_time(kResuspendDuration);
  AnnounceReadyForDarkSuspend(test_api_.dark_suspend_id());

  // The wake reason should be reported, and the dark resume wake duration
  // should include both the time in dark resume and the time waiting to
  // resuspend after the failed initial attempt.
  ASSERT_EQ(1, delegate_.dark_resume_wake_durations().size());
  EXPECT_STREQ(kWakeReason.c_str(),
               delegate_.dark_resume_wake_durations().at(0).first.c_str());
  EXPECT_EQ(kDarkResumeDuration + kResuspendRetryDuration,
            delegate_.dark_resume_wake_durations().at(0).second);
  EXPECT_EQ(kInitialSuspendDuration + kDarkResumeDuration +
                kResuspendRetryDuration + kResuspendDuration,
            delegate_.last_suspend_duration());
}

TEST_F(SuspenderTest, ReportInitialSuspendAttempts) {
  Init();
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, base::TimeDelta());
  EXPECT_EQ(kPrepare, delegate_.GetActions());

  // Suspend successfully once and do a dark resume.
  shutdown_from_suspend_.set_action(
      policy::ShutdownFromSuspendInterface::Action::SUSPEND);
  dark_resume_.set_in_dark_resume(true);
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Report failure for the first attempt to resuspend from dark resume; then
  // report success for the second attempt.
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::FAILURE);
  AnnounceReadyForDarkSuspend(test_api_.dark_suspend_id());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  dark_resume_.set_in_dark_resume(false);
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::SUCCESS);
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());

  // Check that the single initial suspend attempt is reported rather than the
  // two attempts that occurred while in dark resume.
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
  EXPECT_TRUE(delegate_.suspend_was_successful());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());
}

// Tests the standard suspend/resume cycle with a wakeup timeout and wakeup
// count.
TEST_F(SuspenderTest, SuspendWakeupTimeout) {
  Init();

  const uint64_t kWakeupCount = 452;
  delegate_.set_wakeup_count(kWakeupCount);
  const base::TimeDelta kDuration = base::TimeDelta::FromSeconds(5);
  suspender_.RequestSuspendWithExternalWakeupCount(SuspendImminent_Reason_OTHER,
                                                   kWakeupCount, kDuration);
  const int suspend_id = test_api_.suspend_id();
  EXPECT_EQ(suspend_id, GetSuspendImminentId(0));
  EXPECT_EQ(SuspendImminent_Reason_OTHER, GetSuspendImminentReason(0));
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_TRUE(delegate_.suspend_announced());

  // Simulate suspending.
  delegate_.set_suspend_advance_time(kDuration);

  // When Suspender receives notice that the system is ready to be
  // suspended, it should immediately suspend the system.
  AnnounceReadyForSuspend(suspend_id);

  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_EQ(kWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());
  EXPECT_TRUE(delegate_.suspend_was_successful());

  // Suspender shall pass a right duration to delegate
  EXPECT_EQ(kDuration, delegate_.suspend_duration());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());

  // A resuspend timeout shouldn't be set.
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
}

// Tests the standard suspend/resume cycle with a wakeup timeout but no wakeup
// count.
TEST_F(SuspenderTest, SuspendWakeupTimeoutNoWakeupCount) {
  Init();

  const base::TimeDelta kDuration = base::TimeDelta::FromSeconds(5);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, kDuration);
  const int suspend_id = test_api_.suspend_id();
  EXPECT_EQ(suspend_id, GetSuspendImminentId(0));
  EXPECT_EQ(SuspendImminent_Reason_OTHER, GetSuspendImminentReason(0));
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_TRUE(delegate_.suspend_announced());

  // Simulate suspending.
  delegate_.set_suspend_advance_time(kDuration);

  // When Suspender receives notice that the system is ready to be
  // suspended, it should immediately suspend the system.
  AnnounceReadyForSuspend(suspend_id);

  EXPECT_EQ(JoinActions(kSuspend, kUnprepare, nullptr), delegate_.GetActions());
  EXPECT_TRUE(delegate_.suspend_was_successful());

  // Suspender shall pass a right duration to delegate
  EXPECT_EQ(kDuration, delegate_.suspend_duration());
  EXPECT_EQ(1, delegate_.num_suspend_attempts());

  // A resuspend timeout shouldn't be set.
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());
}

// Tests that suspend is retried on failure.
TEST_F(SuspenderTest, SuspendWakeupTimoutRetryOnFailure) {
  Init();

  const uint64_t kWakeupCount = 46;
  delegate_.set_wakeup_count(kWakeupCount);
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::FAILURE);
  const base::TimeDelta kDuration = base::TimeDelta::FromSeconds(7);
  suspender_.RequestSuspend(SuspendImminent_Reason_OTHER, kDuration);
  EXPECT_EQ(kPrepare, delegate_.GetActions());
  EXPECT_TRUE(delegate_.suspend_announced());

  AnnounceReadyForSuspend(test_api_.suspend_id());

  // Before the next attempt, suspender should keep the assigned duration
  EXPECT_EQ(kDuration, delegate_.suspend_duration());

  // Simulate a successful suspend attempt then.
  delegate_.set_suspend_advance_time(kDuration);
  delegate_.set_suspend_result(Suspender::Delegate::SuspendResult::SUCCESS);

  // The timeout should trigger another suspend attempt.
  EXPECT_TRUE(test_api_.TriggerResuspendTimeout());
  EXPECT_EQ(kWakeupCount, delegate_.suspend_wakeup_count());
  EXPECT_TRUE(delegate_.suspend_wakeup_count_valid());
  EXPECT_TRUE(delegate_.suspend_was_successful());
  EXPECT_EQ(kDuration, delegate_.suspend_duration());

  // There shall be two attempts.
  EXPECT_EQ(2, delegate_.num_suspend_attempts());

  // A resuspend timeout shouldn't be set.
  EXPECT_FALSE(test_api_.TriggerResuspendTimeout());

  // Simulate another suspend request, validate empty duration passed in
  suspender_.RequestSuspend(SuspendImminent_Reason_IDLE, base::TimeDelta());
  AnnounceReadyForSuspend(test_api_.suspend_id());
  EXPECT_EQ(base::TimeDelta(), delegate_.suspend_duration());
}

}  // namespace policy
}  // namespace power_manager
