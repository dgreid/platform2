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
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/policy/shutdown_from_suspend_interface.h"
#include "power_manager/powerd/policy/suspend_delay_controller.h"
#include "power_manager/powerd/system/dark_resume_interface.h"
#include "power_manager/powerd/system/dbus_wrapper.h"
#include "power_manager/powerd/system/display/display_watcher.h"
#include "power_manager/powerd/system/input_watcher.h"
#include "power_manager/powerd/system/wakeup_source_identifier.h"
#include "power_manager/proto_bindings/suspend.pb.h"

namespace {
// Default wake reason powerd uses to report wake-reason-specific wake duration
// metrics.
const char kDefaultWakeReason[] = "Other";
}  // namespace

namespace power_manager {
namespace policy {

Suspender::TestApi::TestApi(Suspender* suspender) : suspender_(suspender) {}

bool Suspender::TestApi::TriggerResuspendTimeout() {
  if (!suspender_->resuspend_timer_.IsRunning())
    return false;

  suspender_->resuspend_timer_.Stop();
  suspender_->HandleEvent(Event::READY_TO_RESUSPEND);
  return true;
}

std::string Suspender::TestApi::GetDefaultWakeReason() const {
  return kDefaultWakeReason;
}

Suspender::Suspender()
    : clock_(std::make_unique<Clock>()),
      last_dark_resume_wake_reason_(kDefaultWakeReason),
      weak_ptr_factory_(this) {}

Suspender::~Suspender() = default;

void Suspender::Init(
    Delegate* delegate,
    system::DBusWrapperInterface* dbus_wrapper,
    system::DarkResumeInterface* dark_resume,
    system::DisplayWatcherInterface* display_watcher,
    system::WakeupSourceIdentifierInterface* wakeup_source_identifier,
    policy::ShutdownFromSuspendInterface* shutdown_from_suspend,
    PrefsInterface* prefs) {
  delegate_ = delegate;
  dbus_wrapper_ = dbus_wrapper;
  dark_resume_ = dark_resume;
  wakeup_source_identifier_ = wakeup_source_identifier;
  shutdown_from_suspend_ = shutdown_from_suspend;

  const int initial_id = delegate_->GetInitialSuspendId();
  suspend_request_id_ = initial_id - 1;
  suspend_delay_controller_.reset(new SuspendDelayController(
      initial_id, "", SuspendDelayController::kDefaultMaxSuspendDelayTimeout));
  suspend_delay_controller_->AddObserver(this);

  // Default dark suspend delay same as regular suspend timeout if the pref
  // isn't provided.
  base::TimeDelta max_dark_suspend_delay_timeout;
  int64_t max_dark_suspend_delay_timeout_ms;
  if (prefs->GetInt64(kMaxDarkSuspendDelayTimeoutMsPref,
                      &max_dark_suspend_delay_timeout_ms)) {
    max_dark_suspend_delay_timeout =
        base::TimeDelta::FromMilliseconds(max_dark_suspend_delay_timeout_ms);
  } else {
    max_dark_suspend_delay_timeout =
        SuspendDelayController::kDefaultMaxSuspendDelayTimeout;
  }
  const int initial_dark_id = delegate_->GetInitialDarkSuspendId();
  dark_suspend_id_ = initial_dark_id - 1;
  dark_suspend_delay_controller_.reset(new SuspendDelayController(
      initial_dark_id, "dark", max_dark_suspend_delay_timeout));
  dark_suspend_delay_controller_->AddObserver(this);

  display_watcher->AddObserver(this);
  int64_t retry_delay_ms = 0;
  CHECK(prefs->GetInt64(kRetrySuspendMsPref, &retry_delay_ms));
  retry_delay_ = base::TimeDelta::FromMilliseconds(retry_delay_ms);

  CHECK(prefs->GetInt64(kRetrySuspendAttemptsPref, &max_retries_));

  ExportDBusMethods();

  // Clean up if powerd was previously restarted after emitting SuspendImminent
  // but before emitting SuspendDone.
  if (delegate_->GetSuspendAnnounced()) {
    LOG(INFO) << "Previous run exited mid-suspend; emitting SuspendDone";
    EmitSuspendDoneSignal(0, base::TimeDelta(),
                          SuspendDone_WakeupType_NOT_APPLICABLE);
    delegate_->SetSuspendAnnounced(false);
  }
}

void Suspender::RequestSuspend(SuspendImminent::Reason reason,
                               base::TimeDelta duration) {
  suspend_request_reason_ = reason;
  suspend_request_supplied_wakeup_count_ = false;
  suspend_request_wakeup_count_ = 0;
  suspend_duration_ = duration;
  HandleEvent(Event::SUSPEND_REQUESTED);
}

void Suspender::RequestSuspendWithExternalWakeupCount(
    SuspendImminent::Reason reason,
    uint64_t wakeup_count,
    base::TimeDelta duration) {
  suspend_request_reason_ = reason;
  suspend_request_supplied_wakeup_count_ = true;
  suspend_request_wakeup_count_ = wakeup_count;
  suspend_duration_ = duration;
  HandleEvent(Event::SUSPEND_REQUESTED);
}

void Suspender::HandleLidOpened() {
  HandleEvent(Event::USER_ACTIVITY);
}

void Suspender::HandleUserActivity() {
  HandleEvent(Event::USER_ACTIVITY);
}

void Suspender::HandleWakeNotification() {
  HandleEvent(Event::WAKE_NOTIFICATION);
}

void Suspender::HandleShutdown() {
  HandleEvent(Event::SHUTDOWN_STARTED);
}

void Suspender::HandleDisplayModeChange(DisplayMode mode) {
  if (display_mode_ != mode) {
    display_mode_ = mode;
    HandleEvent(Event::DISPLAY_MODE_CHANGE);
  }
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
    HandleEvent(Event::SUSPEND_DELAYS_READY);
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

    HandleEvent(Event::READY_TO_RESUSPEND);
  }
}

void Suspender::OnDisplaysChanged(
    const std::vector<system::DisplayInfo>& new_displays) {
  std::vector<system::DisplayInfo> sorted_new_displays = new_displays;
  std::sort(sorted_new_displays.begin(), sorted_new_displays.end());
  if (!std::includes(displays_.begin(), displays_.end(),
                     sorted_new_displays.begin(), sorted_new_displays.end()))
    HandleEvent(Event::NEW_DISPLAY);
  displays_ = std::move(sorted_new_displays);
}

// static.
std::string Suspender::EventToString(Event event) {
  switch (event) {
    case Event::SUSPEND_REQUESTED:
      return "SuspendRequested";
    case Event::SUSPEND_DELAYS_READY:
      return "SuspendDelaysReady";
    case Event::USER_ACTIVITY:
      return "UserActivity";
    case Event::READY_TO_RESUSPEND:
      return "ReadyToResuspend";
    case Event::SHUTDOWN_STARTED:
      return "ShutdownStarted";
    case Event::WAKE_NOTIFICATION:
      return "WakeNotification";
    case Event::DISPLAY_MODE_CHANGE:
      return "DisplayModeChange";
    case Event::NEW_DISPLAY:
      return "NewDisplay";
  }
}

void Suspender::ExportDBusMethods() {
  // Normal suspend/resume methods:
  dbus_wrapper_->ExportMethod(kRegisterSuspendDelayMethod,
                              base::Bind(&Suspender::RegisterSuspendDelay,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         suspend_delay_controller_.get()));
  dbus_wrapper_->ExportMethod(kUnregisterSuspendDelayMethod,
                              base::Bind(&Suspender::UnregisterSuspendDelay,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         suspend_delay_controller_.get()));
  dbus_wrapper_->ExportMethod(kHandleSuspendReadinessMethod,
                              base::Bind(&Suspender::HandleSuspendReadiness,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         suspend_delay_controller_.get()));

  // Dark suspend/resume methods:
  dbus_wrapper_->ExportMethod(kRegisterDarkSuspendDelayMethod,
                              base::Bind(&Suspender::RegisterSuspendDelay,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         dark_suspend_delay_controller_.get()));
  dbus_wrapper_->ExportMethod(kUnregisterDarkSuspendDelayMethod,
                              base::Bind(&Suspender::UnregisterSuspendDelay,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         dark_suspend_delay_controller_.get()));
  dbus_wrapper_->ExportMethod(kHandleDarkSuspendReadinessMethod,
                              base::Bind(&Suspender::HandleSuspendReadiness,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         dark_suspend_delay_controller_.get()));
  dbus_wrapper_->ExportMethod(kRecordDarkResumeWakeReasonMethod,
                              base::Bind(&Suspender::RecordDarkResumeWakeReason,
                                         weak_ptr_factory_.GetWeakPtr()));
}

void Suspender::RegisterSuspendDelay(
    SuspendDelayController* controller,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  RegisterSuspendDelayRequest request;
  dbus::MessageReader reader(method_call);
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse " << method_call->GetMember() << " request";
    std::move(response_sender)
        .Run(
            std::unique_ptr<dbus::Response>(dbus::ErrorResponse::FromMethodCall(
                method_call, DBUS_ERROR_INVALID_ARGS,
                "Expected serialized protocol buffer")));
    return;
  }
  RegisterSuspendDelayReply reply_proto;
  controller->RegisterSuspendDelay(request, method_call->GetSender(),
                                   &reply_proto);

  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendProtoAsArrayOfBytes(reply_proto);
  std::move(response_sender).Run(std::move(response));
}

void Suspender::UnregisterSuspendDelay(
    SuspendDelayController* controller,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  UnregisterSuspendDelayRequest request;
  dbus::MessageReader reader(method_call);
  if (!reader.PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse " << method_call->GetMember() << " request";
    std::move(response_sender)
        .Run(
            std::unique_ptr<dbus::Response>(dbus::ErrorResponse::FromMethodCall(
                method_call, DBUS_ERROR_INVALID_ARGS,
                "Expected serialized protocol buffer")));
    return;
  }
  controller->UnregisterSuspendDelay(request, method_call->GetSender());
  std::move(response_sender).Run(dbus::Response::FromMethodCall(method_call));
}

void Suspender::HandleSuspendReadiness(
    SuspendDelayController* controller,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  SuspendReadinessInfo info;
  dbus::MessageReader reader(method_call);
  if (!reader.PopArrayOfBytesAsProto(&info)) {
    LOG(ERROR) << "Unable to parse " << method_call->GetMember() << " request";
    std::move(response_sender)
        .Run(
            std::unique_ptr<dbus::Response>(dbus::ErrorResponse::FromMethodCall(
                method_call, DBUS_ERROR_INVALID_ARGS,
                "Expected serialized protocol buffer")));
    return;
  }
  controller->HandleSuspendReadiness(info, method_call->GetSender());
  std::move(response_sender).Run(dbus::Response::FromMethodCall(method_call));
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
    std::move(response_sender)
        .Run(
            std::unique_ptr<dbus::Response>(dbus::ErrorResponse::FromMethodCall(
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
  std::move(response_sender).Run(dbus::Response::FromMethodCall(method_call));
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
    case State::IDLE:
      DCHECK(!dark_resume_->InDarkResume());
      switch (event) {
        case Event::SUSPEND_REQUESTED:
          StartRequest();
          state_ = State::WAITING_FOR_NORMAL_SUSPEND_DELAYS;
          break;
        case Event::SHUTDOWN_STARTED:
          state_ = State::SHUTTING_DOWN;
          break;
        default:
          break;
      }
      break;
    case State::WAITING_FOR_NORMAL_SUSPEND_DELAYS:
      HandleEventInWaitingForNormalSuspendDelays(event);
      break;
    case State::WAITING_TO_RETRY_SUSPEND:
      // Fallthrough.
    case State::WAITING_FOR_DARK_SUSPEND_DELAYS:
      HandleEventInDarkResumeOrRetrySuspend(event);
      break;
    case State::SHUTTING_DOWN:
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

void Suspender::HandleEventInWaitingForNormalSuspendDelays(Event event) {
  DCHECK_EQ(state_, State::WAITING_FOR_NORMAL_SUSPEND_DELAYS);
  switch (event) {
    case Event::SUSPEND_DELAYS_READY:
      state_ = Suspend();
      break;
    case Event::WAKE_NOTIFICATION:
      // fallthrough.
    case Event::DISPLAY_MODE_CHANGE:
      // fallthrough.
    case Event::USER_ACTIVITY:
    // fallthrough
    case Event::NEW_DISPLAY:
      state_ = HandleWakeEventInSuspend(event);
      break;
    case Event::SHUTDOWN_STARTED:
      FinishRequest(false, SuspendDone_WakeupType_NOT_APPLICABLE);
      state_ = State::SHUTTING_DOWN;
      break;
    default:
      break;
  }
}

void Suspender::HandleEventInDarkResumeOrRetrySuspend(Event event) {
  DCHECK((state_ == State::WAITING_FOR_DARK_SUSPEND_DELAYS) ||
         (state_ == State::WAITING_TO_RETRY_SUSPEND));
  switch (event) {
    case Event::READY_TO_RESUSPEND:
      state_ = Suspend();
      break;
    case Event::WAKE_NOTIFICATION:
      // fallthrough.
    case Event::DISPLAY_MODE_CHANGE:
      // fallthrough.
    case Event::USER_ACTIVITY:
      // fallthrough
    case Event::NEW_DISPLAY:
      state_ = HandleWakeEventInSuspend(event);
      break;
    case Event::SHUTDOWN_STARTED:
      FinishRequest(false, SuspendDone_WakeupType_NOT_APPLICABLE);
      state_ = State::SHUTTING_DOWN;
      break;
    default:
      break;
  }
}

Suspender::State Suspender::HandleWakeEventInSuspend(Event event) {
  DCHECK((state_ == State::WAITING_FOR_NORMAL_SUSPEND_DELAYS) ||
         (state_ == State::WAITING_FOR_DARK_SUSPEND_DELAYS) ||
         (state_ == State::WAITING_TO_RETRY_SUSPEND));
  DCHECK(
      (event == Event::WAKE_NOTIFICATION) || (event == Event::USER_ACTIVITY) ||
      (event == Event::DISPLAY_MODE_CHANGE) || (event == Event::NEW_DISPLAY));
  // Avoid canceling suspend for errant touchpad, power button, etc.
  // events that are generated when the lid is closed and device is not docked.
  // Abort suspend if new display is seen even when the lid is closed.
  if (display_mode_ == DisplayMode::NORMAL && event != Event::NEW_DISPLAY &&
      delegate_->IsLidClosedForSuspend())
    return state_;

  LOG(INFO) << "Aborting request in response to event " << EventToString(event);
  FinishRequest(false, SuspendDone_WakeupType_NOT_APPLICABLE);
  return State::IDLE;
}

void Suspender::StartRequest() {
  DCHECK(!dark_resume_->InDarkResume());

  suspend_request_id_++;
  LOG(INFO) << "Starting request " << suspend_request_id_;

  if (suspend_request_supplied_wakeup_count_) {
    wakeup_count_ = suspend_request_wakeup_count_;
    wakeup_count_valid_ = true;
  } else {
    wakeup_count_valid_ = delegate_->ReadSuspendWakeupCount(&wakeup_count_);
  }

  suspend_request_start_time_ = clock_->GetCurrentBootTime();
  current_num_attempts_ = 0;
  initial_num_attempts_ = 0;

  dark_resume_wake_durations_.clear();
  last_dark_resume_wake_reason_ = kDefaultWakeReason;

  // Call PrepareToSuspend() before emitting SuspendImminent -- powerd needs to
  // set the backlight level to 0 before Chrome turns the display on in response
  // to the signal.
  delegate_->PrepareToSuspend();
  suspend_delay_controller_->PrepareForSuspend(suspend_request_id_, false);
  wakeup_source_identifier_->PrepareForSuspendRequest();
  delegate_->SetSuspendAnnounced(true);

  SuspendImminent proto;
  proto.set_suspend_id(suspend_request_id_);
  proto.set_reason(suspend_request_reason_);
  dbus_wrapper_->EmitSignalWithProtocolBuffer(kSuspendImminentSignal, proto);
}

void Suspender::FinishRequest(bool success,
                              SuspendDone::WakeupType wakeup_type) {
  const base::TimeTicks end_time = clock_->GetCurrentBootTime();
  base::TimeDelta suspend_duration = end_time - suspend_request_start_time_;
  if (suspend_duration < base::TimeDelta()) {
    LOG(ERROR) << "Boot time went backward: started at "
               << suspend_request_start_time_ << ", ended at " << end_time;
    suspend_duration = base::TimeDelta();
  }

  LOG(INFO) << "Finishing request " << suspend_request_id_ << " "
            << (success ? "" : "un") << "successfully after "
            << util::TimeDeltaToString(suspend_duration);

  resuspend_timer_.Stop();
  suspend_delay_controller_->FinishSuspend(suspend_request_id_);
  dark_suspend_delay_controller_->FinishSuspend(dark_suspend_id_);
  shutdown_from_suspend_->HandleFullResume();
  EmitSuspendDoneSignal(suspend_request_id_, suspend_duration, wakeup_type);
  delegate_->SetSuspendAnnounced(false);
  dark_resume_->ExitDarkResume();
  delegate_->UndoPrepareToSuspend(success, initial_num_attempts_
                                               ? initial_num_attempts_
                                               : current_num_attempts_);

  // Only report dark resume metrics if it is actually enabled to prevent a
  // bunch of noise in the data.
  if (dark_resume_->IsEnabled()) {
    delegate_->GenerateDarkResumeMetrics(dark_resume_wake_durations_,
                                         suspend_duration);
  }
}

Suspender::State Suspender::Suspend() {
  policy::ShutdownFromSuspendInterface::Action action =
      shutdown_from_suspend_->PrepareForSuspendAttempt();

  switch (action) {
    case policy::ShutdownFromSuspendInterface::Action::SHUT_DOWN:
      LOG(INFO) << "Shutting down from suspend";
      // Don't call FinishRequest(); we want the backlight to stay off.
      delegate_->ShutDownFromSuspend();
      return State::SHUTTING_DOWN;
    case policy::ShutdownFromSuspendInterface::Action::SUSPEND:
      if (suspend_duration_ != base::TimeDelta()) {
        LOG(INFO) << "Suspending for " << suspend_duration_.InSeconds()
                  << " seconds";
      }
      break;
  }

  // Note: If this log message is changed, the power_AudioDetector test
  // must be updated.
  LOG(INFO) << "Starting suspend";

  if (!dark_resume_wake_durations_.empty()) {
    dark_resume_wake_durations_.back().first = last_dark_resume_wake_reason_;
    dark_resume_wake_durations_.back().second =
        std::max(base::TimeDelta(),
                 clock_->GetCurrentBootTime() - dark_resume_start_time_);
  }

  current_num_attempts_++;
  Delegate::SuspendResult result = delegate_->DoSuspend(
      wakeup_count_, wakeup_count_valid_, suspend_duration_);

  wakeup_source_identifier_->HandleResume();

  //  If we saw a wakeup event and it if it is from any input devices, treat
  //  previous resume as successful as a wake event from input device implies a
  //  user interaction.
  if (result == Delegate::SuspendResult::CANCELED &&
      wakeup_source_identifier_->InputDeviceCausedLastWake())
    result = Delegate::SuspendResult::SUCCESS;

  if (result == Delegate::SuspendResult::SUCCESS) {
    // Reset this immediately right after a successful suspend, leave it
    // for retry attempts
    suspend_duration_ = base::TimeDelta();
    dark_resume_->HandleSuccessfulResume();
  }

  // TODO(crbug.com/790898): Identify attempts that are canceled due to wakeup
  // events from dark resume sources and call HandleDarkResume instead.
  return dark_resume_->InDarkResume() ? HandleDarkResume(result)
                                      : HandleNormalResume(result);
}

Suspender::State Suspender::HandleNormalResume(Delegate::SuspendResult result) {
  SuspendDone::WakeupType wakeup_type = SuspendDone_WakeupType_NOT_APPLICABLE;

  if (result == Delegate::SuspendResult::SUCCESS) {
    wakeup_type = wakeup_source_identifier_->InputDeviceCausedLastWake()
                      ? SuspendDone_WakeupType_INPUT
                      : SuspendDone_WakeupType_OTHER;
  }

  // If an external wakeup count was provided, the caller doesn't want us to
  // just keep trying until it works. Finish out the suspend and report failure
  // to what is likely a test. For example, EC-reported S0ix failures found in
  // UndoPrepareForSuspend() should result in failure.
  if ((result == Delegate::SuspendResult::SUCCESS) ||
      suspend_request_supplied_wakeup_count_) {
    FinishRequest(result == Delegate::SuspendResult::SUCCESS, wakeup_type);
    return State::IDLE;
  }

  return HandleUnsuccessfulSuspend(result);
}

Suspender::State Suspender::HandleDarkResume(Delegate::SuspendResult result) {
  // Go through the normal unsuccessful-suspend path if the suspend failed in
  // the kernel or if we've exceeded the maximum number of retries.
  if (result == Delegate::SuspendResult::FAILURE ||
      (result == Delegate::SuspendResult::CANCELED &&
       current_num_attempts_ > max_retries_))
    return HandleUnsuccessfulSuspend(result);

  // Save the first run's number of attempts so it can be reported later.
  if (!initial_num_attempts_)
    initial_num_attempts_ = current_num_attempts_;

  dark_suspend_id_++;

  shutdown_from_suspend_->HandleDarkResume();

  if (result == Delegate::SuspendResult::SUCCESS) {
    // This is the start of a new dark resume wake.
    dark_resume_start_time_ = clock_->GetCurrentBootTime();
    dark_resume_wake_durations_.push_back(
        DarkResumeInfo(kDefaultWakeReason, base::TimeDelta()));
    last_dark_resume_wake_reason_ = kDefaultWakeReason;
    current_num_attempts_ = 0;
  } else {
    DCHECK_EQ(result, Delegate::SuspendResult::CANCELED);
    LOG(WARNING) << "Suspend attempt #" << current_num_attempts_
                 << " canceled due to wake event";
  }

  LOG(INFO) << "Notifying registered dark suspend delays about "
            << dark_suspend_id_;
  dark_suspend_delay_controller_->PrepareForSuspend(dark_suspend_id_, true);
  EmitDarkSuspendImminentSignal();

  return State::WAITING_FOR_DARK_SUSPEND_DELAYS;
}

Suspender::State Suspender::HandleUnsuccessfulSuspend(
    Delegate::SuspendResult result) {
  DCHECK_NE(result, Delegate::SuspendResult::SUCCESS);

  if (current_num_attempts_ > max_retries_) {
    LOG(ERROR) << "Unsuccessfully attempted to suspend "
               << current_num_attempts_ << " times; shutting down";
    // Don't call FinishRequest(); we want the backlight to stay off.
    delegate_->ShutDownForFailedSuspend();
    return State::SHUTTING_DOWN;
  }

  if (result == Delegate::SuspendResult::CANCELED) {
    LOG(WARNING) << "Suspend attempt #" << current_num_attempts_
                 << " canceled due to wake event";
    wakeup_count_ = 0;
    wakeup_count_valid_ = false;
  } else {
    DCHECK_EQ(result, Delegate::SuspendResult::FAILURE);
    LOG(WARNING) << "Suspend attempt #" << current_num_attempts_ << " failed; "
                 << "will retry in " << retry_delay_.InMilliseconds() << " ms";
    if (!suspend_request_supplied_wakeup_count_)
      wakeup_count_valid_ = delegate_->ReadSuspendWakeupCount(&wakeup_count_);
  }

  ScheduleResuspend(retry_delay_);
  return State::WAITING_TO_RETRY_SUSPEND;
}

void Suspender::ScheduleResuspend(const base::TimeDelta& delay) {
  resuspend_timer_.Start(
      FROM_HERE, delay,
      base::Bind(&Suspender::HandleEvent, base::Unretained(this),
                 Event::READY_TO_RESUSPEND));
}

void Suspender::EmitSuspendDoneSignal(int suspend_request_id,
                                      const base::TimeDelta& suspend_duration,
                                      SuspendDone::WakeupType wakeup_type) {
  SuspendDone proto;
  proto.set_suspend_id(suspend_request_id);
  proto.set_suspend_duration(suspend_duration.ToInternalValue());
  proto.set_wakeup_type(wakeup_type);
  dbus_wrapper_->EmitSignalWithProtocolBuffer(kSuspendDoneSignal, proto);
}

void Suspender::EmitDarkSuspendImminentSignal() {
  SuspendImminent proto;
  proto.set_suspend_id(dark_suspend_id_);
  dbus_wrapper_->EmitSignalWithProtocolBuffer(kDarkSuspendImminentSignal,
                                              proto);
}

}  // namespace policy
}  // namespace power_manager
