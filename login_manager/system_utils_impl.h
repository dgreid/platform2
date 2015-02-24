// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_SYSTEM_UTILS_IMPL_H_
#define LOGIN_MANAGER_SYSTEM_UTILS_IMPL_H_

#include "login_manager/system_utils.h"

#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_ptr.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>

namespace base {
class FilePath;
}

namespace login_manager {

class SystemUtilsImpl : public SystemUtils {
 public:
  SystemUtilsImpl();
  virtual ~SystemUtilsImpl();

  int kill(pid_t pid, uid_t owner, int signal) override;
  time_t time(time_t* t) override;
  pid_t fork() override;
  int IsDevMode() override;
  bool ProcessGroupIsGone(pid_t child_spec, base::TimeDelta timeout) override;

  bool EnsureAndReturnSafeFileSize(const base::FilePath& file,
                                   int32_t* file_size_32) override;
  bool Exists(const base::FilePath& file) override;
  bool CreateReadOnlyFileInTempDir(base::FilePath* temp_file) override;
  bool GetUniqueFilenameInWriteOnlyTempDir(
      base::FilePath* temp_file_path) override;
  bool RemoveFile(const base::FilePath& filename) override;
  bool AtomicFileWrite(const base::FilePath& filename,
                       const std::string& data) override;

 private:
  // If this file exists on the next boot, the stateful partition will be wiped.
  static const char kResetFile[];

  base::ScopedTempDir temp_dir_;

  DISALLOW_COPY_AND_ASSIGN(SystemUtilsImpl);
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_SYSTEM_UTILS_IMPL_H_
