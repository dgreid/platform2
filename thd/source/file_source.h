// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THD_SOURCE_FILE_SOURCE_H_
#define THD_SOURCE_FILE_SOURCE_H_

#include "thd/source/source.h"

#include <cstdint>

#include <base/files/file_path.h>
#include <base/macros.h>

namespace thd {

// Source to read files.
//
// Assumes that the file only contains one integer value.
class FileSource : public Source {
 public:
  explicit FileSource(const base::FilePath& file_path);
  FileSource(const FileSource&) = delete;
  FileSource& operator=(const FileSource&) = delete;

  ~FileSource() override;

  // Source:
  bool ReadValue(int64_t* value_out) override;

 private:
  const base::FilePath file_path_;
};

}  // namespace thd

#endif  // THD_SOURCE_FILE_SOURCE_H_
