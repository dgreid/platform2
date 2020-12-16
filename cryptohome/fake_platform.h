// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FAKE_PLATFORM_H_
#define CRYPTOHOME_FAKE_PLATFORM_H_

#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/secure_blob.h>

#include "cryptohome/platform.h"

namespace cryptohome {

namespace fake_platform {
// Common constants
constexpr char kRoot[] = "root";
constexpr char kChapsUser[] = "chaps";
constexpr char kChronosUser[] = "chronos";
constexpr char kSharedGroup[] = "chronos-access";

constexpr uid_t kRootUID = 0;
constexpr gid_t kRootGID = 0;
constexpr uid_t kChapsUID = 42;
constexpr gid_t kChapsGID = 43;
constexpr uid_t kChronosUID = 44;
constexpr gid_t kChronosGID = 45;
constexpr gid_t kSharedGID = 46;
}  // namespace fake_platform

class FakePlatform final : public Platform {
 public:
  FakePlatform();
  ~FakePlatform() override;

  // Prohibit copy/move/assignment.
  FakePlatform(const FakePlatform&) = delete;
  FakePlatform(const FakePlatform&&) = delete;
  FakePlatform& operator=(const FakePlatform&) = delete;
  FakePlatform& operator=(const FakePlatform&&) = delete;

  // Platform API

  bool GetUserId(const std::string& user,
                 uid_t* user_id,
                 gid_t* group_id) const override;
  bool GetGroupId(const std::string& group, gid_t* group_id) const override;

  FileEnumerator* GetFileEnumerator(const base::FilePath& path,
                                    bool recursive,
                                    int file_type) override;
  bool EnumerateDirectoryEntries(
      const base::FilePath& path,
      bool recursive,
      std::vector<base::FilePath>* ent_list) override;

  bool Rename(const base::FilePath& from, const base::FilePath& to) override;
  bool Move(const base::FilePath& from, const base::FilePath& to) override;
  bool Copy(const base::FilePath& from, const base::FilePath& to) override;
  bool DeleteFile(const base::FilePath& path) override;
  bool DeletePathRecursively(const base::FilePath& path) override;
  bool DeleteFileDurable(const base::FilePath& path) override;
  bool FileExists(const base::FilePath& path) override;
  bool DirectoryExists(const base::FilePath& path) override;
  bool CreateDirectory(const base::FilePath& path) override;
  bool CreateSparseFile(const base::FilePath& path, int64_t size) override;

  bool ReadFile(const base::FilePath& path, brillo::Blob* blob) override;
  bool ReadFileToString(const base::FilePath& path, std::string* str) override;
  bool ReadFileToSecureBlob(const base::FilePath& path,
                            brillo::SecureBlob* sblob) override;

  bool WriteFile(const base::FilePath& path, const brillo::Blob& blob) override;
  bool WriteSecureBlobToFile(const base::FilePath& path,
                             const brillo::SecureBlob& sblob) override;
  bool WriteFileAtomic(const base::FilePath& path,
                       const brillo::Blob& blob,
                       mode_t mode) override;
  bool WriteSecureBlobToFileAtomic(const base::FilePath& path,
                                   const brillo::SecureBlob& sblob,
                                   mode_t mode) override;
  bool WriteFileAtomicDurable(const base::FilePath& path,
                              const brillo::Blob& blob,
                              mode_t mode) override;
  bool WriteSecureBlobToFileAtomicDurable(const base::FilePath& path,
                                          const brillo::SecureBlob& sblob,
                                          mode_t mode) override;
  bool WriteStringToFile(const base::FilePath& path,
                         const std::string& str) override;
  bool WriteStringToFileAtomicDurable(const base::FilePath& path,
                                      const std::string& str,
                                      mode_t mode) override;
  bool WriteArrayToFile(const base::FilePath& path,
                        const char* data,
                        size_t size) override;

  FILE* OpenFile(const base::FilePath& path, const char* mode) override;
  bool CloseFile(FILE* file) override;

  bool GetFileSize(const base::FilePath& path, int64_t* size) override;

  bool HasExtendedFileAttribute(const base::FilePath& path,
                                const std::string& name) override;
  bool ListExtendedFileAttributes(const base::FilePath& path,
                                  std::vector<std::string>* attr_list) override;
  bool GetExtendedFileAttributeAsString(const base::FilePath& path,
                                        const std::string& name,
                                        std::string* value) override;
  bool GetExtendedFileAttribute(const base::FilePath& path,
                                const std::string& name,
                                char* value,
                                ssize_t size) override;
  bool SetExtendedFileAttribute(const base::FilePath& path,
                                const std::string& name,
                                const char* value,
                                size_t size) override;
  bool RemoveExtendedFileAttribute(const base::FilePath& path,
                                   const std::string& name) override;

  // TODO(chromium:1141301, dlunev): consider running under root to make the
  // following operate on FS, not on on fake state.
  bool GetOwnership(const base::FilePath& path,
                    uid_t* user_id,
                    gid_t* group_id,
                    bool follow_links) const override;
  bool SetOwnership(const base::FilePath& path,
                    uid_t user_id,
                    gid_t group_id,
                    bool follow_links) const override;
  bool GetPermissions(const base::FilePath& path, mode_t* mode) const override;
  bool SetPermissions(const base::FilePath& path, mode_t mode) const override;

  // Test API

  void SetStandardUsersAndGroups();

  // TODO(chromium:1141301, dlunev): this is a workaround of the fact that
  // libbrillo reads and caches system salt on it own and we are unable to
  // inject the tmpfs path to it.
  void SetSystemSaltForLibbrillo(const brillo::SecureBlob& salt);
  void RemoveSystemSaltForLibbrillo();

 private:
  std::unordered_map<std::string, uid_t> uids_;
  std::unordered_map<std::string, gid_t> gids_;
  // owners and perms are mutable due to const interface we need to abide.
  mutable std::unordered_map<base::FilePath, std::pair<uid_t, gid_t>>
      file_owners_;
  mutable std::unordered_map<base::FilePath, mode_t> file_mode_;
  base::FilePath tmpfs_rootfs_;

  void SetUserId(const std::string& user, uid_t user_id);
  void SetGroupId(const std::string& group, gid_t group_id);

  base::FilePath TestFilePath(const base::FilePath& path) const;

  Platform real_platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FAKE_PLATFORM_H_
