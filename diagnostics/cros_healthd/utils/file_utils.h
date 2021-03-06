// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_FILE_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_FILE_UTILS_H_

#include <string>

#include <base/files/file_path.h>
#include <base/strings/string_piece.h>

namespace diagnostics {

// Reads the contents of |filename| within |directory| into |out|, trimming
// trailing whitespace. Returns true on success.
bool ReadAndTrimString(const base::FilePath& directory,
                       const std::string& filename,
                       std::string* out);

// Like ReadAndTrimString() above, but expects |file_path| to be the full path
// to the file to be read.
bool ReadAndTrimString(const base::FilePath& file_path, std::string* out);

// Like ReadInteger() above, but expects |file_path| to be the full path to the
// file to be read.
template <typename T>
bool ReadInteger(const base::FilePath& file_path,
                 bool (*StringToInteger)(base::StringPiece, T*),
                 T* out) {
  DCHECK(StringToInteger);
  DCHECK(out);

  std::string buffer;
  if (!ReadAndTrimString(file_path, &buffer))
    return false;

  return StringToInteger(buffer, out);
}

// Reads an integer value from a file and converts it using the provided
// function. Returns true on success.
template <typename T>
bool ReadInteger(const base::FilePath& directory,
                 const std::string& filename,
                 bool (*StringToInteger)(base::StringPiece, T*),
                 T* out) {
  return ReadInteger(directory.AppendASCII(filename), StringToInteger, out);
}

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_FILE_UTILS_H_
