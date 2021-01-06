// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/loopback_device.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/values.h>
#include <brillo/blkdev_utils/loop_device.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/backing_device.h"

namespace cryptohome {

LoopbackDevice::LoopbackDevice(
    const BackingDeviceConfig& config,
    Platform* platform,
    std::unique_ptr<brillo::LoopDeviceManager> loop_device_manager)
    : name_(config.name),
      size_(config.size),
      backing_file_path_(config.loopback.backing_file_path),
      platform_(platform),
      loop_device_manager_(std::move(loop_device_manager)) {}

LoopbackDevice::LoopbackDevice(const BackingDeviceConfig& config,
                               Platform* platform)
    : LoopbackDevice(
          config, platform, std::make_unique<brillo::LoopDeviceManager>()) {}

bool LoopbackDevice::Create() {
  if (!platform_->CreateSparseFile(backing_file_path_, size_) ||
      !platform_->SetPermissions(backing_file_path_, S_IRUSR | S_IWUSR)) {
    LOG(ERROR) << "Failed to create sparse file.";
    return false;
  }
  return true;
}

bool LoopbackDevice::Purge() {
  return platform_->DeleteFile(backing_file_path_);
}

bool LoopbackDevice::Setup() {
  // Set up loopback device.
  std::unique_ptr<brillo::LoopDevice> loopdev =
      loop_device_manager_->AttachDeviceToFile(backing_file_path_);

  if (!loopdev->IsValid()) {
    LOG(ERROR) << "Failed to attach loop back device";
    return false;
  }

  // Set loop device name.
  if (!loopdev->SetName(name_)) {
    LOG(ERROR) << "Loop set name failed";
    loopdev->Detach();
    return false;
  }

  return true;
}

bool LoopbackDevice::Teardown() {
  std::unique_ptr<brillo::LoopDevice> loopdev =
      loop_device_manager_->GetAttachedDeviceByName(name_);

  if (!loopdev->IsValid()) {
    LOG(ERROR) << "Loop device does not exist.";
    return false;
  }

  return loopdev->Detach();
}

bool LoopbackDevice::Exists() {
  return platform_->FileExists(backing_file_path_);
}

base::Optional<base::FilePath> LoopbackDevice::GetPath() {
  std::unique_ptr<brillo::LoopDevice> loopdev =
      loop_device_manager_->GetAttachedDeviceByName(name_);

  if (!loopdev->IsValid()) {
    LOG(ERROR) << "Loop device does not exist.";
    return base::nullopt;
  }

  return loopdev->GetDevicePath();
}

}  // namespace cryptohome
