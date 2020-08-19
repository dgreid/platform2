// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_TYPE_UTILS_H_
#define RUNTIME_PROBE_UTILS_TYPE_UTILS_H_

#include <string>

namespace runtime_probe {
// The following functions are helper functions to convert a string to numeric
// values.  All following functions will first remove leading spaces and then
// pass the remaining string to helper functions in libchrome or standard
// library.  We define these functions because the helper functions only returns
// |true| on perfect conversions, and we need to check which type of failure it
// is when the conversion failed.  The following functions should normalize the
// success case, such that |true| is returned if:
// - Perfect conversion after leading and trailing spaces are removed.

// Converts a string to double.
bool StringToDouble(const std::string& input, double* output);

// Converts a string to int.
bool StringToInt(const std::string& input, int* output);

// Converts a string to int64.
bool StringToInt64(const std::string& input, int64_t* output);

// Converts a hex string to int.
bool HexStringToInt(const std::string& input, int* output);
}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_TYPE_UTILS_H_
