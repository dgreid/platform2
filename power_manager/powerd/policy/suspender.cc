// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/suspender.h"

#include <algorithm>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/dbus_sender.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/policy/suspend_delay_controller.h"
#include "power_manager/powerd/system/dark_resume.h"
#include "power_manager/powerd/system/input_watcher.h"
#include "power_manager/proto_bindings/suspend.pb.h"

namespace {
// Default wake reason powerd uses to report wake-reason-specific wake duration
// metrics.
const char kDefaultWakeReason[] = "Other";
}  // namespace

namespace power_manager {
namespace policy {

Suspender::TestApi::TestApi(Suspender* suspender) : suspender_(suspender) {}

void Suspender::TestApi::SetCurrentWallTime(base::Time wall_time) {
  suspender_->clock_->set_current_wall_time_for_testing(wall_time);
}

bool Suspender::TestApi::TriggerResuspendTimeout() {
  if (!suspender_->resuspend_timer_.IsRunning())
    return false;

  suspender_->resuspend_timer_.Stop();
  suspender_->HandleEvent(EVENT_READY_TO_RESUSPEND);
  return true;
}

std::string Suspender::TestApi::GetDefaultWakeReason() const {
  return kDefaultWakeReason;
}

Suspender::Suspender()
    : delegate_(NULL),
      dbus_sender_(NULL),
      dark_resume_(NULL),
      clock_(new Clock),
      state_(STATE_IDLE),
      handling_event_(false),
      processing_queued_events_(false),
      suspend_request_id_(0),
      dark_suspend_id_(0),
      suspend_request_supplied_wakeup_count_(false),
      suspend_request_wakeup_count_(0),
      wakeup_count_(0),
      wakeup_count_valid_(false),
      max_retries_(0),
      current_num_attempts_(0),
      initial_num_attempts_(0),
      last_dark_resume_wake_reason_(kDefaultWakeReason) {}

Suspender::~Suspender() {
}

void Suspender::Init(Delegate* delegate,
                     DBusSenderInterface* dbus_sender,
                     system::DarkResumeInterface *dark_resume,
                     PrefsInterface* prefs) {
  delegate_ = delegate;
  dbus_sender_ = dbus_sender;
  dark_resume_ = dark_resume;

  const int initial_id = delegate_->GetInitialSuspendId();
  suspend_request_id_ = initial_id - 1;
  suspend_delay_controller_.reset(new SuspendDelayController(initial_id, ""));
  suspend_delay_controller_->AddObserver(this);

  const int initial_dark_id = delegate_->GetInitialDarkSuspendId();
  dark_suspend_id_ = initial_dark_id - 1;
  dark_suspend_delay_controller_.reset(
      new SuspendDelayController(initial_dark_id, "dark"));
  dark_suspend_delay_controller_->AddObserver(this);

  int64_t retry_delay_ms = 0;
  CHECK(prefs->GetInt64(kRetrySuspendMsPref, &retry_delay_ms));
  retry_delay_ = base::TimeDelta::FromMilliseconds(retry_delay_ms);

  CHECK(prefs->GetInt64(kRetrySuspendAttemptsPref, &max_retries_));

  // Clean up if powerd was previously restarted after emitting SuspendImminent
  // but before emitting SuspendDone.
  if (delegate_->GetSuspendAnnounced()) {
    LOG(INFO) << "Previous run exited mid-suspend; emitting SuspendDone";
    EmitSuspendDoneSignal(0, base::TimeDelta());
    delegate_->SetSuspendAnnounced(false);
  }
}

void Suspender::RequestSuspend() {
  suspend_request_supplied_wakeup_count_ = false;
  suspend_request_wakeup_count_ = 0;
  HandleEvent(EVENT_SUSPEND_REQUESTED);
}

void Suspender::RequestSuspendWithExternalWakeupCount(uint64_t wakeup_count) {
  suspend_request_supplied_wakeup_count_ = true;
  suspend_request_wakeup_count_ = wakeup_count;
  HandleEvent(EVENT_SUSPEND_REQUESTED);
}

void Suspender::RegisterSuspendDelay(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  RegisterSuspendDelayInternal(
      suspend_delay_controller_.get(), method_call, response_sender);
}

void Suspender::UnregisterSuspendDelay(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  UnregisterSuspendDelayInternal(
      suspend_delay_controller_.get(), method_call, response_sender);
}

void Suspender::HandleSuspendReadiness(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  HandleSuspendReadinessInternal(
      suspend_delay_controller_.get(), method_call, response_sender);
}

void Suspender::RegisterDarkSuspendDelay(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  RegisterSuspendDelayInternal(
      dark_suspend_delay_controller_.get(), method_call, response_sender);
}

void Suspender::UnregisterDarkSuspendDelay(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  UnregisterSuspendDelayInternal(
      dark_suspend_delay_controller_.get(), method_call, response_sender);
}

void Suspender::HandleDarkSuspendReadiness(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  HandleSuspendReadinessInternal(
      dark_suspend_delay_controller_.get(), method_call, response_sender);
}

// Daemons that want powerd to log the wake duration metrics for the current
// dark resume to a wake-reason-specific histogram should send the wake reason
// to powerd during that dark resume.
//
// This string should take the form $SUBSYSTEM.$REASON, where $SUBSYSTEM refers
// to the subsystem that caused the wake, and $REASON is the specific reason for
// the subsystem waking the system. For example, the wake reason
// "WiFi.Disconnect" should be passed to this function to indicate that the WiFi
// subsystem woke the system in dark resume because of disconnection from an AP.
//
// Note: If multiple daemons send wake reason to powerd during the same dark
// resume, a race condition will be created, and only the last histogram name
// reported to powerd will be used to log wake-reason-specific wake duration
// metrics for that dark resume. Daemons using this function should coordinate
// with each other to ensure that no more than one wake reason is reported to
// powerd per dark resume.
void Suspender::RecordDarkResumeWakeReason(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  bool overwriting_wake_reason = false;
  std::string old_wake_reason;
  if (last_dark_resume_wake_reason_ != kDefaultWakeReason) {
    overwriting_wake_reason = true;
    old_wake_reason = last_dark_resume_wake_reason_;
  }
  DarkResumeWakeReason proto;
  dbus::MessageReader reader(method_call);
  if (!reader.PopArrayOfBytesAsProto(&proto)) {
    LOG(ERROR) << "Unable to parse " << method_call->GetMember() << " request";
    response_sender.Run(
        scoped_ptr<dbus::Response>(dbus::ErrorResponse::FromMethodCall(
            method_call, DBUS_ERROR_INVALID_ARGS,
            "Expected wake reason proto")));
    return;
  }
  last_dark_resume_wake_reason_ = proto.wake_reason();
  if (overwriting_wake_reason) {
    LOG(WARNING) << "Overwrote existing dark resume wake reason "
                 << old_wake_reason << " with wake reason "
                 << last_dark_resume_wake_reason_;
  }
  response_sender.Run(dbus::Response::FromMethodCall(method_call));
}

void Suspender::HandleLidOpened() {
  HandleEvent(EVENT_USER_ACTIVITY);
}

void Suspender::HandleUserActivity() {
  HandleEvent(EVENT_USER_ACTIVITY);
}

void Suspender::HandleShutdown() {
  HandleEvent(EVENT_SHUTDOWN_STARTED);
}

void Suspender::HandleDBusNameOwnerChanged(const std::string& name,
                                           const std::string& old_owner,
                                           const std::string& new_owner) {
  if (new_owner.empty()) {
    suspend_delay_controller_->HandleDBusClientDisconnected(name);
    dark_suspend_delay_controller_->HandleDBusClientDisconnected(name);
  }
}

void Suspender::OnReadyForSuspend(SuspendDelayController* controller,
                                  int suspend_id) {
  if (controller == suspend_delay_controller_.get() &&
      suspend_id == suspend_request_id_) {
    HandleEvent(EVENT_SUSPEND_DELAYS_READY);
  } else if (controller == dark_suspend_delay_controller_.get() &&
             suspend_id == dark_suspend_id_) {
    // Since we are going to be spending more time in dark resume, the
    // probability of the user interacting with the system in the middle of one
    // is much higher.  If this happens before all dark resume clients report
    // ready, then we will find out from Chrome, which will call our
    // HandleUserActivity method.  If this happens after all clients are ready,
    // then we need the kernel to cancel the suspend by providing it a wakeup
    // count at the point of the suspend.  We read the wakeup count now rather
    // than at the start of the attempt because network activity will count as a
    // wakeup event and we don't want the work that clients did during the dark
    // resume to accidentally cancel the suspend.
    if (!suspend_request_supplied_wakeup_count_)
      wakeup_count_valid_ = delegate_->ReadSuspendWakeupCount(&wakeup_count_);

    HandleEvent(EVENT_READY_TO_RESUSPEND);
  }
}

void Suspender::RegisterSuspendDelayInternal(
    SuspendDelayController *controller,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  RegisterSuspendDelayRequest request;
  dbus::MessageReader reader(method_call);
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse " << method_call->GetMember() << " request";
    response_sender.Run(scoped_ptr<dbus::Response>(
        dbus::ErrorResponse::FromMethodCall(method_call,
            DBUS_ERROR_INVALID_ARGS, "Expected serialized protocol buffer")));
    return;
  }
  RegisterSuspendDelayReply reply_proto;
  controller->RegisterSuspendDelay(
      request, method_call->GetSender(), &reply_proto);

  scoped_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendProtoAsArrayOfBytes(reply_proto);
  response_sender.Run(response.Pass());
}

void Suspender::UnregisterSuspendDelayInternal(
    SuspendDelayController *controller,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  UnregisterSuspendDelayRequest request;
  dbus::MessageReader reader(method_call);
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse " << method_call->GetMember() << " request";
    response_sender.Run(scoped_ptr<dbus::Response>(
        dbus::ErrorResponse::FromMethodCall(method_call,
            DBUS_ERROR_INVALID_ARGS, "Expected serialized protocol buffer")));
    return;
  }
  controller->UnregisterSuspendDelay(request, method_call->GetSender());
  response_sender.Run(dbus::Response::FromMethodCall(method_call));
}

void Suspender::HandleSuspendReadinessInternal(
    SuspendDelayController *controller,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  SuspendReadinessInfo info;
  dbus::MessageReader reader(method_call);
  if (!reader.PopArrayOfBytesAsProto(&info)) {
    LOG(ERROR) << "Unable to parse " << method_call->GetMember() << " request";
    response_sender.Run(scoped_ptr<dbus::Response>(
        dbus::ErrorResponse::FromMethodCall(method_call,
            DBUS_ERROR_INVALID_ARGS, "Expected serialized protocol buffer")));
    return;
  }
  controller->HandleSuspendReadiness(info, method_call->GetSender());
  response_sender.Run(dbus::Response::FromMethodCall(method_call));
}

void Suspender::HandleEvent(Event event) {
  // If a new event is received while handling an event, save it for later. This
  // can happen when e.g. |delegate_|'s UndoPrepareToSuspend() method attempts
  // to resuspend or ShutDownFor*() calls HandleShutdown().
  if (handling_event_) {
    queued_events_.push(event);
    return;
  }

  handling_event_ = true;

  switch (state_) {
    case STATE_IDLE:
      switch (event) {
        case EVENT_SUSPEND_REQUESTED:
          StartRequest();
          state_ = STATE_WAITING_FOR_SUSPEND_DELAYS;
          break;
        case EVENT_SHUTDOWN_STARTED:
          state_ = STATE_SHUTTING_DOWN;
          break;
        default:
          break;
      }
      break;
    // These two states are identical apart from the event that triggers the
    // call to Suspend().
    case STATE_WAITING_FOR_SUSPEND_DELAYS:
    case STATE_WAITING_TO_RESUSPEND:
      switch (event) {
        case EVENT_SUSPEND_DELAYS_READY:
          if (state_ == STATE_WAITING_FOR_SUSPEND_DELAYS)
            state_ = Suspend();
          break;
        case EVENT_READY_TO_RESUSPEND:
          if (state_ == STATE_WAITING_TO_RESUSPEND)
            state_ = Suspend();
          break;
        case EVENT_USER_ACTIVITY:
          // Avoid canceling suspend for errant touchpad, power button, etc.
          // events that can be generated by closing the lid.
          if (delegate_->IsLidClosedForSuspend())
            break;

          // We ignore any user activity in dark resume if the system doesn't
          // have the ability to properly transition from dark resume to fully
          // resumed.
          if (state_ == STATE_WAITING_FOR_SUSPEND_DELAYS ||
              !dark_resume_->InDarkResume() ||
              delegate_->CanSafelyExitDarkResume()) {
            LOG(INFO) << "Aborting request in response to user activity";
            FinishRequest(false);
            state_ = STATE_IDLE;
          }
          break;
        case EVENT_SHUTDOWN_STARTED:
          if (state_ == STATE_WAITING_FOR_SUSPEND_DELAYS ||
              !dark_resume_->InDarkResume() ||
              delegate_->CanSafelyExitDarkResume())
            FinishRequest(false);
          state_ = STATE_SHUTTING_DOWN;
          break;
        default:
          break;
      }
      break;
    case STATE_SHUTTING_DOWN:
      break;
  }

  handling_event_ = false;

  // Let the outermost invocation of HandleEvent() deal with the queue.
  if (processing_queued_events_)
    return;

  // Pass queued events back into HandleEvent() one at a time.
  processing_queued_events_ = true;
  while (!queued_events_.empty()) {
    Event event = queued_events_.front();
    queued_events_.pop();
    HandleEvent(event);
  }
  processing_queued_events_ = false;
}

void Suspender::StartRequest() {
  suspend_request_id_++;
  LOG(INFO) << "Starting request " << suspend_request_id_;

  if (suspend_request_supplied_wakeup_count_) {
    wakeup_count_ = suspend_request_wakeup_count_;
    wakeup_count_valid_ = true;
  } else {
    wakeup_count_valid_ = delegate_->ReadSuspendWakeupCount(&wakeup_count_);
  }

  suspend_request_start_time_ = clock_->GetCurrentWallTime();
  current_num_attempts_ = 0;
  initial_num_attempts_ = 0;

  dark_resume_wake_durations_.clear();
  last_dark_resume_wake_reason_ = kDefaultWakeReason;

  // Call PrepareToSuspend() before emitting SuspendImminent -- powerd needs to
  // set the backlight level to 0 before Chrome turns the display on in response
  // to the signal.
  delegate_->PrepareToSuspend();
  suspend_delay_controller_->PrepareForSuspend(suspend_request_id_);
  dark_resume_->PrepareForSuspendRequest();
  delegate_->SetSuspendAnnounced(true);
  EmitSuspendImminentSignal(suspend_request_id_);
}

void Suspender::FinishRequest(bool success) {
  LOG(INFO) << "Finishing request " << suspend_request_id_ << " "
            << (success ? "" : "un") << "successfully";
  resuspend_timer_.Stop();
  suspend_delay_controller_->FinishSuspend(suspend_request_id_);
  dark_suspend_delay_controller_->FinishSuspend(dark_suspend_id_);
  base::TimeDelta suspend_duration = std::max(base::TimeDelta(),
      clock_->GetCurrentWallTime() - suspend_request_start_time_);
  EmitSuspendDoneSignal(suspend_request_id_, suspend_duration);
  delegate_->SetSuspendAnnounced(false);
  delegate_->UndoPrepareToSuspend(success,
      initial_num_attempts_ ? initial_num_attempts_ : current_num_attempts_,
      dark_resume_->InDarkResume());

  // Only report dark resume metrics if it is actually enabled to prevent a
  // bunch of noise in the data.
  if (dark_resume_->IsEnabled()) {
    delegate_->GenerateDarkResumeMetrics(dark_resume_wake_durations_,
                                         suspend_duration);
  }
  dark_resume_->UndoPrepareForSuspendRequest();
}

Suspender::State Suspender::Suspend() {
  system::DarkResumeInterface::Action action;
  base::TimeDelta duration;
  dark_resume_->GetActionForSuspendAttempt(&action, &duration);
  switch (action) {
    case system::DarkResumeInterface::SHUT_DOWN:
      LOG(INFO) << "Shutting down from dark resume";
      // Don't call FinishRequest(); we want the backlight to stay off.
      delegate_->ShutDownForDarkResume();
      return STATE_SHUTTING_DOWN;
    case system::DarkResumeInterface::SUSPEND:
      if (duration != base::TimeDelta())
        LOG(INFO) << "Suspending for " << duration.InSeconds() << " seconds";
      break;
    default:
      NOTREACHED() << "Unhandled dark resume action " << action;
  }

  // Note: If this log message is changed, the power_AudioDetector test
  // must be updated.
  LOG(INFO) << "Starting suspend";

  // Measuring time spent in dark resume is a little tricky because the
  // resuspend attempt might fail and we wouldn't know until after the call to
  // delegate_->DoSuspend() returns.  To deal with this, we split up counting
  // the number of wakeups from measuring the time spent during that wakeup.
  // Whenever the system enters dark resume and the previous suspend attempt was
  // successful, we push a new dummy value to the end of
  // |dark_resume_wake_durations_| and mark the time at which the dark resume
  // started in |dark_resume_start_time_|.  When the system is about to
  // resuspend, we calculate the difference between the current time and
  // |dark_resume_start_time_| and store it in the last element of
  // |dark_resume_wake_durations_|.
  //
  // If the resuspend is successful and we are still in dark resume, then a new
  // dummy value is pushed to the end of |dark_resume_wake_durations_|.
  // However, if the resuspend fails or is canceled then we schedule another
  // resuspend and when we are about to perform the second resuspend attempt we
  // again calculate the difference between the current time and
  // |dark_resume_start_time_|, storing it in the last element of
  // |dark_resume_wake_durations_|.  This overwrites the wake duration that was
  // stored there before the first unsuccessful resuspend.  Additional
  // unsuccessful attempts trigger further overwrites until an attempt is
  // successful, ensuring that we account for all the time spent in dark resume
  // during a particular wake.
  if (!dark_resume_wake_durations_.empty()) {
    dark_resume_wake_durations_.back().first =
        last_dark_resume_wake_reason_;
    dark_resume_wake_durations_.back().second =
        std::max(base::TimeDelta(),
                 clock_->GetCurrentWallTime() - dark_resume_start_time_);
  }
  current_num_attempts_++;
  const Delegate::SuspendResult result =
      delegate_->DoSuspend(wakeup_count_, wakeup_count_valid_, duration);

  if (result == Delegate::SUSPEND_SUCCESSFUL)
    dark_resume_->HandleSuccessfulResume();

  // At this point, we've either resumed successfully or failed to suspend in
  // the first place.
  const bool in_dark_resume = dark_resume_->InDarkResume();

  // We first deal with the common case: the suspend was successful and we have
  // fully resumed.  We also check if an external wakeup count was provided and
  // the suspend attempt failed due to a wakeup count mismatch -- this indicates
  // that a test probably triggered this suspend attempt after setting a wake
  // alarm, and if we retry later, it's likely that the alarm will have already
  // fired and the system will never wake up.
  if (!in_dark_resume &&
      ((result == Delegate::SUSPEND_SUCCESSFUL) ||
       (result == Delegate::SUSPEND_CANCELED &&
        suspend_request_supplied_wakeup_count_))) {
    FinishRequest(result == Delegate::SUSPEND_SUCCESSFUL);
    return STATE_IDLE;
  }

  // If we reach this point there are three possibilities: the suspend attempt
  // was successful but we are in dark resume, the suspend attempt was canceled
  // due to a wakeup count mismatch, or the suspend attempt failed due to some
  // (hopefully) transient kernel error.  We will deal with the first two cases
  // here.  In either case, we want to announce the resuspend attempt to the
  // registered listeners on devices that support this.  One or more of the
  // listeners may have to do some work in response to the wake event and we
  // should ensure that these listeners are given the time they need.  If it
  // turns out that the wake event was triggered by the user, then chrome will
  // send us a user activity message, which will abort the suspend request
  // entirely and, if we are in dark resume, will trigger a transition to fully
  // resumed (on devices that support this).
  if ((in_dark_resume && result == Delegate::SUSPEND_SUCCESSFUL) ||
      (result == Delegate::SUSPEND_CANCELED &&
       current_num_attempts_ <= max_retries_)) {
    // Save the first run's number of attempts so it can be reported later.
    if (in_dark_resume && !initial_num_attempts_)
      initial_num_attempts_ = current_num_attempts_;

    dark_suspend_id_++;

    if (result == Delegate::SUSPEND_SUCCESSFUL) {
      // This is the start of a new dark resume wake.
      dark_resume_start_time_ = clock_->GetCurrentWallTime();
      dark_resume_wake_durations_.push_back(
          DarkResumeInfo(kDefaultWakeReason, base::TimeDelta()));
      last_dark_resume_wake_reason_ = kDefaultWakeReason;

      // We only reset the retry count if the suspend was successful.
      current_num_attempts_ = 0;
    } else {
      LOG(WARNING) << "Suspend attempt #" << current_num_attempts_
                   << " canceled due to wake event";
    }

    // We don't want to emit a DarkSuspendImminent on devices with older kernels
    // because they probably don't have the hardware support to do any useful
    // work in dark resume anyway.
    if (delegate_->CanSafelyExitDarkResume()) {
      LOG(INFO) << "Notifying registered dark suspend delays about "
                << dark_suspend_id_;
      dark_suspend_delay_controller_->PrepareForSuspend(dark_suspend_id_);
      EmitDarkSuspendImminentSignal(dark_suspend_id_);
    } else {
      wakeup_count_ = 0;
      wakeup_count_valid_ = false;
      ScheduleResuspend(result == Delegate::SUSPEND_SUCCESSFUL
                            ? base::TimeDelta()
                            : retry_delay_);
    }

    return STATE_WAITING_TO_RESUSPEND;
  }

  // If we make it here then the suspend attempt failed due to some kernel error
  // or we have exceeded the maximum number of retries.  If the number of
  // suspend attempts _has_ exceeded |max_retries_|, then we shut down.
  // Otherwise we reschedule another attempt in |retry_delay_|.
  if (current_num_attempts_ > max_retries_) {
    LOG(ERROR) << "Unsuccessfully attempted to suspend "
               << current_num_attempts_ << " times; shutting down";
    // Don't call FinishRequest(); we want the backlight to stay off.
    delegate_->ShutDownForFailedSuspend();
    return STATE_SHUTTING_DOWN;
  }

  LOG(WARNING) << "Suspend attempt #" << current_num_attempts_ << " failed; "
               << "will retry in " << retry_delay_.InMilliseconds() << " ms";
  if (!suspend_request_supplied_wakeup_count_)
    wakeup_count_valid_ = delegate_->ReadSuspendWakeupCount(&wakeup_count_);
  ScheduleResuspend(retry_delay_);
  return STATE_WAITING_TO_RESUSPEND;
}

void Suspender::ScheduleResuspend(const base::TimeDelta& delay) {
  resuspend_timer_.Start(FROM_HERE, delay,
      base::Bind(&Suspender::HandleEvent, base::Unretained(this),
                 EVENT_READY_TO_RESUSPEND));
}

void Suspender::EmitSuspendImminentSignal(int suspend_request_id) {
  SuspendImminent proto;
  proto.set_suspend_id(suspend_request_id);
  dbus_sender_->EmitSignalWithProtocolBuffer(kSuspendImminentSignal, proto);
}

void Suspender::EmitSuspendDoneSignal(int suspend_request_id,
                                      const base::TimeDelta& suspend_duration) {
  SuspendDone proto;
  proto.set_suspend_id(suspend_request_id);
  proto.set_suspend_duration(suspend_duration.ToInternalValue());
  dbus_sender_->EmitSignalWithProtocolBuffer(kSuspendDoneSignal, proto);
}

void Suspender::EmitDarkSuspendImminentSignal(int dark_suspend_id) {
  SuspendImminent proto;
  proto.set_suspend_id(dark_suspend_id);
  dbus_sender_->EmitSignalWithProtocolBuffer(kDarkSuspendImminentSignal, proto);
}

}  // namespace policy
}  // namespace power_manager
