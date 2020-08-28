// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_ERROR_LOGGER_H_
#define CROS_DISKS_ERROR_LOGGER_H_

#include <ostream>

#include <chromeos/dbus/service_constants.h>

namespace cros_disks {

// Output operators for logging.
std::ostream& operator<<(std::ostream& out, FormatErrorType error);
std::ostream& operator<<(std::ostream& out, MountErrorType error);
std::ostream& operator<<(std::ostream& out, PartitionErrorType error);
std::ostream& operator<<(std::ostream& out, RenameErrorType error);

}  // namespace cros_disks

#endif  // CROS_DISKS_ERROR_LOGGER_H_
