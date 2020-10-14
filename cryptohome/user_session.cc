// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session.h"

#include <base/memory/ref_counted.h>

#include "cryptohome/mount.h"

namespace cryptohome {

UserSession::UserSession() {}
UserSession::~UserSession() {}
UserSession::UserSession(const scoped_refptr<Mount> mount) : mount_(mount) {}

}  // namespace cryptohome
