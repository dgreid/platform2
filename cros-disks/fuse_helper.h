// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_FUSE_HELPER_H_
#define CROS_DISKS_FUSE_HELPER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest_prod.h>

namespace brillo {
class ProcessReaper;
}  // namespace brillo

namespace cros_disks {

class FUSEMounter;
class Platform;
class Uri;

// Base class to simplify calling of individual mounters based on
// specific conventions for a particular userspace FUSE implementation.
// TODO(dats): Remove this class. This should be in the corresponding
// instance of FUSEMounter.
class FUSEHelper {
 public:
  // OS user that will access files provided by this module.
  static const char kFilesUser[];

  // OS group that will access files provided by this module.
  static const char kFilesGroup[];

  // FUSE kernel-level option allowing access to the mount by UIDs different
  // from the one that FUSE helper is being run. Relevant if FUSE helper is
  // being run in a sandbox under different UID.
  static const char kOptionAllowOther[];

  // Enable permission checking by the kernel instead of FUSE helper itself.
  static const char kOptionDefaultPermissions[];

  FUSEHelper(const std::string& fuse_type,
             const Platform* platform,
             brillo::ProcessReaper* process_reaper,
             const base::FilePath& mount_program_path,
             const std::string& mount_user);
  FUSEHelper(const FUSEHelper&) = delete;
  FUSEHelper& operator=(const FUSEHelper&) = delete;

  virtual ~FUSEHelper();

  const std::string& type() const { return fuse_type_; }
  const std::string& user() const { return mount_user_; }
  const Platform* platform() const { return platform_; }
  const base::FilePath& program_path() const { return mount_program_path_; }

  // Whether this helper is able to handle this kind of source.
  // Default implementation just compares scheme of the URI with FUSE type
  // and checks that there is some path in the URI.
  virtual bool CanMount(const Uri& source) const;

  // Derives suggested directory name for the mount point from the source.
  // Default implementation takes path part of the URI and escapes path
  // characters..
  virtual std::string GetTargetSuffix(const Uri& source) const;

  // Does preprocessing and conversion of options and source format to be
  // compatible with the FUSE mount program, and returns resulting Mounter.
  // |working_dir| is a temporary writable directory that may be used by
  // this invocation of the mounter process.
  virtual std::unique_ptr<FUSEMounter> CreateMounter(
      const base::FilePath& working_dir,
      const Uri& source,
      const base::FilePath& target_path,
      const std::vector<std::string>& options) const;

 protected:
  brillo::ProcessReaper* process_reaper() const { return process_reaper_; }

 private:
  const std::string fuse_type_;
  const Platform* const platform_;
  brillo::ProcessReaper* const process_reaper_;
  const base::FilePath mount_program_path_;
  const std::string mount_user_;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_FUSE_HELPER_H_
