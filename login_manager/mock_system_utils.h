// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_SYSTEM_UTILS_H_
#define LOGIN_MANAGER_MOCK_SYSTEM_UTILS_H_

#include "login_manager/system_utils.h"

#include <stdint.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_ptr.h>
#include <base/memory/scoped_vector.h>
#include <base/time/time.h>
#include <gmock/gmock.h>

#include "login_manager/system_utils_impl.h"

namespace login_manager {

class MockSystemUtils : public SystemUtils {
 public:
  MockSystemUtils();
  virtual ~MockSystemUtils();
  MOCK_METHOD3(kill, int(pid_t pid, uid_t uid, int signal));
  MOCK_METHOD1(time, time_t(time_t*));  // NOLINT
  MOCK_METHOD0(fork, pid_t(void));
  MOCK_METHOD0(IsDevMode, int(void));
  MOCK_METHOD2(ProcessGroupIsGone, bool(pid_t child_spec,
                                        base::TimeDelta timeout));

  // All filesystem-touching methods write to a ScopedTempDir that's owned by
  // this class.
  bool Exists(const base::FilePath& file) override;
  bool DirectoryExists(const base::FilePath& dir) override;
  bool AtomicFileWrite(const base::FilePath& file,
                       const std::string& data) override;
  bool CreateTemporaryDirIn(const base::FilePath& parent_dir,
                            base::FilePath* out_dir) override;
  bool CreateDir(const base::FilePath& dir) override;
  bool ReadFileToString(const base::FilePath& file, std::string* out);
  bool EnsureAndReturnSafeFileSize(const base::FilePath& file,
                                   int32_t* file_size_32) override;
  bool RemoveDirTree(const base::FilePath& dir) override;
  bool RemoveFile(const base::FilePath& file) override;
  bool RenameDir(const base::FilePath& source,
                 const base::FilePath& target) override;

  bool GetUniqueFilenameInWriteOnlyTempDir(
      base::FilePath* temp_file_path) override;
  bool CreateReadOnlyFileInTempDir(base::FilePath* temp_file_path) override;
  // Get filename to be returned by the above. Returns full path to the file.
  // An empty path is returned on failure.
  base::FilePath GetUniqueFilename();

  MOCK_CONST_METHOD1(AppendToClobberLog, void(const char*));

 private:
  // Ensures that temp_dir_ (inherited from SystemUtils) exists.
  // Returns true if it already does, or if it can be created. Else, False.
  // This call is idempotent
  bool EnsureTempDir();

  // Returns the given path "chrooted" inside temp_dir_, so to speak.
  // Ex: /var/run/foo -> /tmp/.org.Chromium.whatever/var/run/foo
  base::FilePath PutInsideTempdir(const base::FilePath& path);

  // To fake out GetUniqueFilenameInWriteOnlyTempDir() and
  // CreateReadOnlyFileInTempDir(), we just generate a single "unique"
  // path inside the temp dir managed by this class, store it here,
  // and return it whenever asked.
  base::FilePath unique_file_path_;

  SystemUtilsImpl real_utils_;
  base::ScopedTempDir temp_dir_;
  DISALLOW_COPY_AND_ASSIGN(MockSystemUtils);
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_SYSTEM_UTILS_H_
