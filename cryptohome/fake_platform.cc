// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Platform

#include "cryptohome/fake_platform.h"

#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>

namespace cryptohome {

namespace {

class ProxyFileEnumerator : public FileEnumerator {
 public:
  ProxyFileEnumerator(const base::FilePath& tmpfs_rootfs,
                      FileEnumerator* real_enumerator)
      : tmpfs_rootfs_(tmpfs_rootfs), real_enumerator_(real_enumerator) {}

  // Removed tmpfs prefix from the returned path.
  base::FilePath Next() override {
    base::FilePath next = real_enumerator_->Next();
    if (!tmpfs_rootfs_.IsParent(next)) {
      return next;
    }
    base::FilePath assumed_path("/");
    CHECK(tmpfs_rootfs_.AppendRelativePath(next, &assumed_path));
    return assumed_path;
  }

  FileEnumerator::FileInfo GetInfo() override {
    return real_enumerator_->GetInfo();
  }

 private:
  base::FilePath tmpfs_rootfs_;
  std::unique_ptr<FileEnumerator> real_enumerator_;
};

}  // namespace

// Constructor/destructor

FakePlatform::FakePlatform() : Platform() {
  base::GetTempDir(&tmpfs_rootfs_);
  tmpfs_rootfs_ = tmpfs_rootfs_.Append(real_platform_.GetRandomSuffix());
  if (!real_platform_.CreateDirectory(tmpfs_rootfs_)) {
    LOG(ERROR) << "Failed to create test dir: " << tmpfs_rootfs_;
  }
}

FakePlatform::~FakePlatform() {
  real_platform_.DeletePathRecursively(tmpfs_rootfs_);
}

// Helpers

base::FilePath FakePlatform::TestFilePath(const base::FilePath& path) const {
  std::string path_str = path.NormalizePathSeparators().value();
  // Make the path relative.
  CHECK(path.IsAbsolute());
  if (path_str.length() > 0 && path_str[0] == '/') {
    path_str = path_str.substr(1);
  }
  return tmpfs_rootfs_.Append(path_str);
}

// Platform API

bool FakePlatform::Rename(const base::FilePath& from,
                          const base::FilePath& to) {
  return real_platform_.Rename(TestFilePath(from), TestFilePath(to));
}

bool FakePlatform::Move(const base::FilePath& from, const base::FilePath& to) {
  return real_platform_.Move(TestFilePath(from), TestFilePath(to));
}

bool FakePlatform::Copy(const base::FilePath& from, const base::FilePath& to) {
  return real_platform_.Copy(TestFilePath(from), TestFilePath(to));
}

bool FakePlatform::EnumerateDirectoryEntries(
    const base::FilePath& path,
    bool recursive,
    std::vector<base::FilePath>* ent_list) {
  return real_platform_.EnumerateDirectoryEntries(TestFilePath(path), recursive,
                                                  ent_list);
}

bool FakePlatform::DeleteFile(const base::FilePath& path) {
  return real_platform_.DeleteFile(TestFilePath(path));
}

bool FakePlatform::DeletePathRecursively(const base::FilePath& path) {
  return real_platform_.DeletePathRecursively(TestFilePath(path));
}

bool FakePlatform::DeleteFileDurable(const base::FilePath& path) {
  return real_platform_.DeleteFileDurable(TestFilePath(path));
}

bool FakePlatform::FileExists(const base::FilePath& path) {
  return real_platform_.FileExists(TestFilePath(path));
}

bool FakePlatform::DirectoryExists(const base::FilePath& path) {
  return real_platform_.DirectoryExists(TestFilePath(path));
}

bool FakePlatform::CreateDirectory(const base::FilePath& path) {
  return real_platform_.CreateDirectory(TestFilePath(path));
}

bool FakePlatform::ReadFile(const base::FilePath& path, brillo::Blob* blob) {
  return real_platform_.ReadFile(TestFilePath(path), blob);
}

bool FakePlatform::ReadFileToString(const base::FilePath& path,
                                    std::string* str) {
  return real_platform_.ReadFileToString(TestFilePath(path), str);
}

bool FakePlatform::ReadFileToSecureBlob(const base::FilePath& path,
                                        brillo::SecureBlob* sblob) {
  return real_platform_.ReadFileToSecureBlob(TestFilePath(path), sblob);
}

bool FakePlatform::WriteFile(const base::FilePath& path,
                             const brillo::Blob& blob) {
  return real_platform_.WriteFile(TestFilePath(path), blob);
}

bool FakePlatform::WriteSecureBlobToFile(const base::FilePath& path,
                                         const brillo::SecureBlob& sblob) {
  return real_platform_.WriteSecureBlobToFile(TestFilePath(path), sblob);
}

bool FakePlatform::WriteFileAtomic(const base::FilePath& path,
                                   const brillo::Blob& blob,
                                   mode_t mode) {
  return real_platform_.WriteFileAtomic(TestFilePath(path), blob, mode);
}

bool FakePlatform::WriteSecureBlobToFileAtomic(const base::FilePath& path,
                                               const brillo::SecureBlob& sblob,
                                               mode_t mode) {
  return real_platform_.WriteSecureBlobToFileAtomic(TestFilePath(path), sblob,
                                                    mode);
}

bool FakePlatform::WriteFileAtomicDurable(const base::FilePath& path,
                                          const brillo::Blob& blob,
                                          mode_t mode) {
  return real_platform_.WriteFileAtomicDurable(TestFilePath(path), blob, mode);
}

bool FakePlatform::WriteSecureBlobToFileAtomicDurable(
    const base::FilePath& path, const brillo::SecureBlob& sblob, mode_t mode) {
  return real_platform_.WriteSecureBlobToFileAtomicDurable(TestFilePath(path),
                                                           sblob, mode);
}

bool FakePlatform::WriteStringToFile(const base::FilePath& path,
                                     const std::string& str) {
  return real_platform_.WriteStringToFile(TestFilePath(path), str);
}

bool FakePlatform::WriteStringToFileAtomicDurable(const base::FilePath& path,
                                                  const std::string& str,
                                                  mode_t mode) {
  return real_platform_.WriteStringToFileAtomicDurable(TestFilePath(path), str,
                                                       mode);
}

bool FakePlatform::WriteArrayToFile(const base::FilePath& path,
                                    const char* data,
                                    size_t size) {
  return real_platform_.WriteArrayToFile(TestFilePath(path), data, size);
}

FILE* FakePlatform::OpenFile(const base::FilePath& path, const char* mode) {
  return real_platform_.OpenFile(TestFilePath(path), mode);
}

bool FakePlatform::CloseFile(FILE* file) {
  return real_platform_.CloseFile(file);
}

FileEnumerator* FakePlatform::GetFileEnumerator(const base::FilePath& path,
                                                bool recursive,
                                                int file_type) {
  return new ProxyFileEnumerator(
      tmpfs_rootfs_, real_platform_.GetFileEnumerator(TestFilePath(path),
                                                      recursive, file_type));
}

bool FakePlatform::GetFileSize(const base::FilePath& path, int64_t* size) {
  return real_platform_.GetFileSize(TestFilePath(path), size);
}

bool FakePlatform::HasExtendedFileAttribute(const base::FilePath& path,
                                            const std::string& name) {
  return real_platform_.HasExtendedFileAttribute(TestFilePath(path), name);
}

bool FakePlatform::ListExtendedFileAttributes(
    const base::FilePath& path, std::vector<std::string>* attr_list) {
  return real_platform_.ListExtendedFileAttributes(TestFilePath(path),
                                                   attr_list);
}

bool FakePlatform::GetExtendedFileAttributeAsString(const base::FilePath& path,
                                                    const std::string& name,
                                                    std::string* value) {
  return real_platform_.GetExtendedFileAttributeAsString(TestFilePath(path),
                                                         name, value);
}

bool FakePlatform::GetExtendedFileAttribute(const base::FilePath& path,
                                            const std::string& name,
                                            char* value,
                                            ssize_t size) {
  return real_platform_.GetExtendedFileAttribute(TestFilePath(path), name,
                                                 value, size);
}

bool FakePlatform::SetExtendedFileAttribute(const base::FilePath& path,
                                            const std::string& name,
                                            const char* value,
                                            size_t size) {
  return real_platform_.SetExtendedFileAttribute(TestFilePath(path), name,
                                                 value, size);
}

bool FakePlatform::RemoveExtendedFileAttribute(const base::FilePath& path,
                                               const std::string& name) {
  return real_platform_.RemoveExtendedFileAttribute(TestFilePath(path), name);
}

bool FakePlatform::GetOwnership(const base::FilePath& path,
                                uid_t* user_id,
                                gid_t* group_id,
                                bool follow_links) const {
  // TODO(chromium:1141301, dlunev): here and further check for file existence.
  // Can not do it at present due to weird test dependencies.
  if (file_owners_.find(path) == file_owners_.end()) {
    *user_id = fake_platform::kChronosUID;
    *group_id = fake_platform::kChronosGID;
    return true;
  }

  *user_id = file_owners_.at(path).first;
  *group_id = file_owners_.at(path).second;
  return true;
}

bool FakePlatform::SetOwnership(const base::FilePath& path,
                                uid_t user_id,
                                gid_t group_id,
                                bool follow_links) const {
  file_owners_[path] = {user_id, group_id};
  return true;
}

bool FakePlatform::GetPermissions(const base::FilePath& path,
                                  mode_t* mode) const {
  if (file_mode_.find(path) == file_mode_.end()) {
    *mode = S_IRWXU | S_IRGRP | S_IXGRP;
    return true;
  }
  *mode = file_mode_.at(path);
  return true;
}

bool FakePlatform::SetPermissions(const base::FilePath& path,
                                  mode_t mode) const {
  file_mode_[path] = mode;
  return true;
}

bool FakePlatform::FakePlatform::GetUserId(const std::string& user,
                                           uid_t* user_id,
                                           gid_t* group_id) const {
  CHECK(user_id);
  CHECK(group_id);

  if (uids_.find(user) == uids_.end() || gids_.find(user) == gids_.end()) {
    LOG(ERROR) << "No user: " << user;
    return false;
  }

  *user_id = uids_.at(user);
  *group_id = gids_.at(user);
  return true;
}

bool FakePlatform::GetGroupId(const std::string& group, gid_t* group_id) const {
  CHECK(group_id);

  if (gids_.find(group) == gids_.end()) {
    LOG(ERROR) << "No group: " << group;
    return false;
  }

  *group_id = gids_.at(group);
  return true;
}

// Test API

void FakePlatform::SetUserId(const std::string& user, uid_t user_id) {
  CHECK(uids_.find(user) == uids_.end());

  uids_[user] = user_id;
}

void FakePlatform::SetGroupId(const std::string& group, gid_t group_id) {
  CHECK(gids_.find(group) == gids_.end());

  gids_[group] = group_id;
}

void FakePlatform::SetStandardUsersAndGroups() {
  SetUserId(fake_platform::kRoot, fake_platform::kRootUID);
  SetGroupId(fake_platform::kRoot, fake_platform::kRootGID);
  SetUserId(fake_platform::kChapsUser, fake_platform::kChapsUID);
  SetGroupId(fake_platform::kChapsUser, fake_platform::kChapsGID);
  SetUserId(fake_platform::kChronosUser, fake_platform::kChronosUID);
  SetGroupId(fake_platform::kChronosUser, fake_platform::kChronosGID);
  SetGroupId(fake_platform::kSharedGroup, fake_platform::kSharedGID);
}

void FakePlatform::SetSystemSaltForLibbrillo(const brillo::SecureBlob& salt) {
  std::string* brillo_salt = new std::string();
  brillo_salt->resize(salt.size());
  brillo_salt->assign(reinterpret_cast<const char*>(salt.data()), salt.size());
  brillo::cryptohome::home::SetSystemSalt(brillo_salt);
}

void FakePlatform::RemoveSystemSaltForLibbrillo() {
  std::string* salt = brillo::cryptohome::home::GetSystemSalt();
  brillo::cryptohome::home::SetSystemSalt(NULL);
  delete salt;
}

}  // namespace cryptohome
