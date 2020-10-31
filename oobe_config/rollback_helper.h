// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_ROLLBACK_HELPER_H_
#define OOBE_CONFIG_ROLLBACK_HELPER_H_

#include <set>
#include <string>

#include <base/files/file_path.h>

namespace oobe_config {

// Prepares the files for saving the configuration. This must run as the root
// user.
// |root_path| specifies a path prefix, used for testing, otherwise empty.
// |ignore_permissions_for_testing| skips the permission setting steps, use
// only for testing.
// Returns true if succeeded.
bool PrepareSave(const base::FilePath& root_path,
                 bool ignore_permissions_for_testing);

// Places the files from the temporary directory to their final places after
// rollback_restore did run.
// |root_path| specifies a path prefix, used for testing, otherwise empty.
// |ignore_permissions_for_testing| skips the permission setting steps, use
// only for testing.
// Returns true on success. Not being in the correct stage is considered success
// as well.
bool FinishRestore(const base::FilePath& root_path,
                   bool ignore_permissions_for_testing);

// Adds the prefix of the temp directory to the absolute path.
base::FilePath PrefixAbsolutePath(const base::FilePath& prefix,
                                  const base::FilePath& file_path);

// Tries to copy a file, prints a warning if it didn't succeed.
void TryFileCopy(const base::FilePath& source,
                 const base::FilePath& destination);

bool IsSymlink(const base::FilePath& path);

bool CopyFileAndSetPermissions(const base::FilePath& source,
                               const base::FilePath& destination,
                               const std::string& owner_username,
                               mode_t permissions,
                               bool ignore_permissions_for_testing);

bool GetUidGid(const std::string& user, uid_t* uid, gid_t* gid);
bool GetGid(const std::string& group, gid_t* gid);

// Deletes all files under /var/lib/oobe_config_restore except those in
// |excluded_files|.
// Deletes the preserved data on the unencrypted stateful partition.
// |root_path| is the path prefix used for testing.
void CleanupRestoreFiles(const base::FilePath& root_path,
                         const std::set<std::string>& excluded_files);

}  // namespace oobe_config

#endif  // OOBE_CONFIG_ROLLBACK_HELPER_H_
