// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBFS_SMB_FILESYSTEM_H_
#define SMBFS_SMB_FILESYSTEM_H_

#include <libsmbclient.h>
#include <sys/types.h>

#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/containers/mru_cache.h>
#include <base/macros.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <gtest/gtest_prod.h>

#include "smbfs/filesystem.h"
#include "smbfs/inode_map.h"
#include "smbfs/smb_credential.h"

namespace smbfs {

class SmbFsImpl;

class SmbFilesystem : public Filesystem {
 public:
  struct Options {
    Options();
    ~Options();

    // Allow moves.
    Options(Options&&);
    Options& operator=(Options&&);

    std::string share_path;
    uid_t uid = 0;
    gid_t gid = 0;
    std::unique_ptr<SmbCredential> credentials;
    bool allow_ntlm = false;
  };

  enum class ConnectError {
    kOk = 0,
    kNotFound,
    kAccessDenied,
    kSmb1Unsupported,
    kUnknownError,
  };

  explicit SmbFilesystem(Options options);
  ~SmbFilesystem() override;

  // Ensures that the SMB share can be connected to. Must NOT be called after
  // the filesystem is attached to a FUSE session.
  // Virtual for testing.
  virtual ConnectError EnsureConnected();

  // Store the implementation of the mojom::SmbFs Mojo interface.
  void SetSmbFsImpl(std::unique_ptr<SmbFsImpl> impl);

  // Sets the resolved IP address of the share host. |ip_address| is an IPv4
  // address in network byte order, or empty. If |ip_address| is empty, any
  // existing resolved address will be reset.
  // Virtual for testing.
  virtual void SetResolvedAddress(const std::vector<uint8_t>& ip_address);

  const std::string& resolved_share_path() const {
    return resolved_share_path_;
  }

  // Filesystem overrides.
  void StatFs(std::unique_ptr<StatFsRequest> request,
              fuse_ino_t inode) override;
  void Lookup(std::unique_ptr<EntryRequest> request,
              fuse_ino_t parent_inode,
              const std::string& name) override;
  void Forget(fuse_ino_t inode, uint64_t count) override;
  void GetAttr(std::unique_ptr<AttrRequest> request, fuse_ino_t inode) override;
  void SetAttr(std::unique_ptr<AttrRequest> request,
               fuse_ino_t inode,
               base::Optional<uint64_t> file_handle,
               const struct stat& attr,
               int to_set) override;
  void Open(std::unique_ptr<OpenRequest> request,
            fuse_ino_t inode,
            int flags) override;
  void Create(std::unique_ptr<CreateRequest> request,
              fuse_ino_t parent_inode,
              const std::string& name,
              mode_t mode,
              int flags) override;
  void Read(std::unique_ptr<BufRequest> request,
            fuse_ino_t inode,
            uint64_t file_handle,
            size_t size,
            off_t offset) override;
  void Write(std::unique_ptr<WriteRequest> request,
             fuse_ino_t inode,
             uint64_t file_handle,
             const char* buf,
             size_t size,
             off_t offset) override;
  void Release(std::unique_ptr<SimpleRequest> request,
               fuse_ino_t inode,
               uint64_t file_handle) override;
  void Rename(std::unique_ptr<SimpleRequest> request,
              fuse_ino_t old_parent_inode,
              const std::string& old_name,
              fuse_ino_t new_parent_inode,
              const std::string& new_name) override;
  void Unlink(std::unique_ptr<SimpleRequest> request,
              fuse_ino_t parent_inode,
              const std::string& name) override;
  void OpenDir(std::unique_ptr<OpenRequest> request,
               fuse_ino_t inode,
               int flags) override;
  void ReadDir(std::unique_ptr<DirentryRequest> request,
               fuse_ino_t inode,
               uint64_t file_handle,
               off_t offset) override;
  void ReleaseDir(std::unique_ptr<SimpleRequest> request,
                  fuse_ino_t inode,
                  uint64_t file_handle) override;
  void MkDir(std::unique_ptr<EntryRequest> request,
             fuse_ino_t parent_inode,
             const std::string& name,
             mode_t mode) override;
  void RmDir(std::unique_ptr<SimpleRequest> request,
             fuse_ino_t parent_inode,
             const std::string& name) override;

 protected:
  // Protected constructor for unit tests.
  explicit SmbFilesystem(const std::string& share_path);

 private:
  FRIEND_TEST(SmbFilesystemTest, MakeStatModeBits);
  FRIEND_TEST(SmbFilesystemTest, MakeStatModeBitsFromDOSAttributes);

  // Cache stat information when listing directories to reduce unnecessary
  // network requests.
  struct StatCacheItem {
    struct stat inode_stat;
    base::Time expires_at;
  };

  // Filesystem implementations that execute on |samba_thread_|.
  void StatFsInternal(std::unique_ptr<StatFsRequest> request, fuse_ino_t inode);
  void LookupInternal(std::unique_ptr<EntryRequest> request,
                      fuse_ino_t parent_inode,
                      const std::string& name);
  void ForgetInternal(fuse_ino_t inode, uint64_t count);
  void GetAttrInternal(std::unique_ptr<AttrRequest> request, fuse_ino_t inode);
  void SetAttrInternal(std::unique_ptr<AttrRequest> request,
                       fuse_ino_t inode,
                       base::Optional<uint64_t> file_handle,
                       const struct stat& attr,
                       int to_set);
  void OpenInternal(std::unique_ptr<OpenRequest> request,
                    fuse_ino_t inode,
                    int flags);
  void CreateInternal(std::unique_ptr<CreateRequest> request,
                      fuse_ino_t parent_inode,
                      const std::string& name,
                      mode_t mode,
                      int flags);
  void ReadInternal(std::unique_ptr<BufRequest> request,
                    fuse_ino_t inode,
                    uint64_t file_handle,
                    size_t size,
                    off_t offset);
  void WriteInternal(std::unique_ptr<WriteRequest> request,
                     fuse_ino_t inode,
                     uint64_t file_handle,
                     const std::vector<char>& buf,
                     off_t offset);
  void ReleaseInternal(std::unique_ptr<SimpleRequest> request,
                       fuse_ino_t inode,
                       uint64_t file_handle);
  void RenameInternal(std::unique_ptr<SimpleRequest> request,
                      fuse_ino_t old_parent_inode,
                      const std::string& old_name,
                      fuse_ino_t new_parent_inode,
                      const std::string& new_name);
  void UnlinkInternal(std::unique_ptr<SimpleRequest> request,
                      fuse_ino_t parent_inode,
                      const std::string& name);
  void OpenDirInternal(std::unique_ptr<OpenRequest> request,
                       fuse_ino_t inode,
                       int flags);
  void ReadDirInternal(std::unique_ptr<DirentryRequest> request,
                       fuse_ino_t inode,
                       uint64_t file_handle,
                       off_t offset);
  void ReleaseDirInternal(std::unique_ptr<SimpleRequest> request,
                          fuse_ino_t inode,
                          uint64_t file_handle);
  void MkDirInternal(std::unique_ptr<EntryRequest> request,
                     fuse_ino_t parent_inode,
                     const std::string& name,
                     mode_t mode);
  void RmDirinternal(std::unique_ptr<SimpleRequest> request,
                     fuse_ino_t parent_inode,
                     const std::string& name);

  // Constructs a sanitised stat struct for sending as a response.
  struct stat MakeStat(ino_t inode, const struct stat& in_stat) const;

  // Clear / propagate permission bits appropriately (crbug.com/1063715).
  mode_t MakeStatModeBits(mode_t in_mode) const;

  // Constructs mode (type and permission) bits for stat from DOS attributes.
  mode_t MakeStatModeBitsFromDOSAttributes(uint16_t attrs) const;

  // Constructs a share file path suitable for passing to libsmbclient from the
  // given absolute file path.
  std::string MakeShareFilePath(const base::FilePath& path) const;

  // Construct a share file path from the |inode|. |inode| must be a valid inode
  // number.
  std::string ShareFilePathFromInode(ino_t inode) const;

  // Registers an open file and returns a handle to that file. Always returns a
  // non-zero handle.
  uint64_t AddOpenFile(SMBCFILE* file);

  // Removes |handle| from the open file table.
  void RemoveOpenFile(uint64_t handle);

  // Returns the open file referred to by |handle|. Returns nullptr if |handle|
  // does not exist.
  SMBCFILE* LookupOpenFile(uint64_t handle) const;

  // Callback function for obtaining authentication credentials. Set by calling
  // smbc_setFunctionAuthDataWithContext() and called from libsmbclient.
  static void GetUserAuth(SMBCCTX* context,
                          const char* server,
                          const char* share,
                          char* workgroup,
                          int workgroup_len,
                          char* username,
                          int username_len,
                          char* password,
                          int password_len);

  // Cache a stat structure. |inode_stat.st_ino| is used as the key.
  void AddCachedInodeStat(const struct stat& inode_stat);

  // Remove the cached stat structure for |inode|.
  void EraseCachedInodeStat(ino_t inode);

  // Lookup the cached stat structure for |inode|. Returns true on cache hit or
  // false on a miss.
  bool GetCachedInodeStat(ino_t inode, struct stat* out_stat);

  const std::string share_path_;
  const uid_t uid_ = 0;
  const gid_t gid_ = 0;
  const std::unique_ptr<SmbCredential> credentials_;
  base::Thread samba_thread_;
  InodeMap inode_map_{FUSE_ROOT_ID};

  std::unique_ptr<SmbFsImpl> smbfs_impl_;

  std::unordered_map<uint64_t, SMBCFILE*> open_files_;
  uint64_t open_files_seq_ = 1;

  mutable base::Lock lock_;
  std::string resolved_share_path_ = share_path_;

  // Cache stat information during ReadDir() to speed up subsequent access.
  base::HashingMRUCache<ino_t, StatCacheItem> stat_cache_;

  SMBCCTX* context_ = nullptr;
  smbc_close_fn smbc_close_ctx_ = nullptr;
  smbc_closedir_fn smbc_closedir_ctx_ = nullptr;
  smbc_ftruncate_fn smbc_ftruncate_ctx_ = nullptr;
  smbc_lseek_fn smbc_lseek_ctx_ = nullptr;
  smbc_lseekdir_fn smbc_lseekdir_ctx_ = nullptr;
  smbc_mkdir_fn smbc_mkdir_ctx_ = nullptr;
  smbc_open_fn smbc_open_ctx_ = nullptr;
  smbc_opendir_fn smbc_opendir_ctx_ = nullptr;
  smbc_read_fn smbc_read_ctx_ = nullptr;
  smbc_readdir_fn smbc_readdir_ctx_ = nullptr;
  // TODO(crbug.com/1054711): This should be swapped out for
  // smbc_readdirplus2_fn when Samba is updated to 4.12.
  smbc_readdirplus_fn smbc_readdirplus_ctx_ = nullptr;
  smbc_rename_fn smbc_rename_ctx_ = nullptr;
  smbc_rmdir_fn smbc_rmdir_ctx_ = nullptr;
  smbc_stat_fn smbc_stat_ctx_ = nullptr;
  smbc_statvfs_fn smbc_statvfs_ctx_ = nullptr;
  smbc_telldir_fn smbc_telldir_ctx_ = nullptr;
  smbc_unlink_fn smbc_unlink_ctx_ = nullptr;
  smbc_write_fn smbc_write_ctx_ = nullptr;

  DISALLOW_IMPLICIT_CONSTRUCTORS(SmbFilesystem);
};

std::ostream& operator<<(std::ostream& out, SmbFilesystem::ConnectError error);

}  // namespace smbfs

#endif  // SMBFS_SMB_FILESYSTEM_H_
