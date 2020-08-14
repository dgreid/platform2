// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_DRIVEFS_HELPER_H_
#define CROS_DISKS_DRIVEFS_HELPER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>

#include "cros-disks/fuse_helper.h"

namespace cros_disks {

class Platform;

// A helper for mounting DriveFS.
//
// DriveFS URIs are of the form:
// drivefs://identity
//
// The datadir option is required. It is the path DriveFS should use for its
// data. It must be an absolute path without parent directory references. This
// is enforced by |GetValidatedDataDir()| as part of |CreateMounter()|. Further,
// |SetupDirectoryForFUSEAccess()| enforces that datadir either does not exist,
// or already has the right owner (fuse-drivefs:chronos-access).
//
// |identity| is an opaque string. In particular it's a string representation of
// a base::UnguessableToken, used to lookup a pending DriveFS mount in Chrome.
class DrivefsHelper : public FUSEHelper {
 public:
  DrivefsHelper(const Platform* platform,
                brillo::ProcessReaper* process_reaper);
  ~DrivefsHelper() override;

  // FUSEHelper overrides:
  std::unique_ptr<FUSEMounter> CreateMounter(
      const base::FilePath& working_dir,
      const Uri& source,
      const base::FilePath& target_path,
      const std::vector<std::string>& options) const override;

 protected:
  // Make sure the dir is set up to be used by FUSE's helper user.
  virtual bool CheckDataDirPermissions(const base::FilePath& dirpath) const;

  // Ensure |path| is accessible by chronos.
  virtual bool CheckMyFilesPermissions(const base::FilePath& path) const;

 private:
  friend class DrivefsHelperTest;

  // Returns the directory specified by |prefix| from the options if one is
  // present and valid. Returns an empty path on failure.
  base::FilePath GetValidatedDirectory(const std::vector<std::string>& options,
                                       base::StringPiece prefix) const;

  DISALLOW_COPY_AND_ASSIGN(DrivefsHelper);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_DRIVEFS_HELPER_H_
