// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THD_MECHANISM_FILE_WRITE_MECHANISM_H_
#define THD_MECHANISM_FILE_WRITE_MECHANISM_H_

#include "thd/mechanism/mechanism.h"

#include <cstdint>
#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>

namespace thd {

// Mechanism to write integers into files. Used to control sysfs
// accessible knobs.
class FileWriteMechanism : public Mechanism {
 public:
  FileWriteMechanism(int64_t max_level,
                     int64_t min_level,
                     int64_t default_level,
                     const std::string& name,
                     const base::FilePath& path);
  FileWriteMechanism(const FileWriteMechanism&) = delete;
  FileWriteMechanism& operator=(const FileWriteMechanism&) = delete;

  ~FileWriteMechanism() override;

  // Mechanism:
  bool SetLevel(int64_t level) override;
  int64_t GetMaxLevel() override;
  int64_t GetMinLevel() override;
  int64_t GetDefaultLevel() override;

 private:
  const int64_t max_level_;
  const int64_t min_level_;
  const int64_t default_level_;
  const std::string name_;
  const base::FilePath path_;
};

}  // namespace thd

#endif  // THD_MECHANISM_FILE_WRITE_MECHANISM_H_
