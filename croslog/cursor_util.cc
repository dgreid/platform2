// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/cursor_util.h"

#include <string>

#include <base/strings/string_number_conversions.h>

namespace croslog {

std::string GenerateCursor(const base::Time& time) {
  int64_t time_value = time.ToDeltaSinceWindowsEpoch().InMicroseconds();
  uint8_t buffer[sizeof(int64_t)];
  std::reverse_copy(reinterpret_cast<uint8_t*>(&time_value),
                    reinterpret_cast<uint8_t*>(&time_value + 1), buffer);
  return "time=" + base::HexEncode(buffer, sizeof(int64_t));
}

bool ParseCursor(const std::string& cursor_str, base::Time* output) {
  if (cursor_str.size() != (sizeof(int64_t) * 2 + 5))
    return false;

  if (cursor_str.rfind("time=", 0) != 0)
    return false;

  int64_t time_value;
  if (!base::HexStringToInt64(
          base::StringPiece(cursor_str).substr(5, sizeof(int64_t) * 2),
          &time_value))
    return false;

  *output = base::Time::FromDeltaSinceWindowsEpoch(
      base::TimeDelta::FromMicroseconds(time_value));
  return true;
}

}  // namespace croslog
