// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_STATUS_MACROS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_STATUS_MACROS_H_

#include "diagnostics/cros_healthd/utils/storage/statusor.h"

#define RETURN_IF_ERROR(rexpr) \
  do {                         \
    auto status = (rexpr);     \
    if (!status.ok())          \
      return status;           \
  } while (0)

// Generates multi-line expression.
#define ASSIGN_OR_RETURN(lhs, rexpr)    \
  STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_( \
      STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__), lhs, rexpr)

// Implementation details.
#define STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                                         \
  if (!statusor.ok()) {                                            \
    return (statusor.status());                                    \
  }                                                                \
  lhs = statusor.value()

// Internal helper for concatenating macro values.
#define STATUS_MACROS_IMPL_CONCAT_INNER_(x, y) x##y
#define STATUS_MACROS_IMPL_CONCAT_(x, y) STATUS_MACROS_IMPL_CONCAT_INNER_(x, y)

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_STATUS_MACROS_H_
