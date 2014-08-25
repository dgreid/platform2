// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/suspender.h"

#include <algorithm>
#include <string>

#include <base/file_util.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/dbus_sender.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/policy/suspend_delay_controller.h"
#include "power_manager/powerd/system/dark_resume.h"
#include "power_manager/powerd/system/input.h"
#include "power_manager/proto_bindings/suspend.pb.h"

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
      initial_num_attempts_(0) {
}

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
  suspend_delay_controller_.reset(new SuspendDelayController(initial_id));
  suspend_delay_controller_->AddObserver(this);

  const int initial_dark_id = delegate_->GetInitialDarkSuspendId();
  dark_suspend_id_ = initial_dark_id - 1;
  dark_suspend_delay_controller_.reset(
      new SuspendDelayController(initial_dark_id));
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

void Suspender::HandleLidOpened() {
  HandleEvent(EVENT_USER_ACTIVITY);
}

void Suspender::HandleUserActivity() {
  // Avoid canceling suspend for errant touchpad, power button, etc. events
  // that can be generated by closing the lid.
  if (!delegate_->IsLidClosedForSuspend())
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
          // We ignore any user activity in dark resume if the system doesn't
          // have the ability to properly transition from dark resume to fully
          // resumed.
          if (state_ == STATE_WAITING_FOR_SUSPEND_DELAYS ||
              !dark_resume_->InDarkResume() ||
              delegate_->CanSafelyExitDarkResume()) {
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
  if (suspend_request_supplied_wakeup_count_) {
    wakeup_count_ = suspend_request_wakeup_count_;
    wakeup_count_valid_ = true;
  } else {
    wakeup_count_valid_ = delegate_->ReadSuspendWakeupCount(&wakeup_count_);
  }

  suspend_request_id_++;
  suspend_request_start_time_ = clock_->GetCurrentWallTime();
  current_num_attempts_ = 0;
  initial_num_attempts_ = 0;

  // Call PrepareToSuspend() before emitting SuspendImminent -- powerd needs to
  // set the backlight level to 0 before Chrome turns the display on in response
  // to the signal.
  delegate_->PrepareToSuspend();
  suspend_delay_controller_->PrepareForSuspend(suspend_request_id_);
  delegate_->SetSuspendAnnounced(true);
  EmitSuspendImminentSignal(suspend_request_id_);
}

void Suspender::FinishRequest(bool success) {
  resuspend_timer_.Stop();
  base::TimeDelta suspend_duration = std::max(base::TimeDelta(),
      clock_->GetCurrentWallTime() - suspend_request_start_time_);
  EmitSuspendDoneSignal(suspend_request_id_, suspend_duration);
  delegate_->SetSuspendAnnounced(false);
  delegate_->UndoPrepareToSuspend(success,
      initial_num_attempts_ ? initial_num_attempts_ : current_num_attempts_,
      dark_resume_->InDarkResume());
}

Suspender::State Suspender::Suspend() {
  system::DarkResumeInterface::Action action;
  base::TimeDelta duration;
  dark_resume_->PrepareForSuspendAttempt(&action, &duration);
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
  current_num_attempts_++;
  const Delegate::SuspendResult result =
      delegate_->DoSuspend(wakeup_count_, wakeup_count_valid_, duration);

  // At this point, we've either resumed successfully or failed to suspend in
  // the first place.
  const bool in_dark_resume = dark_resume_->InDarkResume();
  if (in_dark_resume && result == Delegate::SUSPEND_SUCCESSFUL) {
    // Save the first run's number of attempts so it can be reported later.
    if (!initial_num_attempts_)
      initial_num_attempts_ = current_num_attempts_;

    dark_suspend_id_++;
    current_num_attempts_ = 0;

    // We don't want to emit a DarkSuspendImminent on devices with older kernels
    // because they probably don't have the hardware support to do any useful
    // work in dark resume anyway.
    if (delegate_->CanSafelyExitDarkResume()) {
      dark_suspend_delay_controller_->PrepareForSuspend(dark_suspend_id_);
      EmitDarkSuspendImminentSignal(dark_suspend_id_);
    } else {
      wakeup_count_ = 0;
      wakeup_count_valid_ = false;
      ScheduleResuspend(base::TimeDelta());
    }

    return STATE_WAITING_TO_RESUSPEND;
  }

  // Don't retry if an external wakeup count was supplied and the suspend
  // attempt failed due to a wakeup count mismatch -- a test probably triggered
  // this suspend attempt after setting a wake alarm, and if we retry later,
  // it's likely that the alarm will have already fired and the system will
  // never wake up.
  if ((result == Delegate::SUSPEND_SUCCESSFUL) ||
      (result == Delegate::SUSPEND_CANCELED &&
       suspend_request_supplied_wakeup_count_ && !in_dark_resume)) {
    FinishRequest(result == Delegate::SUSPEND_SUCCESSFUL);
    return STATE_IDLE;
  }

  // TODO(chirantan): The suspend attempt may have been canceled during dark
  // resume due to the arrival of more packets from the network.  In this case,
  // we want to emit another DarkSuspendImminent signal rather than just
  // retrying the suspend.  Once we have support to distinguish this case in the
  // kernel, we should add a check here to potentially re-run the dark suspend
  // delays.

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
