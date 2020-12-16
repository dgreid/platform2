// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_ENCRYPTED_CONTAINER_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_ENCRYPTED_CONTAINER_H_

#include <memory>

#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace cryptohome {

// Type of encrypted containers.
enum class EncryptedContainerType {
  kUnknown = 0,
  kFscrypt,
  kEcryptfs,
};

// An encrypted container is an abstract class that represents an encrypted
// backing storage medium. Since encrypted containers can be used in both
// daemons and one-shot calls, the implementation of each encrypted container
// leans towards keeping the container as stateless as possible.
class EncryptedContainer {
 public:
  virtual ~EncryptedContainer() {}

  static std::unique_ptr<EncryptedContainer> Generate(
      EncryptedContainerType type,
      const base::FilePath& backing_dir,
      const FileSystemKeyReference& key_reference,
      Platform* platform);

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
