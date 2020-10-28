// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBMEMS_COMMON_TYPES_H_
#define LIBMEMS_COMMON_TYPES_H_

#include <vector>

namespace libmems {

constexpr int kErrorBufferSize = 256;
constexpr int kReadAttrBufferSize = 256;

constexpr char kDeviceIdPrefix[] = "iio:device";
constexpr char kIioSysfsTrigger[] = "iio_sysfs_trigger";
constexpr char kTriggerIdPrefix[] = "trigger";

constexpr char kFrequencyAttr[] = "frequency";
constexpr char kSamplingFrequencyAttr[] = "sampling_frequency";
constexpr char kHWFifoTimeoutAttr[] = "buffer/hwfifo_timeout";
constexpr char kSamplingFrequencyAvailable[] = "sampling_frequency_available";

constexpr double kFrequencyEpsilon = 0.001;  // Hz

constexpr char kRawAttr[] = "raw";
constexpr char kTimestampAttr[] = "timestamp";

constexpr char kSysDevString[] = "/sys/bus/iio/devices";

}  // namespace libmems

#endif  // LIBMEMS_COMMON_TYPES_H_
