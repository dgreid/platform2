// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEMS_SETUP_DELEGATE_H_
#define MEMS_SETUP_DELEGATE_H_

#include <unistd.h>

#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/optional.h>

namespace mems_setup {

class Delegate {
 public:
  virtual ~Delegate() = default;

  virtual base::Optional<std::string> ReadVpdValue(const std::string& key) = 0;
  virtual bool ProbeKernelModule(const std::string& module) = 0;

  virtual bool Exists(const base::FilePath&) = 0;

  virtual base::Optional<gid_t> FindGroupId(const char* group) = 0;

  virtual int GetPermissions(const base::FilePath& path) = 0;
  virtual bool SetPermissions(const base::FilePath& path, int mode) = 0;

  virtual bool SetOwnership(const base::FilePath& path,
                            uid_t user,
                            gid_t group) = 0;

 protected:
  Delegate() = default;
  Delegate(const Delegate&) = delete;
  Delegate& operator=(const Delegate&) = delete;
};

}  // namespace mems_setup

#endif  // MEMS_SETUP_DELEGATE_H_
