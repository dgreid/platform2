// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/system_utils_impl.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <limits>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/process.h>
#include <chromeos/dbus/service_constants.h>

using std::string;
using std::vector;

namespace login_manager {

SystemUtilsImpl::SystemUtilsImpl() {
}
SystemUtilsImpl::~SystemUtilsImpl() {
}

int SystemUtilsImpl::IsDevMode() {
  int dev_mode_code = system("crossystem 'cros_debug?0'");
  if (WIFEXITED(dev_mode_code)) {
    return WEXITSTATUS(dev_mode_code);
  }
  return -1;
}

int SystemUtilsImpl::kill(pid_t pid, uid_t owner, int signal) {
  LOG(INFO) << "Sending " << signal << " to " << pid << " as " << owner;
  uid_t uid, euid, suid;
  getresuid(&uid, &euid, &suid);
  if (setresuid(owner, owner, -1)) {
    PLOG(ERROR) << "Couldn't assume uid " << owner;
    return -1;
  }
  int ret = ::kill(pid, signal);
  if (setresuid(uid, euid, -1)) {
    PLOG(ERROR) << "Couldn't return to root";
    return -1;
  }
  return ret;
}

time_t SystemUtilsImpl::time(time_t* t) {
  return ::time(t);
}

pid_t SystemUtilsImpl::fork() {
  return ::fork();
}

bool SystemUtilsImpl::ProcessGroupIsGone(pid_t child_spec,
                                         base::TimeDelta timeout) {
  base::TimeTicks start = base::TimeTicks::Now();
  base::TimeDelta elapsed;
  int ret;

  DCHECK_GE(timeout.InSeconds(), 0);
  DCHECK_LE(timeout.InSeconds(),
            static_cast<int64_t>(std::numeric_limits<int>::max()));
  alarm(static_cast<int32_t>(timeout.InSeconds()));
  do {
    errno = 0;
    ret = ::waitpid(-child_spec, NULL, 0);
    elapsed = base::TimeTicks::Now() - start;
  } while (ret > 0 || (errno == EINTR && elapsed < timeout));

  // Once we exit the loop, we know there was an error.
  alarm(0);
  return errno == ECHILD;  // EINTR means we timed out.
}

bool SystemUtilsImpl::EnsureAndReturnSafeFileSize(const base::FilePath& file,
                                                  int32_t* file_size_32) {
  // Get the file size (must fit in a 32 bit int for NSS).
  int64_t file_size;
  if (!base::GetFileSize(file, &file_size)) {
    LOG(ERROR) << "Could not get size of " << file.value();
    return false;
  }
  if (file_size > static_cast<int64_t>(std::numeric_limits<int>::max())) {
    LOG(ERROR) << file.value() << "is " << file_size << "bytes!!!  Too big!";
    return false;
  }
  *file_size_32 = static_cast<int32_t>(file_size);
  return true;
}

bool SystemUtilsImpl::Exists(const base::FilePath& file) {
  return base::PathExists(file);
}

bool SystemUtilsImpl::CreateReadOnlyFileInTempDir(base::FilePath* temp_file) {
  if (!temp_dir_.IsValid() && !temp_dir_.CreateUniqueTempDir())
    return false;
  base::FilePath local_temp_file;
  if (base::CreateTemporaryFileInDir(temp_dir_.path(), &local_temp_file)) {
    if (chmod(local_temp_file.value().c_str(), 0644) == 0) {
      *temp_file = local_temp_file;
      return true;
    } else {
      PLOG(ERROR) << "Can't chmod " << local_temp_file.value() << " to 0644.";
    }
    RemoveFile(local_temp_file);
  }
  return false;
}

bool SystemUtilsImpl::GetUniqueFilenameInWriteOnlyTempDir(
    base::FilePath* temp_file_path) {
  // Create a temporary directory to put the testing channel in.
  // It will be made write-only below; we need to be able to read it
  // when trying to create a unique name inside it.
  base::FilePath temp_dir_path;
  if (!base::CreateNewTempDirectory(FILE_PATH_LITERAL(""), &temp_dir_path)) {
    PLOG(ERROR) << "Can't create temp dir";
    return false;
  }
  // Create a temporary file in the temporary directory, to be deleted later.
  // This ensures a unique name.
  if (!base::CreateTemporaryFileInDir(temp_dir_path, temp_file_path)) {
    PLOG(ERROR) << "Can't get temp file name in " << temp_dir_path.value();
    return false;
  }
  // Now, allow access to non-root processes.
  if (chmod(temp_dir_path.value().c_str(), 0333)) {
    PLOG(ERROR) << "Can't chmod " << temp_file_path->value() << " to 0333.";
    return false;
  }
  if (!RemoveFile(*temp_file_path)) {
    PLOG(ERROR) << "Can't clear temp file in " << temp_file_path->value();
    return false;
  }
  return true;
}

bool SystemUtilsImpl::RemoveDirTree(const base::FilePath& dir) {
  if (!base::DirectoryExists(dir))
    return false;
  return base::DeleteFile(dir, true);
}

bool SystemUtilsImpl::RemoveFile(const base::FilePath& filename) {
  if (base::DirectoryExists(filename))
    return false;
  return base::DeleteFile(filename, false);
}

bool SystemUtilsImpl::AtomicFileWrite(const base::FilePath& filename,
                                      const std::string& data) {
  return (
      base::ImportantFileWriter::WriteFileAtomically(filename, data) &&
      base::SetPosixFilePermissions(filename, (S_IRUSR | S_IWUSR | S_IROTH)));
}

bool SystemUtilsImpl::DirectoryExists(const base::FilePath& dir) {
  return base::DirectoryExists(dir);
}

bool SystemUtilsImpl::CreateTemporaryDirIn(const base::FilePath& parent_dir,
                                           base::FilePath* out_dir) {
  return base::CreateTemporaryDirInDir(parent_dir, "temp", out_dir);
}

bool SystemUtilsImpl::RenameDir(const base::FilePath& source,
                                const base::FilePath& target) {
  if (!base::DirectoryExists(source))
    return false;
  return base::ReplaceFile(source, target, nullptr);
}

bool SystemUtilsImpl::CreateDir(const base::FilePath& dir) {
  return base::CreateDirectoryAndGetError(dir, nullptr);
}

bool SystemUtilsImpl::IsDirectoryEmpty(const base::FilePath& dir) {
  return !DirectoryExists(dir) || base::IsDirectoryEmpty(dir);
}

}  // namespace login_manager
