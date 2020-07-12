// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_MOUNTER_H_
#define CROS_DISKS_MOUNTER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <chromeos/dbus/service_constants.h>

#include "cros-disks/mount_options.h"

namespace cros_disks {

class MountPoint;

// Interface for mounting a given filesystem.
class Mounter {
 public:
  explicit Mounter(std::string filesystem_type);
  virtual ~Mounter();

  // Mounts the filesystem. On failure returns nullptr and |error| is
  // set accordingly. Both |source| and |options| are just some strings
  // that can be interpreted by this mounter.
  virtual std::unique_ptr<MountPoint> Mount(const std::string& source,
                                            const base::FilePath& target_path,
                                            std::vector<std::string> options,
                                            MountErrorType* error) const = 0;

  // Whether this mounter is able to mount given |source| with provided
  // |options|. If so - it may suggest a directory name for the mount point
  // to be created. Note that in many cases it's impossible to tell beforehand
  // if the particular source is mountable so it may blanketly return true for
  // any arguments.
  virtual bool CanMount(const std::string& source,
                        const std::vector<std::string>& options,
                        base::FilePath* suggested_dir_name) const = 0;

  const std::string& filesystem_type() const { return filesystem_type_; }

 private:
  // Type of filesystem to mount.
  const std::string filesystem_type_;

  DISALLOW_COPY_AND_ASSIGN(Mounter);
};

// Temporary adaptor to keep some signatures compatible with old implementation
// and minimize churn.
// TODO(crbug.com/933018): Remove when done.
class MounterCompat : public Mounter {
 public:
  MounterCompat(std::unique_ptr<Mounter> mounter, MountOptions mount_options);
  MounterCompat(std::string filesystem_type, MountOptions mount_options);
  ~MounterCompat() override;

  // Mounter overrides.
  std::unique_ptr<MountPoint> Mount(const std::string& source,
                                    const base::FilePath& target_path,
                                    std::vector<std::string> options,
                                    MountErrorType* error) const override;
  // Always returns true.
  bool CanMount(const std::string& source,
                const std::vector<std::string>& options,
                base::FilePath* suggested_dir_name) const override;

  const MountOptions& mount_options() const { return mount_options_; }

 private:
  const std::unique_ptr<Mounter> mounter_;
  const MountOptions mount_options_;

  DISALLOW_COPY_AND_ASSIGN(MounterCompat);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_MOUNTER_H_
