// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/dmcrypt_container.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/backing_device.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace cryptohome {

namespace {

constexpr uint64_t kSectorSize = 512;
constexpr uint64_t kExt4BlockSize = 4096;

}  // namespace

DmcryptContainer::DmcryptContainer(
    const DmcryptConfig& config,
    std::unique_ptr<BackingDevice> backing_device,
    const FileSystemKeyReference& key_reference,
    Platform* platform,
    std::unique_ptr<brillo::DeviceMapper> device_mapper)
    : dmcrypt_device_name_(config.dmcrypt_device_name),
      dmcrypt_cipher_(config.dmcrypt_cipher),
      mkfs_opts_(config.mkfs_opts),
      tune2fs_opts_(config.tune2fs_opts),
      backing_device_(std::move(backing_device)),
      key_reference_(key_reference),
      platform_(platform),
      device_mapper_(std::move(device_mapper)) {}

DmcryptContainer::DmcryptContainer(
    const DmcryptConfig& config,
    std::unique_ptr<BackingDevice> backing_device,
    const FileSystemKeyReference& key_reference,
    Platform* platform)
    : DmcryptContainer(config,
                       std::move(backing_device),
                       key_reference,
                       platform,
                       std::make_unique<brillo::DeviceMapper>()) {}

bool DmcryptContainer::Purge() {
  return backing_device_->Purge();
}

bool DmcryptContainer::Setup(const FileSystemKey& encryption_key, bool create) {
  if (create && !backing_device_->Create()) {
    LOG(ERROR) << "Failed to create backing device";
    return false;
  }

  if (!backing_device_->Setup()) {
    LOG(ERROR) << "Failed to setup backing device";
    return false;
  }

  base::Optional<base::FilePath> backing_device_path =
      backing_device_->GetPath();
  if (!backing_device_path) {
    LOG(ERROR) << "Failed to get backing device path";
    backing_device_->Teardown();
    return false;
  }

  uint64_t blkdev_size;
  if (!platform_->GetBlkSize(*backing_device_path, &blkdev_size) ||
      blkdev_size < kExt4BlockSize) {
    PLOG(ERROR) << "Failed to get block device size";
    backing_device_->Teardown();
    return false;
  }

  base::FilePath dmcrypt_device_path =
      base::FilePath("/dev/mapper").Append(dmcrypt_device_name_);
  uint64_t sectors = blkdev_size / kSectorSize;
  brillo::SecureBlob dm_parameters =
      brillo::DevmapperTable::CryptCreateParameters(
          // cipher.
          dmcrypt_cipher_,
          // encryption key.
          encryption_key.fek,
          // iv offset.
          0,
          // device path.
          *backing_device_path,
          // device offset.
          0,
          // allow discards.
          true);
  brillo::DevmapperTable dm_table(0, sectors, "crypt", dm_parameters);
  if (!device_mapper_->Setup(dmcrypt_device_name_, dm_table)) {
    backing_device_->Teardown();
    LOG(ERROR) << "dm_setup failed";
    return false;
  }

  // Ensure that the dm-crypt device or the underlying backing device are
  // not left attached on the failure paths.
  base::ScopedClosureRunner device_teardown_runner(base::BindOnce(
      base::IgnoreResult(&DmcryptContainer::Teardown), base::Unretained(this)));

  // Wait for the dmcrypt device path to show up before continuing to setting
  // up the filesystem.
  if (!platform_->UdevAdmSettle(dmcrypt_device_path, true)) {
    LOG(ERROR) << "udevadm settle failed.";
    return false;
  }

  // Create filesystem.
  if (create && !platform_->FormatExt4(dmcrypt_device_path, mkfs_opts_, 0)) {
    PLOG(ERROR) << "Failed to format ext4 filesystem";
    return false;
  }

  // Modify features depending on whether we already have the following enabled.
  if (!tune2fs_opts_.empty() &&
      !platform_->Tune2Fs(dmcrypt_device_path, tune2fs_opts_)) {
    PLOG(ERROR) << "Failed to tune ext4 filesystem";
    return false;
  }

  ignore_result(device_teardown_runner.Release());
  return true;
}

bool DmcryptContainer::Teardown() {
  if (!device_mapper_->Remove(dmcrypt_device_name_)) {
    LOG(ERROR) << "Failed to teardown device mapper device.";
    return false;
  }

  if (!backing_device_->Teardown()) {
    LOG(ERROR) << "Failed to teardown backing device";
    return false;
  }

  return true;
}

}  // namespace cryptohome
