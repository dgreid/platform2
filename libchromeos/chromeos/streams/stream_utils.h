// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCHROMEOS_CHROMEOS_STREAMS_STREAM_UTILS_H_
#define LIBCHROMEOS_CHROMEOS_STREAMS_STREAM_UTILS_H_

#include <base/location.h>
#include <chromeos/chromeos_export.h>
#include <chromeos/streams/stream.h>

namespace chromeos {
namespace stream_utils {

// Generates "Stream closed" error and returns false.
CHROMEOS_EXPORT bool ErrorStreamClosed(
    const tracked_objects::Location& location, ErrorPtr* error);

// Generates "Not supported" error and returns false.
CHROMEOS_EXPORT bool ErrorOperationNotSupported(
    const tracked_objects::Location& location, ErrorPtr* error);

// Generates "Read past end of stream" error and returns false.
CHROMEOS_EXPORT bool ErrorReadPastEndOfStream(
    const tracked_objects::Location& location, ErrorPtr* error);

// Generates "Operation time out" error and returns false.
CHROMEOS_EXPORT bool ErrorOperationTimeout(
    const tracked_objects::Location& location, ErrorPtr* error);

// Checks if |position| + |offset| fit within the constraint of positive
// signed int64_t type. We use uint64_t for absolute stream pointer positions,
// however many implementations, including file-descriptor-based I/O do not
// support the full extent of unsigned 64 bit numbers. So we restrict the file
// positions to what can fit in the signed 64 bit value (that is, we support
// "only" up to 9 exabytes, instead of the possible 18).
// The |location| parameter will be used to report the origin of the error
// if one is generated/triggered.
CHROMEOS_EXPORT bool CheckInt64Overflow(
    const tracked_objects::Location& location,
    uint64_t position,
    int64_t offset,
    ErrorPtr* error);

// Helper function to calculate the stream position based on the current
// stream position and offset. Returns true and the new calculated stream
// position in |new_position| if successful. In case of invalid stream
// position (negative values or out of range of signed 64 bit values), returns
// false and "invalid_parameter" |error|.
// The |location| parameter will be used to report the origin of the error
// if one is generated/triggered.
CHROMEOS_EXPORT bool CalculateStreamPosition(
    const tracked_objects::Location& location,
    int64_t offset,
    Stream::Whence whence,
    uint64_t current_position,
    uint64_t stream_size,
    uint64_t* new_position,
    ErrorPtr* error);

// Checks if |mode| allows read access.
inline bool IsReadAccessMode(Stream::AccessMode mode) {
  return mode == Stream::AccessMode::READ ||
         mode == Stream::AccessMode::READ_WRITE;
}

// Checks if |mode| allows write access.
inline bool IsWriteAccessMode(Stream::AccessMode mode) {
  return mode == Stream::AccessMode::WRITE ||
         mode == Stream::AccessMode::READ_WRITE;
}

// Make the access mode based on read/write rights requested.
inline Stream::AccessMode MakeAccessMode(bool read, bool write) {
  CHECK(read || write);  // Either read or write (or both) must be specified.
  if (read && write)
    return Stream::AccessMode::READ_WRITE;
  return write ? Stream::AccessMode::WRITE : Stream::AccessMode::READ;
}

}  // namespace stream_utils
}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_STREAMS_STREAM_UTILS_H_
