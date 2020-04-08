// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_LPA_UTIL_H_
#define HERMES_LPA_UTIL_H_

#include <memory>

#include <brillo/dbus/dbus_method_response.h>
#include <brillo/errors/error.h>
#include <google-lpa/lpa/core/lpa.h>

#include "hermes/executor.h"

namespace hermes {

// Context needed to interact with the google-lpa library.
struct LpaContext {
  lpa::core::Lpa* lpa;
  Executor* executor;
};

// Create a brillo Error from an Lpa error code. Return nullptr if no error.
brillo::ErrorPtr LpaErrorToBrillo(const base::Location& location, int error);

}  // namespace hermes

#endif  // HERMES_LPA_UTIL_H_
