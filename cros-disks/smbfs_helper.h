// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_SMBFS_HELPER_H_
#define CROS_DISKS_SMBFS_HELPER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>

#include "cros-disks/fuse_helper.h"

namespace cros_disks {

class Platform;

// A helper for mounting SmbFs.
//
// SmbFs URIs are of the form:
// smbfs://mojo_id
//
// |mojo_id| is an opaque string, which is the string representation of a
// base::UnguessableToken created by calling base::UnguessableToken::ToString().
// It is used to bootstrap a Mojo IPC connection to Chrome.
class SmbfsHelper : public FUSEHelper {
 public:
  SmbfsHelper(const Platform* platform, brillo::ProcessReaper* process_reaper);
  SmbfsHelper(const SmbfsHelper&) = delete;
  SmbfsHelper& operator=(const SmbfsHelper&) = delete;

  ~SmbfsHelper() override;

  // FUSEHelper overrides:
  std::unique_ptr<FUSEMounter> CreateMounter(
      const base::FilePath& working_dir,
      const Uri& source,
      const base::FilePath& target_path,
      const std::vector<std::string>& options) const override;

 private:
  friend class SmbfsHelperTest;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_SMBFS_HELPER_H_
