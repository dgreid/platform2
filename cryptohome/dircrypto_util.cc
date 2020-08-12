// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/dircrypto_util.h"

#include <string>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

extern "C" {
#include <ext2fs/ext2_fs.h>
#include <linux/fscrypt.h>
#include <keyutils.h>
}

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/secure_blob.h>

// Add missing chromeos specific partition wide drop cache.
#define FS_IOC_DROP_CACHE  _IO('f', 129)

namespace dircrypto {

namespace {

constexpr char kKeyType[] = "logon";
constexpr char kKeyNamePrefix[] = "ext4:";
constexpr char kKeyringName[] = "dircrypt";

key_serial_t GetSessionKeyring() {
  key_serial_t keyring =
      keyctl_search(KEY_SPEC_SESSION_KEYRING, "keyring", kKeyringName, 0);
  if (keyring == kInvalidKeySerial) {
    PLOG(ERROR) << "keyctl_search failed";
    return kInvalidKeySerial;
  }

  return keyring;
}

key_serial_t KeyReferenceToKeySerial(const brillo::SecureBlob& key_reference) {
  std::string key_name =
      kKeyNamePrefix + base::ToLowerASCII(base::HexEncode(
                           key_reference.data(), key_reference.size()));

  key_serial_t key =
      keyctl_search(GetSessionKeyring(), "logon", key_name.c_str(), 0);
  if (key == kInvalidKeySerial) {
    PLOG(ERROR) << "keyctl_search failed";
    return kInvalidKeySerial;
  }

  return key;
}

bool DropMountCaches(const base::FilePath& dir) {
  base::ScopedFD fd(HANDLE_EINTR(open(dir.value().c_str(),
                                      O_RDONLY | O_DIRECTORY)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Invalid directory: " << dir.value();
    return false;
  }

  if (ioctl(fd.get(), FS_IOC_DROP_CACHE, nullptr) < 0) {
    PLOG(ERROR) << "Failed: drop cache for mount point. Dir:" << dir.value();
    return false;
  }

  return true;
}

}  // namespace

bool SetDirectoryKey(const base::FilePath& dir,
                     const KeyReference& key_reference) {
  DCHECK_EQ(static_cast<size_t>(FS_KEY_DESCRIPTOR_SIZE),
            key_reference.reference.size());
  base::ScopedFD fd(HANDLE_EINTR(open(dir.value().c_str(),
                                      O_RDONLY | O_DIRECTORY)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Fscrypt: Invalid directory " << dir.value();
    return false;
  }
  struct fscrypt_policy policy = {};
  policy.version = 0;
  policy.contents_encryption_mode = FS_ENCRYPTION_MODE_AES_256_XTS;
  policy.filenames_encryption_mode = FS_ENCRYPTION_MODE_AES_256_CTS;
  policy.flags = 0;
  memcpy(policy.master_key_descriptor, key_reference.reference.data(),
         FS_KEY_DESCRIPTOR_SIZE);
  if (ioctl(fd.get(), FS_IOC_SET_ENCRYPTION_POLICY, &policy) < 0) {
    PLOG(ERROR) << "Failed to set the encryption policy of " << dir.value();
    return false;
  }
  return true;
}

KeyState GetDirectoryKeyState(const base::FilePath& dir) {
  base::ScopedFD fd(HANDLE_EINTR(open(dir.value().c_str(),
                                      O_RDONLY | O_DIRECTORY)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Fscrypt: Invalid directory " << dir.value();
    return KeyState::UNKNOWN;
  }
  struct fscrypt_policy policy = {};
  if (ioctl(fd.get(), FS_IOC_GET_ENCRYPTION_POLICY, &policy) < 0) {
    switch (errno) {
      case ENODATA:
      case ENOENT:
        return KeyState::NO_KEY;
      case ENOTTY:
      case EOPNOTSUPP:
        return KeyState::NOT_SUPPORTED;
      default:
        PLOG(ERROR) << "Failed to get the encryption policy of " << dir.value();
        return KeyState::UNKNOWN;
    }
  }
  return KeyState::ENCRYPTED;
}

bool AddKeyToKeyring(const brillo::SecureBlob& key,
                     KeyReference* key_reference) {
  if (key.size() > FS_MAX_KEY_SIZE ||
      key_reference->reference.size() != FS_KEY_DESCRIPTOR_SIZE) {
    LOG(ERROR) << "Invalid arguments: key.size() = " << key.size()
               << "key_descriptor.size() = " << key_reference->reference.size();
    return false;
  }
  key_serial_t keyring = GetSessionKeyring();
  if (keyring == kInvalidKeySerial) {
    PLOG(ERROR) << "keyctl_search failed";
    return false;
  }
  struct fscrypt_key fs_key = {};
  fs_key.mode = FS_ENCRYPTION_MODE_AES_256_XTS;
  memcpy(fs_key.raw, key.char_data(), key.size());
  fs_key.size = key.size();
  std::string key_name = kKeyNamePrefix + base::ToLowerASCII(base::HexEncode(
                                              key_reference->reference.data(),
                                              key_reference->reference.size()));
  key_serial_t key_serial = add_key(kKeyType, key_name.c_str(), &fs_key,
                                    sizeof(fscrypt_key), keyring);
  if (key_serial == kInvalidKeySerial) {
    PLOG(ERROR) << "Failed to insert key into keyring";
    return false;
  }

  /* Set the permission on the key.
   * Possessor: (everyone given the key is in a session keyring belonging to
   * init):
   * -- View, Search
   * User: (root)
   * -- View, Search, Write, Setattr
   * Group, Other:
   * -- None
   */
  const key_perm_t kPermissions = KEY_POS_VIEW | KEY_POS_SEARCH | KEY_USR_VIEW |
                                  KEY_USR_WRITE | KEY_USR_SEARCH |
                                  KEY_USR_SETATTR;
  if (keyctl_setperm(key_serial, kPermissions) != 0) {
    PLOG(ERROR) << "Could not change permission on key " << key_serial;
    return false;
  }
  return true;
}

bool UnlinkKey(const KeyReference& key_reference) {
  key_serial_t keyring = GetSessionKeyring(),
               key = KeyReferenceToKeySerial(key_reference.reference);

  if (key == kInvalidKeySerial || keyring == kInvalidKeySerial)
    return false;

  if (keyctl_unlink(key, keyring) == -1) {
    PLOG(ERROR) << "Failed to unlink the key";
    return false;
  }
  return true;
}

bool InvalidateSessionKey(const KeyReference& key_reference,
                          const base::FilePath& mount_path) {
  // First, attempt to selectively drop caches for mount point.
  // This can fail if the directory does not support the operation or if
  // the process does not have the correct capabilities (CAP_SYS_ADMIN).
  if (!DropMountCaches(mount_path)) {
    LOG(ERROR) << "Failed to drop cache for user mount.";
    // Use drop_caches to drop all clear cache. Otherwise, cached decrypted data
    // will stay visible. This should invalidate the key provided no one touches
    // the encrypted directories while this function is running.
    constexpr char kData = '3';
    if (base::WriteFile(base::FilePath("/proc/sys/vm/drop_caches"), &kData,
                        sizeof(kData)) != sizeof(kData)) {
      LOG(ERROR) << "Failed to drop all caches.";
      return false;
    }
  }

  // At this point, the key should be invalidated, but try to invalidate it just
  // in case.
  // If the key was already invaldated, this should fail with ENOKEY.
  key_serial_t keyring = GetSessionKeyring();
  key_serial_t key = KeyReferenceToKeySerial(key_reference.reference);

  if (key == kInvalidKeySerial || keyring == kInvalidKeySerial)
    return false;

  if (keyctl_invalidate(key) == 0) {
    LOG(ERROR) << "We ended up invalidating key " << key;
  } else if (errno != ENOKEY) {
    PLOG(ERROR) << "Failed to invalidate key" << key;
  }
  return true;
}

}  // namespace dircrypto
