// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/blkdev_utils/lvm_device.h"

// lvm2 has multiple options for managing LVM objects:
// - liblvm2app: deprecated.
// - liblvm2cmd: simple interface to directly parse cli commands into functions.
// - lvmdbusd: persistent daemon that can be reached via D-Bus.
//
// Since the logical/physical volume and volume group creation can occur during
// early boot when dbus is not available, the preferred solution is to use
// lvm2cmd.
#include <lvm2cmd.h>

#include <base/posix/eintr_wrapper.h>
#include <brillo/process/process.h>

namespace brillo {
namespace {

void LogLvmError(int rc, const std::string& cmd) {
  switch (rc) {
    case LVM2_COMMAND_SUCCEEDED:
      break;
    case LVM2_NO_SUCH_COMMAND:
      LOG(ERROR) << "Failed to run lvm2 command: no such command " << cmd;
      break;
    case LVM2_INVALID_PARAMETERS:
      LOG(ERROR) << "Failed to run lvm2 command: invalid parameters " << cmd;
      break;
    case LVM2_PROCESSING_FAILED:
      LOG(ERROR) << "Failed to run lvm2 command: processing failed " << cmd;
      break;
    default:
      LOG(ERROR) << "Failed to run lvm2 command: invalid return code " << cmd;
      break;
  }
}

}  // namespace

PhysicalVolume::PhysicalVolume(const base::FilePath& device_path,
                               std::shared_ptr<LvmCommandRunner> lvm)
    : device_path_(device_path), lvm_(lvm) {}

bool PhysicalVolume::Check() {
  if (device_path_.empty() || !lvm_)
    return false;

  return lvm_->RunCommand({"pvck", device_path_.value()});
}

bool PhysicalVolume::Repair() {
  if (device_path_.empty() || !lvm_)
    return false;

  return lvm_->RunCommand({"pvck", "--yes", device_path_.value()});
}

bool PhysicalVolume::Remove() {
  if (device_path_.empty() || !lvm_)
    return false;

  bool ret = lvm_->RunCommand({"pvremove", device_path_.value()});
  device_path_ = base::FilePath();
  return ret;
}

VolumeGroup::VolumeGroup(const std::string& volume_group_name,
                         std::shared_ptr<LvmCommandRunner> lvm)
    : volume_group_name_(volume_group_name), lvm_(lvm) {}

bool VolumeGroup::Check() {
  if (volume_group_name_.empty() || !lvm_)
    return false;

  return lvm_->RunCommand({"vgck", GetPath().value()});
}

bool VolumeGroup::Repair() {
  if (volume_group_name_.empty() || !lvm_)
    return false;
  return lvm_->RunCommand({"vgck", "--yes", GetPath().value()});
}

base::FilePath VolumeGroup::GetPath() const {
  if (volume_group_name_.empty() || !lvm_)
    return base::FilePath();
  return base::FilePath("/dev").Append(volume_group_name_);
}

bool VolumeGroup::Activate() {
  if (volume_group_name_.empty() || !lvm_)
    return false;
  return lvm_->RunCommand({"vgchange", "-ay", volume_group_name_});
}

bool VolumeGroup::Deactivate() {
  if (volume_group_name_.empty() || !lvm_)
    return false;
  return lvm_->RunCommand({"vgchange", "-an", volume_group_name_});
}

bool VolumeGroup::Remove() {
  if (volume_group_name_.empty() || !lvm_)
    return false;
  bool ret = lvm_->RunCommand({"vgremove", volume_group_name_});
  volume_group_name_ = "";
  return ret;
}

LogicalVolume::LogicalVolume(const std::string& logical_volume_name,
                             const std::string& volume_group_name,
                             std::shared_ptr<LvmCommandRunner> lvm)
    : logical_volume_name_(logical_volume_name),
      volume_group_name_(volume_group_name),
      lvm_(lvm) {}

base::FilePath LogicalVolume::GetPath() {
  if (logical_volume_name_.empty() || !lvm_)
    return base::FilePath();
  return base::FilePath("/dev")
      .Append(volume_group_name_)
      .Append(logical_volume_name_);
}

bool LogicalVolume::Activate() {
  if (logical_volume_name_.empty() || !lvm_)
    return false;
  return lvm_->RunCommand({"lvchange", "-ay", GetName()});
}

bool LogicalVolume::Deactivate() {
  if (logical_volume_name_.empty() || !lvm_)
    return false;
  return lvm_->RunCommand({"lvchange", "-an", GetName()});
}

bool LogicalVolume::Remove() {
  if (volume_group_name_.empty() || !lvm_)
    return false;
  bool ret = lvm_->RunCommand({"lvremove", GetName()});
  logical_volume_name_ = "";
  volume_group_name_ = "";
  return ret;
}

Thinpool::Thinpool(const std::string& thinpool_name,
                   const std::string& volume_group_name,
                   std::shared_ptr<LvmCommandRunner> lvm)
    : thinpool_name_(thinpool_name),
      volume_group_name_(volume_group_name),
      lvm_(lvm) {}

bool Thinpool::Check() {
  if (thinpool_name_.empty() || !lvm_)
    return false;

  return lvm_->RunProcess({"thin_check", GetName()});
}

bool Thinpool::Repair() {
  if (thinpool_name_.empty() || !lvm_)
    return false;
  return lvm_->RunProcess({"lvconvert", "--repair", GetName()});
}

bool Thinpool::Activate() {
  if (thinpool_name_.empty() || !lvm_)
    return false;
  return lvm_->RunCommand({"lvchange", "-ay", GetName()});
}

bool Thinpool::Deactivate() {
  if (thinpool_name_.empty() || !lvm_)
    return false;
  return lvm_->RunCommand({"lvchange", "-an", GetName()});
}

bool Thinpool::Remove() {
  if (thinpool_name_.empty() || !lvm_)
    return false;

  bool ret = lvm_->RunCommand({"lvremove", GetName()});
  volume_group_name_ = "";
  thinpool_name_ = "";
  return ret;
}

// For unittests, don't initialize the lvm2 handle.
LvmCommandRunner::LvmCommandRunner() : lvm_handle_(lvm2_init()) {}

LvmCommandRunner::~LvmCommandRunner() {
  lvm2_exit(lvm_handle_);
}

bool LvmCommandRunner::RunCommand(const std::vector<std::string>& cmd) {
  // lvm2_run() does not exec/fork a separate process, instead it parses the
  // command line and calls the relevant functions within liblvm2cmd directly.
  std::string lvm_cmd = base::JoinString(cmd, " ");
  int rc = lvm2_run(lvm_handle_, lvm_cmd.c_str());
  LogLvmError(rc, lvm_cmd);

  return rc == LVM2_COMMAND_SUCCEEDED;
}

bool LvmCommandRunner::RunProcess(const std::vector<std::string>& cmd,
                                  std::string* output) {
  brillo::ProcessImpl lvm_process;
  for (auto arg : cmd)
    lvm_process.AddArg(arg);
  lvm_process.SetCloseUnusedFileDescriptors(true);

  if (output) {
    lvm_process.RedirectUsingMemory(STDOUT_FILENO);
  }

  if (lvm_process.Run() != 0) {
    PLOG(ERROR) << "Failed to run command";
    return false;
  }

  if (output) {
    *output = lvm_process.GetOutputString(STDOUT_FILENO);
  }

  return true;
}

}  // namespace brillo
