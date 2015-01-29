// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_DARK_RESUME_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_DARK_RESUME_STUB_H_

#include "power_manager/powerd/system/dark_resume.h"

namespace power_manager {
namespace system {

// Stub implementation of DarkResumeInterface for tests.
class DarkResumeStub : public DarkResumeInterface {
 public:
  DarkResumeStub();
  virtual ~DarkResumeStub();

  void set_action(Action action) { action_ = action; }
  void set_suspend_duration(const base::TimeDelta& duration) {
    suspend_duration_ = duration;
  }
  void set_in_dark_resume(bool in_dark_resume) {
    in_dark_resume_ = in_dark_resume;
  }
  void set_enabled(bool enabled) { enabled_ = enabled; }

  // DarkResumeInterface implementation:
  void PrepareForSuspendRequest() override;
  void UndoPrepareForSuspendRequest() override;
  void GetActionForSuspendAttempt(Action* action,
                                  base::TimeDelta* suspend_duration) override;
  void HandleSuccessfulResume() override;
  bool InDarkResume() override;
  bool IsEnabled() override;

 private:
  // Values to return.
  Action action_;
  base::TimeDelta suspend_duration_;
  bool in_dark_resume_;
  bool enabled_;

  DISALLOW_COPY_AND_ASSIGN(DarkResumeStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_DARK_RESUME_STUB_H_
