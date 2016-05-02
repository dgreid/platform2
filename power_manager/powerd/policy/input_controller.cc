// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/input_controller.h"

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/dbus_sender.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/display/display_watcher.h"
#include "power_manager/powerd/system/input_watcher_interface.h"
#include "power_manager/proto_bindings/input_event.pb.h"

namespace power_manager {
namespace policy {

namespace {

// Frequency with which CheckActiveVT() should be called, in seconds.
// This just needs to be lower than the screen-dimming delay.
const int kCheckActiveVTFrequencySec = 60;

}  // namespace

InputController::InputController()
    : input_watcher_(NULL),
      delegate_(NULL),
      display_watcher_(NULL),
      dbus_sender_(NULL),
      clock_(new Clock),
      only_has_external_display_(false),
      lid_state_(LID_NOT_PRESENT),
      tablet_mode_(TABLET_MODE_OFF) {
}

InputController::~InputController() {
  if (input_watcher_)
    input_watcher_->RemoveObserver(this);
}

void InputController::Init(system::InputWatcherInterface* input_watcher,
                           Delegate* delegate,
                           system::DisplayWatcherInterface* display_watcher,
                           DBusSenderInterface* dbus_sender,
                           PrefsInterface* prefs) {
  input_watcher_ = input_watcher;
  input_watcher_->AddObserver(this);
  delegate_ = delegate;
  display_watcher_ = display_watcher;
  dbus_sender_ = dbus_sender;

  prefs->GetBool(kExternalDisplayOnlyPref, &only_has_external_display_);

  bool use_lid = false;
  if (prefs->GetBool(kUseLidPref, &use_lid) && use_lid)
    lid_state_ = input_watcher_->QueryLidState();

  tablet_mode_ = input_watcher_->GetTabletMode();

  bool check_active_vt = false;
  if (prefs->GetBool(kCheckActiveVTPref, &check_active_vt) && check_active_vt) {
    check_active_vt_timer_.Start(FROM_HERE,
        base::TimeDelta::FromSeconds(kCheckActiveVTFrequencySec),
        this, &InputController::CheckActiveVT);
  }
}

bool InputController::TriggerPowerButtonAcknowledgmentTimeoutForTesting() {
  if (!power_button_acknowledgment_timer_.IsRunning())
    return false;

  power_button_acknowledgment_timer_.Stop();
  HandlePowerButtonAcknowledgmentTimeout();
  return true;
}

bool InputController::TriggerCheckActiveVTTimeoutForTesting() {
  if (!check_active_vt_timer_.IsRunning())
    return false;

  CheckActiveVT();
  return true;
}

void InputController::HandlePowerButtonAcknowledgment(
    const base::TimeTicks& timestamp) {
  VLOG(1) << "Received acknowledgment of power button press at "
          << timestamp.ToInternalValue() << "; expected "
          << expected_power_button_acknowledgment_timestamp_.ToInternalValue();
  if (timestamp == expected_power_button_acknowledgment_timestamp_) {
    delegate_->ReportPowerButtonAcknowledgmentDelay(clock_->GetCurrentTime() -
        expected_power_button_acknowledgment_timestamp_);
    expected_power_button_acknowledgment_timestamp_ = base::TimeTicks();
    power_button_acknowledgment_timer_.Stop();
  }
}

void InputController::OnLidEvent(LidState state) {
  lid_state_ = state;
  InputEvent proto;
  switch (lid_state_) {
    case LID_CLOSED:
      delegate_->HandleLidClosed();
      proto.set_type(InputEvent_Type_LID_CLOSED);
      break;
    case LID_OPEN:
      delegate_->HandleLidOpened();
      proto.set_type(InputEvent_Type_LID_OPEN);
      break;
    case LID_NOT_PRESENT:
      return;
  }
  proto.set_timestamp(clock_->GetCurrentTime().ToInternalValue());
  dbus_sender_->EmitSignalWithProtocolBuffer(kInputEventSignal, proto);
}

void InputController::OnTabletModeEvent(TabletMode mode) {
  tablet_mode_ = mode;

  delegate_->HandleTabletModeChanged(mode);

  InputEvent proto;
  proto.set_type(tablet_mode_ == TABLET_MODE_ON ?
                 InputEvent_Type_TABLET_MODE_ON :
                 InputEvent_Type_TABLET_MODE_OFF);
  proto.set_timestamp(clock_->GetCurrentTime().ToInternalValue());
  dbus_sender_->EmitSignalWithProtocolBuffer(kInputEventSignal, proto);
}

void InputController::OnPowerButtonEvent(ButtonState state) {
  if (state == BUTTON_DOWN && only_has_external_display_ &&
      display_watcher_->GetDisplays().empty()) {
    delegate_->ShutDownForPowerButtonWithNoDisplay();
    return;
  }

  if (state != BUTTON_REPEAT) {
    const base::TimeTicks now = clock_->GetCurrentTime();

    InputEvent proto;
    proto.set_type(state == BUTTON_DOWN ?
                   InputEvent_Type_POWER_BUTTON_DOWN :
                   InputEvent_Type_POWER_BUTTON_UP);
    proto.set_timestamp(now.ToInternalValue());
    dbus_sender_->EmitSignalWithProtocolBuffer(kInputEventSignal, proto);

    if (state == BUTTON_DOWN) {
      expected_power_button_acknowledgment_timestamp_ = now;
      power_button_acknowledgment_timer_.Start(FROM_HERE,
          base::TimeDelta::FromMilliseconds(
              kPowerButtonAcknowledgmentTimeoutMs),
          this, &InputController::HandlePowerButtonAcknowledgmentTimeout);
    } else {
      expected_power_button_acknowledgment_timestamp_ = base::TimeTicks();
      power_button_acknowledgment_timer_.Stop();
    }
  }

  delegate_->HandlePowerButtonEvent(state);
}

void InputController::OnHoverStateChanged(bool hovering) {
  delegate_->HandleHoverStateChanged(hovering);
}

void InputController::CheckActiveVT() {
  if (input_watcher_->GetActiveVT() == 2)
    delegate_->DeferInactivityTimeoutForVT2();
}

void InputController::HandlePowerButtonAcknowledgmentTimeout() {
  delegate_->ReportPowerButtonAcknowledgmentDelay(
      base::TimeDelta::FromMilliseconds(
          kPowerButtonAcknowledgmentTimeoutMs));
  delegate_->HandleMissingPowerButtonAcknowledgment();
  expected_power_button_acknowledgment_timestamp_ = base::TimeTicks();
}

}  // namespace policy
}  // namespace power_manager
