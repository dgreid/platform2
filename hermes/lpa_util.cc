// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/lpa_util.h"

#include <map>
#include <memory>

#include <brillo/array_utils.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>

namespace hermes {

namespace {

struct ErrorInfo {
  constexpr ErrorInfo(int lpa_code,
                      const char* error_code,
                      const char* error_message)
      : lpa_code_(lpa_code),
        error_code_(error_code),
        error_message_(error_message) {}

  int lpa_code_;
  const char* error_code_;
  const char* error_message_;
};

// Note that lpa error codes are not constexpr atm.
const auto kErrors = brillo::make_array<ErrorInfo>(
    ErrorInfo(lpa::core::Lpa::kWrongState,
              kErrorWrongState,
              "Invalid state for requested method"),
    ErrorInfo(lpa::core::Lpa::kIccidNotFound, "InvalidIccid", "Invalid iccid"),
    ErrorInfo(lpa::core::Lpa::kProfileAlreadyEnabled,
              kErrorAlreadyEnabled,
              "Requested method provided an already-enabled profile"),
    ErrorInfo(lpa::core::Lpa::kProfileAlreadyDisabled,
              kErrorAlreadyDisabled,
              "Requested method provided a disabled profile"),
    ErrorInfo(lpa::core::Lpa::kNeedConfirmationCode,
              kErrorNeedConfirmationCode,
              "Need confirmation code"),
    ErrorInfo(lpa::core::Lpa::kInvalidActivationCode,
              kErrorInvalidActivationCode,
              "Invalid activation code"),
    ErrorInfo(lpa::core::Lpa::kFailedToSendNotifications,
              kErrorSendNotificationFailure,
              "Failed to send notifications"),
    ErrorInfo(lpa::core::Lpa::kNoOpForTestingProfile,
              kErrorTestProfileInProd,
              "Non-test mode cannot use test profile"));

}  // namespace

brillo::ErrorPtr LpaErrorToBrillo(const base::Location& location, int error) {
  if (error == lpa::core::Lpa::kNoError) {
    return nullptr;
  }

  for (auto& info : kErrors) {
    if (info.lpa_code_ == error) {
      return brillo::Error::Create(location, brillo::errors::dbus::kDomain,
                                   info.error_code_, info.error_message_);
    }
  }
  return brillo::Error::Create(location, brillo::errors::dbus::kDomain,
                               kErrorUnknown, "Unknown error");
}

}  // namespace hermes
