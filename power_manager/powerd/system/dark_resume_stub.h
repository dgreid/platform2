// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_DARK_RESUME_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_DARK_RESUME_STUB_H_

#include <base/macros.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "power_manager/powerd/system/dark_resume_interface.h"

namespace power_manager {
namespace system {

// Stub implementation of DarkResumeInterface for tests.
class DarkResumeStub : public DarkResumeInterface {
 public:
  DarkResumeStub();
  DarkResumeStub(const DarkResumeStub&) = delete;
  DarkResumeStub& operator=(const DarkResumeStub&) = delete;

  ~DarkResumeStub() override;

  void set_in_dark_resume(bool in_dark_resume) {
    in_dark_resume_ = in_dark_resume;
  }
  void set_enabled(bool enabled) { enabled_ = enabled; }

  // DarkResumeInterface implementation:
  void HandleSuccessfulResume() override;
  bool InDarkResume() override;
  bool IsEnabled() override;
  void ExitDarkResume() override{};

 private:
  // Values to return.
  bool in_dark_resume_ = false;
  bool enabled_ = false;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_DARK_RESUME_STUB_H_
