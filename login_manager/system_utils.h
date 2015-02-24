// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_SYSTEM_UTILS_H_
#define LOGIN_MANAGER_SYSTEM_UTILS_H_

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

struct DBusPendingCall;

namespace login_manager {

class ScopedDBusPendingCall;

class SystemUtils {
 public:
  SystemUtils() {}
  virtual ~SystemUtils() {}

  // Sends |signal| to |pid|, with uid and euid set to |owner|.
  // NOTE: Your saved UID is kept unchanged.  If you expect to drop and regain
  // root privs, MAKE SURE YOUR suid == 0.
  virtual int kill(pid_t pid, uid_t owner, int signal) = 0;

  // Returns time, in seconds, since the unix epoch.
  virtual time_t time(time_t* t) = 0;

  // Forks a new process.  In the parent, returns child's pid.  In child, 0.
  virtual pid_t fork() = 0;

  // Returns 0 if normal mode, 1 if developer mode, -1 if error.
  virtual int IsDevMode() = 0;

  // Returns: true if process group specified by |child_spec| exited,
  //          false if we time out.
  virtual bool ProcessGroupIsGone(pid_t child_spec,
                                  base::TimeDelta timeout) = 0;

  virtual bool EnsureAndReturnSafeFileSize(const base::FilePath& file,
                                           int32_t* file_size_32) = 0;

  // Returns whether a file exists.
  virtual bool Exists(const base::FilePath& file) = 0;

  // Creates a uniquely-named read-only file under |dir|.
  // Upon success, sets |temp_file| and returns true. Upon failure, |temp_file|
  // remains untouched.
  virtual bool CreateReadOnlyFileInTempDir(base::FilePath* temp_file) = 0;

  // Generates a guaranteed-unique filename in a write-only temp dir.
  // Returns false upon failure.
  virtual bool GetUniqueFilenameInWriteOnlyTempDir(
      base::FilePath* temp_file_path) = 0;

  // Removes a file.
  virtual bool RemoveFile(const base::FilePath& filename) = 0;

  // Atomically writes the given buffer into the file, overwriting any
  // data that was previously there.  Returns true upon success, false
  // otherwise.
  virtual bool AtomicFileWrite(const base::FilePath& filename,
                               const std::string& data) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(SystemUtils);
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_SYSTEM_UTILS_H_
