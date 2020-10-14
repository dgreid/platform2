// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_USER_SESSION_H_
#define CRYPTOHOME_USER_SESSION_H_

#include <base/memory/ref_counted.h>

#include "cryptohome/mount.h"

namespace cryptohome {

class UserSession : public base::RefCountedThreadSafe<UserSession> {
 public:
  UserSession();
  UserSession(const scoped_refptr<Mount> mount);
  virtual ~UserSession();

  // Disallow Copy/Move/Assign
  UserSession(const UserSession&) = delete;
  UserSession(const UserSession&&) = delete;
  void operator=(const UserSession&) = delete;
  void operator=(const UserSession&&) = delete;

  scoped_refptr<Mount> GetMount() { return mount_; }
  const scoped_refptr<Mount> GetMount() const { return mount_; }

 private:
  scoped_refptr<cryptohome::Mount> mount_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_USER_SESSION_H_
