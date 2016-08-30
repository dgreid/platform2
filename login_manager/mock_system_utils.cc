// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/mock_system_utils.h"

#include <stdint.h>

#include <string>

#include <base/files/file_util.h>
#include <base/memory/scoped_ptr.h>

namespace login_manager {

MockSystemUtils::MockSystemUtils() {
}

MockSystemUtils::~MockSystemUtils() {
}

bool MockSystemUtils::Exists(const base::FilePath& file) {
  return EnsureTempDir() && real_utils_.Exists(PutInsideTempdir(file));
}

bool MockSystemUtils::AtomicFileWrite(const base::FilePath& file,
                                      const std::string& data) {
  if (!EnsureTempDir())
    return false;
  base::FilePath to_write(PutInsideTempdir(file));
  if (!base::CreateDirectory(to_write.DirName())) {
    PLOG(ERROR) << "Could not recursively create "
                << to_write.DirName().value();
    return false;
  }
  return real_utils_.AtomicFileWrite(to_write, data);
}

bool MockSystemUtils::ReadFileToString(const base::FilePath& file,
                                       std::string* out) {
  return EnsureTempDir() && base::ReadFileToString(PutInsideTempdir(file), out);
}

bool MockSystemUtils::EnsureAndReturnSafeFileSize(const base::FilePath& file,
                                                  int32_t* file_size_32) {
  return (EnsureTempDir() &&
          real_utils_.EnsureAndReturnSafeFileSize(PutInsideTempdir(file),
                                                  file_size_32));
}

bool MockSystemUtils::RemoveDirTree(const base::FilePath& dir) {
  return EnsureTempDir() && real_utils_.RemoveDirTree(PutInsideTempdir(dir));
}

bool MockSystemUtils::RemoveFile(const base::FilePath& file) {
  return EnsureTempDir() && real_utils_.RemoveFile(PutInsideTempdir(file));
}

bool MockSystemUtils::DirectoryExists(const base::FilePath& dir) {
  return EnsureTempDir() && real_utils_.DirectoryExists(PutInsideTempdir(dir));
}

bool MockSystemUtils::CreateTemporaryDirIn(const base::FilePath& parent_dir,
                                           base::FilePath* out_dir) {
  base::FilePath new_dir;
  if (EnsureTempDir() && real_utils_.CreateTemporaryDirIn(
          PutInsideTempdir(parent_dir), &new_dir)) {
    out_dir->clear();
    // Remove |temp_dir_| part from |new_dir| and store it in |out_dir|.
    return temp_dir_.path().AppendRelativePath(new_dir, out_dir);
  }
  return false;
}

bool MockSystemUtils::RenameDir(const base::FilePath& source,
                                const base::FilePath& target) {
  return (EnsureTempDir() && real_utils_.RenameDir(
      PutInsideTempdir(source), PutInsideTempdir(target)));
}

bool MockSystemUtils::CreateDir(const base::FilePath& dir) {
  return EnsureTempDir() && real_utils_.CreateDir(PutInsideTempdir(dir));
}

bool MockSystemUtils::IsDirectoryEmpty(const base::FilePath& dir) {
  return EnsureTempDir() && real_utils_.IsDirectoryEmpty(PutInsideTempdir(dir));
}

bool MockSystemUtils::GetUniqueFilenameInWriteOnlyTempDir(
    base::FilePath* temp_file_path) {
  return CreateReadOnlyFileInTempDir(temp_file_path);
}

bool MockSystemUtils::CreateReadOnlyFileInTempDir(
    base::FilePath* temp_file_path) {
  *temp_file_path = GetUniqueFilename();
  return !temp_file_path->empty();
}

base::FilePath MockSystemUtils::GetUniqueFilename() {
  if (unique_file_path_.empty()) {
    if (EnsureTempDir() &&
        !base::CreateTemporaryFileInDir(temp_dir_.path(), &unique_file_path_)) {
      PLOG(ERROR) << "Could not create file in " << temp_dir_.path().value();
      unique_file_path_.clear();
    }
  }
  return unique_file_path_;
}

bool MockSystemUtils::EnsureTempDir() {
  if (!temp_dir_.IsValid() && !temp_dir_.CreateUniqueTempDir()) {
    PLOG(ERROR) << "Could not create temp dir";
    return false;
  }
  return true;
}

base::FilePath MockSystemUtils::PutInsideTempdir(const base::FilePath& path) {
  base::FilePath to_append(path);
  while (to_append.IsAbsolute()) {
    std::string ascii(path.MaybeAsASCII());
    to_append = base::FilePath(ascii.substr(1, std::string::npos));
  }
  base::FilePath to_return(temp_dir_.path().Append(to_append));
  return to_return;
}

}  // namespace login_manager
