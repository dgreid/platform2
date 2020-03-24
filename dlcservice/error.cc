// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/error.h"

#include <base/strings/stringprintf.h>
#include <brillo/errors/error_codes.h>

namespace dlcservice {

// static
brillo::ErrorPtr Error::Create(const std::string& code,
                               const std::string& msg) {
  return brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain, code,
                               msg);
}

// static
std::string Error::ToString(const brillo::ErrorPtr& err) {
  // TODO(crbug.com/999284): No inner error support, err->GetInnerError().
  DCHECK(err);
  return base::StringPrintf("Error Code=%s, Error Message=%s",
                            err->GetCode().c_str(), err->GetMessage().c_str());
}

}  // namespace dlcservice
