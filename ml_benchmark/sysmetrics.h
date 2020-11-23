// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_BENCHMARK_SYSMETRICS_H_
#define ML_BENCHMARK_SYSMETRICS_H_

namespace ml_benchmark {

// Reads the 'VmSize:' value from /proc/self/status
// returns:            The virtual memory size of the current process in bytes.
int GetVMSizeBytes();

// Reads the 'VmPeak:' value from /proc/self/status
// returns:            The highest virtual memory size of the current process.
int GetVMPeakBytes();

}  // namespace ml_benchmark

#endif  // ML_BENCHMARK_SYSMETRICS_H_
