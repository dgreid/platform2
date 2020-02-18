// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_PRIVILEGE_DROPPER_H_
#define CRASH_REPORTER_PRIVILEGE_DROPPER_H_

// Utility class to temporarily drop from root to crash in a particular scope.
class ScopedPrivilegeDropper {
 public:
  ScopedPrivilegeDropper();
  ~ScopedPrivilegeDropper();

 private:
  // True iff the constructor dropped privileges.
  // This constant is used so that we can nest ScopedPrivilegeDroppers and to
  // allow unit tests (that won't start as root) to work.
  bool dropped_privs_ = false;

  DISALLOW_COPY_AND_ASSIGN(ScopedPrivilegeDropper);
};

#endif  // CRASH_REPORTER_PRIVILEGE_DROPPER_H_
