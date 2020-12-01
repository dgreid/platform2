// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mems_setup/test_fakes.h"

#include <base/logging.h>

namespace mems_setup {
namespace fakes {

namespace {

constexpr char kAcpiAlsTriggerName[] = "iioservice-acpi-als";

}  // namespace

base::Optional<std::string> FakeDelegate::ReadVpdValue(
    const std::string& name) {
  auto k = vpd_.find(name);
  if (k == vpd_.end())
    return base::nullopt;
  return k->second;
}

bool FakeDelegate::ProbeKernelModule(const std::string& module) {
  probed_modules_.push_back(module);
  return true;
}

bool FakeDelegate::CreateDirectory(const base::FilePath& fp) {
  existing_files_.emplace(fp);

  base::FilePath hrtimer_path("/sys/kernel/config/iio/triggers/hrtimer");
  hrtimer_path = hrtimer_path.Append(kAcpiAlsTriggerName);
  if (mock_context_ && fp == hrtimer_path) {
    mock_context_->AddTrigger(std::make_unique<libmems::fakes::FakeIioDevice>(
        mock_context_, kAcpiAlsTriggerName, 1));
  }
  return true;
}

bool FakeDelegate::Exists(const base::FilePath& fp) {
  return existing_files_.count(fp) > 0;
}

void FakeDelegate::CreateFile(const base::FilePath& fp) {
  existing_files_.emplace(fp);
}

base::Optional<gid_t> FakeDelegate::FindGroupId(const char* group) {
  auto k = groups_.find(group);
  if (k == groups_.end())
    return base::nullopt;
  return k->second;
}

int FakeDelegate::GetPermissions(const base::FilePath& path) {
  auto k = permissions_.find(path.value());
  if (k == permissions_.end())
    return 0;
  return k->second;
}

bool FakeDelegate::SetPermissions(const base::FilePath& path, int mode) {
  permissions_[path.value()] = mode;
  return true;
}

bool FakeDelegate::GetOwnership(const base::FilePath& path,
                                uid_t* user,
                                gid_t* group) {
  auto k = ownerships_.find(path.value());
  if (k == ownerships_.end())
    return false;
  if (user)
    *user = k->second.first;
  if (group)
    *group = k->second.second;
  return true;
}

bool FakeDelegate::SetOwnership(const base::FilePath& path,
                                uid_t user,
                                gid_t group) {
  ownerships_[path.value()] = {user, group};
  return true;
}

}  // namespace fakes
}  // namespace mems_setup
