// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/error.h"

#include <base/strings/stringprintf.h>
#include <brillo/errors/error_codes.h>
#include <dbus/dlcservice/dbus-constants.h>

namespace dlcservice {

const char kDlcErrorDomain[] = "dlcservice";

namespace error {
const char kFailedToCreateDirectory[] = "failedToCreateDirectory";
const char kFailedInstallInUpdateEngine[] = "failedInstallInUpdateEngine";
const char kFailedInternal[] = "InternalError";
const char kFailedToVerifyImage[] = "failedToVerifyImage";
const char kFailedToMountImage[] = "failedToMountImage";
}  // namespace error

// static
brillo::ErrorPtr Error::Create(const base::Location& location,
                               const std::string& code,
                               const std::string& msg) {
  return brillo::Error::Create(location, brillo::errors::dbus::kDomain, code,
                               msg);
}

// static
brillo::ErrorPtr Error::CreateInternal(const base::Location& location,
                                       const std::string& code,
                                       const std::string& message) {
  return brillo::Error::Create(location, kDlcErrorDomain, code, message);
}

// static
void Error::AddTo(brillo::ErrorPtr* error,
                  const base::Location& location,
                  const std::string& code,
                  const std::string& message) {
  brillo::Error::AddTo(error, location, brillo::errors::dbus::kDomain, code,
                       message);
}

// static
void Error::AddInternalTo(brillo::ErrorPtr* error,
                          const base::Location& location,
                          const std::string& code,
                          const std::string& message) {
  brillo::Error::AddTo(error, location, kDlcErrorDomain, code, message);
}

// static
std::string Error::ToString(const brillo::ErrorPtr& err) {
  // TODO(crbug.com/999284): No inner error support, err->GetInnerError().
  DCHECK(err);
  return base::StringPrintf("Error Code=%s, Error Message=%s",
                            err->GetCode().c_str(), err->GetMessage().c_str());
}

std::string Error::GetRootErrorCode(const brillo::ErrorPtr& error) {
  DCHECK(error);
  return error->GetFirstError()->GetCode();
}

std::string Error::GetDbusErrorCode(const brillo::ErrorPtr& error) {
  const brillo::Error* dbus_err = brillo::Error::FindErrorOfDomain(
      error.get(), brillo::errors::dbus::kDomain);
  if (dbus_err)
    return dbus_err->GetCode();
  else
    return kErrorInternal;
}

void Error::ConvertToDbusError(brillo::ErrorPtr* error) {
  DCHECK(error->get());
  if (error->get()->GetInnerError() == nullptr &&
      error->get()->GetDomain() == brillo::errors::dbus::kDomain)
    return;  // The error is already a dbus error without inner errors.

  const brillo::Error* dbus_err = brillo::Error::FindErrorOfDomain(
      error->get(), brillo::errors::dbus::kDomain);
  if (dbus_err)
    *error = Create(dbus_err->GetLocation(), dbus_err->GetCode(),
                    dbus_err->GetMessage());
  else  // We would only reach here if there are no dbus errors in the chain.
    *error = Create(error->get()->GetLocation(), kErrorInternal,
                    error->get()->GetMessage());
}

}  // namespace dlcservice
