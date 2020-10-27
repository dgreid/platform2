// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_ENCRYPTED_CONTAINER_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_ENCRYPTED_CONTAINER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/blkdev_utils/device_mapper.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/backing_device.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace cryptohome {

// Type of encrypted containers.
enum class EncryptedContainerType {
  kUnknown = 0,
  kFscrypt,
  kEcryptfs,
  kDmcrypt,
};

struct DmcryptConfig {
  BackingDeviceConfig backing_device_config;
  std::string dmcrypt_device_name;
  std::string dmcrypt_cipher;
  std::vector<std::string> mkfs_opts;
  std::vector<std::string> tune2fs_opts;
};

struct EncryptedContainerConfig {
  EncryptedContainerType type;
  base::FilePath backing_dir;
  DmcryptConfig dmcrypt_config;
};

// An encrypted container is an abstract class that represents an encrypted
// backing storage medium. Since encrypted containers can be used in both
// daemons and one-shot calls, the implementation of each encrypted container
// leans towards keeping the container as stateless as possible.
class EncryptedContainer {
 public:
  virtual ~EncryptedContainer() {}

  // Removes the encrypted container's backing storage.
  virtual bool Purge() = 0;
  // Sets up the encrypted container, including creating the container if
  // needed.
  virtual bool Setup(const FileSystemKey& encryption_key, bool create) = 0;
  // Tears down the container, removing the encryption key if it was added.
  virtual bool Teardown() = 0;
  // Gets the type of the encrypted container.
  virtual EncryptedContainerType GetType() = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_ENCRYPTED_CONTAINER_H_
