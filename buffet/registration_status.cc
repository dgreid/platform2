// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/registration_status.h"

namespace buffet {

std::string StatusToString(RegistrationStatus status) {
  switch (status) {
    case RegistrationStatus::kOffline:
      return "offline";
    case RegistrationStatus::kCloudError:
      return "cloud_error";
    case RegistrationStatus::kUnregistered:
      return "unregistered";
    case RegistrationStatus::kRegistering:
      return "registering";
    case RegistrationStatus::kRegistered:
      return "registered";
  }
  return "unknown";
}

}  // namespace buffet
