// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_COMMANDS_USER_ROLE_H_
#define BUFFET_COMMANDS_USER_ROLE_H_

#include <string>
#include <chromeos/errors/error.h>

namespace buffet {

enum class UserRole {
  kViewer,
  kUser,
  kManager,
  kOwner,
};

std::string ToString(UserRole role);

bool FromString(const std::string& str,
                UserRole* role,
                chromeos::ErrorPtr* error);

}  // namespace buffet

#endif  // BUFFET_COMMANDS_USER_ROLE_H_
