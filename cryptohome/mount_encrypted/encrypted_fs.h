// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOUNT_ENCRYPTED_ENCRYPTED_FS_H_
#define CRYPTOHOME_MOUNT_ENCRYPTED_ENCRYPTED_FS_H_

#include <glib.h>
#include <inttypes.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/blkdev_utils/device_mapper.h>
#include <brillo/blkdev_utils/loop_device.h>
#include <brillo/secure_blob.h>

#include <cryptohome/mount_encrypted/mount_encrypted.h>
#include <cryptohome/platform.h>

#define STATEFUL_MNT "mnt/stateful_partition"
#define ENCRYPTED_MNT STATEFUL_MNT "/encrypted"

namespace mount_encrypted {

// Teardown stage: for granular teardowns
enum class TeardownStage {
  kTeardownUnbind,
  kTeardownDevmapper,
  kTeardownLoopDevice,
};

// BindMount represents a bind mount to be setup from
// source directories within the encrypted mount.
// EncryptedFs is responsible for setting up the bind mount
// once it sets up the encrypted mount.
struct BindMount {
  base::FilePath src;  // Location of bind source.
  base::FilePath dst;  // Destination of bind.
  std::string owner;
  std::string group;
  mode_t mode;
  bool submount;  // Submount is bound already.
};

// EncryptedFs sets up, tears down and cleans up encrypted
// stateful mounts. Given a root directory, the class
// sets up an encrypted mount at <root_dir>/ENCRYPTED_MOUNT.
class EncryptedFs {
 public:
  // Setup EncryptedFs with the root dir, platform and loopdev manager.
  EncryptedFs(const base::FilePath& rootdir,
              cryptohome::Platform* platform,
              brillo::LoopDeviceManager* loop_device_manager,
              brillo::DeviceMapper* device_mapper);
  ~EncryptedFs() = default;

  // Setup mounts the encrypted mount by:
  // 1. Create a sparse file at <rootdir>/STATEFUL_MNT/encrypted.block
  // 2. Mounting a loop device on top of the sparse file.
  // 3. Mounting a dmcrypt device with the loop device as the backing
  //    device and the provided encryption key.
  // 4. Formatting the dmcrypt device as ext4 and mounting it at the
  //    mount_point.
  // If a sparse file already exists, Setup assumes that the stateful
  // mount has already been setup and attempts to mount the
  // | ext4 | dmcrypt | loopback | tower on top of the sparse file.
  // Parameters
  //   encryption_key - dmcrypt encryption key.
  //   rebuild - cleanup and recreate the encrypted mount.
  result_code Setup(const brillo::SecureBlob& encryption_key, bool rebuild);
  // Purge - obliterate the sparse file. This should be called only
  // when the encrypted fs is not mounted.
  bool Purge(void);
  // Teardown - stepwise unmounts the | ext4 | dmcrypt | loopback | tower
  // on top of the sparse file.
  result_code Teardown(void);
  // CheckStates - Checks validity for the stateful mount before mounting.
  result_code CheckStates(void);
  // ReportInfo - Reports the paths and bind mounts.
  result_code ReportInfo(void) const;
  // GetKey - Returns the key for the dmcrypt device. This is used
  // for finalization in devices that do not have the TPM available
  // initially while setting up the encrypted mount.
  brillo::SecureBlob GetKey() const;

 private:
  // Use a raw Platform pointer to avoid convoluted EXPECT_CALL semantics
  // for mock Platform objects.
  cryptohome::Platform* platform_;
  // Loop Device Manager.
  brillo::LoopDeviceManager* loopdev_manager_;
  // Device Mapper.
  brillo::DeviceMapper* device_mapper_;

  friend class EncryptedFsTest;
  FRIEND_TEST(EncryptedFsTest, RebuildStateful);
  FRIEND_TEST(EncryptedFsTest, OldStateful);
  FRIEND_TEST(EncryptedFsTest, LoopdevTeardown);
  FRIEND_TEST(EncryptedFsTest, DevmapperTeardown);

  // CreateSparseBackingFile creates the sparse backing file for the
  // encrypted mount and returns an open fd, if successful.
  bool CreateSparseBackingFile(int64_t file_size);
  // TeardownByStage allows higher granularity over teardown
  // processes.
  result_code TeardownByStage(TeardownStage stage, bool ignore_errors);

  // FilePaths used by the encrypted fs.
  base::FilePath rootdir_;
  base::FilePath stateful_mount_;
  base::FilePath block_path_;
  base::FilePath encrypted_mount_;
  std::string dmcrypt_name_;
  base::FilePath dmcrypt_dev_;
  std::vector<BindMount> bind_mounts_;
};

}  // namespace mount_encrypted

#endif  // CRYPTOHOME_MOUNT_ENCRYPTED_ENCRYPTED_FS_H_
