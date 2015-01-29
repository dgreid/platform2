// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/dark_resume_stub.h"

namespace power_manager {
namespace system {

DarkResumeStub::DarkResumeStub()
    : action_(SUSPEND),
      in_dark_resume_(false),
      enabled_(false) {
}

DarkResumeStub::~DarkResumeStub() {
}

void DarkResumeStub::PrepareForSuspendRequest() {}

void DarkResumeStub::UndoPrepareForSuspendRequest() {}

void DarkResumeStub::GetActionForSuspendAttempt(
    Action* action,
    base::TimeDelta* suspend_duration) {
  CHECK(action);
  CHECK(suspend_duration);
  *action = action_;
  *suspend_duration = suspend_duration_;
}

void DarkResumeStub::HandleSuccessfulResume() {}

bool DarkResumeStub::InDarkResume() {
  return in_dark_resume_;
}

bool DarkResumeStub::IsEnabled() {
  return enabled_;
}

}  // namespace system
}  // namespace power_manager
