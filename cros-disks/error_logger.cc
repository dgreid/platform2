// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/error_logger.h"

#include <type_traits>

namespace cros_disks {

std::ostream& operator<<(std::ostream& out, const FormatErrorType error) {
  switch (error) {
    case FORMAT_ERROR_NONE:
      return out << "FORMAT_ERROR_NONE";
    case FORMAT_ERROR_UNKNOWN:
      return out << "FORMAT_ERROR_UNKNOWN";
    case FORMAT_ERROR_INTERNAL:
      return out << "FORMAT_ERROR_INTERNAL";
    case FORMAT_ERROR_INVALID_DEVICE_PATH:
      return out << "FORMAT_ERROR_INVALID_DEVICE_PATH";
    case FORMAT_ERROR_DEVICE_BEING_FORMATTED:
      return out << "FORMAT_ERROR_DEVICE_BEING_FORMATTED";
    case FORMAT_ERROR_UNSUPPORTED_FILESYSTEM:
      return out << "FORMAT_ERROR_UNSUPPORTED_FILESYSTEM";
    case FORMAT_ERROR_FORMAT_PROGRAM_NOT_FOUND:
      return out << "FORMAT_ERROR_FORMAT_PROGRAM_NOT_FOUND";
    case FORMAT_ERROR_FORMAT_PROGRAM_FAILED:
      return out << "FORMAT_ERROR_FORMAT_PROGRAM_FAILED";
    case FORMAT_ERROR_DEVICE_NOT_ALLOWED:
      return out << "FORMAT_ERROR_DEVICE_NOT_ALLOWED";
    case FORMAT_ERROR_INVALID_OPTIONS:
      return out << "FORMAT_ERROR_INVALID_OPTIONS";
    case FORMAT_ERROR_LONG_NAME:
      return out << "FORMAT_ERROR_LONG_NAME";
    case FORMAT_ERROR_INVALID_CHARACTER:
      return out << "FORMAT_ERROR_INVALID_CHARACTER";
    default:
      return out << "FORMAT_ERROR_"
                 << static_cast<std::underlying_type_t<FormatErrorType>>(error);
  }
}

std::ostream& operator<<(std::ostream& out, const MountErrorType error) {
  switch (error) {
    case MOUNT_ERROR_NONE:
      return out << "MOUNT_ERROR_NONE";
    case MOUNT_ERROR_UNKNOWN:
      return out << "MOUNT_ERROR_UNKNOWN";
    case MOUNT_ERROR_INTERNAL:
      return out << "MOUNT_ERROR_INTERNAL";
    case MOUNT_ERROR_INVALID_ARGUMENT:
      return out << "MOUNT_ERROR_INVALID_ARGUMENT";
    case MOUNT_ERROR_INVALID_PATH:
      return out << "MOUNT_ERROR_INVALID_PATH";
    case MOUNT_ERROR_PATH_ALREADY_MOUNTED:
      return out << "MOUNT_ERROR_PATH_ALREADY_MOUNTED";
    case MOUNT_ERROR_PATH_NOT_MOUNTED:
      return out << "MOUNT_ERROR_PATH_NOT_MOUNTED";
    case MOUNT_ERROR_DIRECTORY_CREATION_FAILED:
      return out << "MOUNT_ERROR_DIRECTORY_CREATION_FAILED";
    case MOUNT_ERROR_INVALID_MOUNT_OPTIONS:
      return out << "MOUNT_ERROR_INVALID_MOUNT_OPTIONS";
    case MOUNT_ERROR_INVALID_UNMOUNT_OPTIONS:
      return out << "MOUNT_ERROR_INVALID_UNMOUNT_OPTIONS";
    case MOUNT_ERROR_INSUFFICIENT_PERMISSIONS:
      return out << "MOUNT_ERROR_INSUFFICIENT_PERMISSIONS";
    case MOUNT_ERROR_MOUNT_PROGRAM_NOT_FOUND:
      return out << "MOUNT_ERROR_MOUNT_PROGRAM_NOT_FOUND";
    case MOUNT_ERROR_MOUNT_PROGRAM_FAILED:
      return out << "MOUNT_ERROR_MOUNT_PROGRAM_FAILED";
    case MOUNT_ERROR_NEED_PASSWORD:
      return out << "MOUNT_ERROR_NEED_PASSWORD";
    case MOUNT_ERROR_INVALID_DEVICE_PATH:
      return out << "MOUNT_ERROR_INVALID_DEVICE_PATH";
    case MOUNT_ERROR_UNKNOWN_FILESYSTEM:
      return out << "MOUNT_ERROR_UNKNOWN_FILESYSTEM";
    case MOUNT_ERROR_UNSUPPORTED_FILESYSTEM:
      return out << "MOUNT_ERROR_UNSUPPORTED_FILESYSTEM";
    case MOUNT_ERROR_INVALID_ARCHIVE:
      return out << "MOUNT_ERROR_INVALID_ARCHIVE";
    case MOUNT_ERROR_UNSUPPORTED_ARCHIVE:
      return out << "MOUNT_ERROR_UNSUPPORTED_ARCHIVE";
    default:
      return out << "MOUNT_ERROR_"
                 << static_cast<std::underlying_type_t<MountErrorType>>(error);
  }
}

std::ostream& operator<<(std::ostream& out, const RenameErrorType error) {
  switch (error) {
    case RENAME_ERROR_NONE:
      return out << "RENAME_ERROR_NONE";
    case RENAME_ERROR_UNKNOWN:
      return out << "RENAME_ERROR_UNKNOWN";
    case RENAME_ERROR_INTERNAL:
      return out << "RENAME_ERROR_INTERNAL";
    case RENAME_ERROR_INVALID_DEVICE_PATH:
      return out << "RENAME_ERROR_INVALID_DEVICE_PATH";
    case RENAME_ERROR_DEVICE_BEING_RENAMED:
      return out << "RENAME_ERROR_DEVICE_BEING_RENAMED";
    case RENAME_ERROR_UNSUPPORTED_FILESYSTEM:
      return out << "RENAME_ERROR_UNSUPPORTED_FILESYSTEM";
    case RENAME_ERROR_RENAME_PROGRAM_NOT_FOUND:
      return out << "RENAME_ERROR_RENAME_PROGRAM_NOT_FOUND";
    case RENAME_ERROR_RENAME_PROGRAM_FAILED:
      return out << "RENAME_ERROR_RENAME_PROGRAM_FAILED";
    case RENAME_ERROR_DEVICE_NOT_ALLOWED:
      return out << "RENAME_ERROR_DEVICE_NOT_ALLOWED";
    case RENAME_ERROR_LONG_NAME:
      return out << "RENAME_ERROR_LONG_NAME";
    case RENAME_ERROR_INVALID_CHARACTER:
      return out << "RENAME_ERROR_INVALID_CHARACTER";
    default:
      return out << "RENAME_ERROR_"
                 << static_cast<std::underlying_type_t<RenameErrorType>>(error);
  }
}

}  // namespace cros_disks
