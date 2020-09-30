// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/common/util.h"

#include <stdint.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "power_manager/common/power_constants.h"

namespace power_manager {
namespace util {
namespace {

// Reads a string value from |path| and uses StringToType function to convert it
// to |value_out|. Logs an error and returns false on failure.
template <typename T>
bool ReadTypeFile(const base::FilePath& path,
                  bool (*StringToType)(base::StringPiece, T*),
                  T* value_out) {
  DCHECK(value_out);

  std::string str;
  if (!ReadStringFile(path, &str))
    return false;

  if (!StringToType(str, value_out)) {
    LOG(ERROR) << "Unable to parse \"" << str << "\" from " << path.value();
    return false;
  }
  return true;
}

// Wrapper of string to number conversion function to absorb the libchrome
// update signature change.
// TODO(crbug.com/909719): Remove them after the uprev.
bool StringToInt64(base::StringPiece s, int64_t* out) {
  return base::StringToInt64(s, out);
}

bool StringToUint64(base::StringPiece s, uint64_t* out) {
  return base::StringToUint64(s, out);
}

bool HexStringToUInt(base::StringPiece s, uint32_t* out) {
  return base::HexStringToUInt(s, out);
}

}  // namespace

double ClampPercent(double percent) {
  return std::max(0.0, std::min(100.0, percent));
}

std::string TimeDeltaToString(base::TimeDelta delta) {
  std::string output;
  if (delta < base::TimeDelta())
    output += "-";

  int64_t total_seconds = llabs(delta.InSeconds());

  const int64_t hours = total_seconds / 3600;
  if (hours)
    output += base::StringPrintf("%" PRId64 "h", hours);

  const int64_t minutes = (total_seconds % 3600) / 60;
  if (minutes)
    output += base::StringPrintf("%" PRId64 "m", minutes);

  const int64_t seconds = total_seconds % 60;
  if (seconds || !total_seconds)
    output += base::StringPrintf("%" PRId64 "s", seconds);

  return output;
}

bool WriteFileFully(const base::FilePath& filename,
                    const char* data,
                    int size) {
  return base::WriteFile(filename, data, size) == size;
}

bool WriteInt64File(const base::FilePath& path, int64_t value) {
  std::string buf = base::NumberToString(value);
  if (!WriteFileFully(path, buf.data(), buf.size())) {
    PLOG(ERROR) << "Unable to write \"" << buf << "\" to " << path.value();
    return false;
  }
  return true;
}

bool ReadStringFile(const base::FilePath& path, std::string* value_out) {
  DCHECK(value_out);

  if (!base::ReadFileToString(path, value_out)) {
    PLOG(ERROR) << "Unable to read from " << path.value();
    return false;
  }
  base::TrimWhitespaceASCII(*value_out, base::TRIM_TRAILING, value_out);
  return true;
}

bool ReadInt64File(const base::FilePath& path, int64_t* value_out) {
  return ReadTypeFile(path, StringToInt64, value_out);
}

bool ReadUint64File(const base::FilePath& path, uint64_t* value_out) {
  return ReadTypeFile(path, StringToUint64, value_out);
}

bool ReadHexUint32File(const base::FilePath& path, uint32_t* value_out) {
  return ReadTypeFile(path, HexStringToUInt, value_out);
}

std::string JoinPaths(const std::vector<base::FilePath>& paths,
                      const std::string& separator) {
  std::string str;
  for (const auto& path : paths)
    str += (str.empty() ? std::string() : separator) + path.value();
  return str;
}

}  // namespace util
}  // namespace power_manager
