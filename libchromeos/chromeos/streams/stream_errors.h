// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCHROMEOS_CHROMEOS_STREAMS_STREAM_ERRORS_H_
#define LIBCHROMEOS_CHROMEOS_STREAMS_STREAM_ERRORS_H_

#include <chromeos/chromeos_export.h>

namespace chromeos {
namespace errors {
namespace stream {

// Error domain for generic stream-based errors.
CHROMEOS_EXPORT extern const char kDomain[];

CHROMEOS_EXPORT extern const char kStreamClosed[];
CHROMEOS_EXPORT extern const char kOperationNotSupported[];
CHROMEOS_EXPORT extern const char kPartialData[];
CHROMEOS_EXPORT extern const char kInvalidParameter[];
CHROMEOS_EXPORT extern const char kTimeout[];

}  // namespace stream
}  // namespace errors
}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_STREAMS_STREAM_ERRORS_H_
