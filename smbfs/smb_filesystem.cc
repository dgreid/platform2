// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/smb_filesystem.h"

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/logging.h>
#include <base/posix/safe_strerror.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_util.h>

#include "smbfs/smbfs_impl.h"
#include "smbfs/util.h"

namespace smbfs {

namespace {

constexpr char kSambaThreadName[] = "smbfs-libsmb";
constexpr char kUrlPrefix[] = "smb://";

constexpr double kAttrTimeoutSeconds = 5.0;
constexpr mode_t kAllowedFileTypes = S_IFREG | S_IFDIR;
constexpr mode_t kFileModeMask = kAllowedFileTypes | 0770;

void SambaLog(void* private_ptr, int level, const char* msg) {
  VLOG(level) << "libsmbclient: " << msg;
}

bool IsAllowedFileMode(mode_t mode) {
  return mode & kAllowedFileTypes;
}

void CopyCredential(const std::string& cred, char* out, int out_len) {
  DCHECK_GT(out_len, 0);
  if (cred.size() > out_len - 1) {
    LOG(ERROR) << "Credential string longer than buffer provided";
  }
  base::strlcpy(out, cred.c_str(), out_len);
}

void CopyPassword(const password_provider::Password& password,
                  char* out,
                  int out_len) {
  DCHECK_GT(out_len, 0);
  if (password.size() > out_len - 1) {
    LOG(ERROR) << "Password string longer than buffer provided";
  }
  base::strlcpy(out, password.GetRaw(), out_len);
}

}  // namespace

SmbFilesystem::Options::Options() = default;

SmbFilesystem::Options::~Options() = default;

SmbFilesystem::Options::Options(Options&&) = default;

SmbFilesystem::Options& SmbFilesystem::Options::operator=(Options&&) = default;

SmbFilesystem::SmbFilesystem(Options options)
    : share_path_(options.share_path),
      uid_(options.uid),
      gid_(options.gid),
      credentials_(std::move(options.credentials)),
      samba_thread_(kSambaThreadName) {
  // Ensure files are not owned by root.
  CHECK_GT(uid_, 0);
  CHECK_GT(gid_, 0);

  CHECK(!share_path_.empty());
  CHECK_NE(share_path_.back(), '/');

  context_ = smbc_new_context();
  CHECK(context_);
  CHECK(smbc_init_context(context_));

  smbc_setOptionUserData(context_, this);
  smbc_setOptionUseKerberos(context_, 1);
  // Allow fallback to NTLMv2 authentication if Kerberos fails. This does not
  // prevent fallback to anonymous auth if authentication fails.
  smbc_setOptionFallbackAfterKerberos(context_, options.allow_ntlm);
  LOG_IF(WARNING, !options.allow_ntlm) << "NTLM protocol is disabled";
  if (credentials_) {
    smbc_setFunctionAuthDataWithContext(context_, &SmbFilesystem::GetUserAuth);
  }

  smbc_setLogCallback(context_, nullptr, &SambaLog);
  int vlog_level = logging::GetVlogVerbosity();
  if (vlog_level > 0) {
    smbc_setDebug(context_, vlog_level);
  }

  smbc_close_ctx_ = smbc_getFunctionClose(context_);
  smbc_closedir_ctx_ = smbc_getFunctionClosedir(context_);
  smbc_ftruncate_ctx_ = smbc_getFunctionFtruncate(context_);
  smbc_lseek_ctx_ = smbc_getFunctionLseek(context_);
  smbc_lseekdir_ctx_ = smbc_getFunctionLseekdir(context_);
  smbc_mkdir_ctx_ = smbc_getFunctionMkdir(context_);
  smbc_open_ctx_ = smbc_getFunctionOpen(context_);
  smbc_opendir_ctx_ = smbc_getFunctionOpendir(context_);
  smbc_read_ctx_ = smbc_getFunctionRead(context_);
  smbc_readdir_ctx_ = smbc_getFunctionReaddir(context_);
  smbc_rename_ctx_ = smbc_getFunctionRename(context_);
  smbc_rmdir_ctx_ = smbc_getFunctionRmdir(context_);
  smbc_stat_ctx_ = smbc_getFunctionStat(context_);
  smbc_statvfs_ctx_ = smbc_getFunctionStatVFS(context_);
  smbc_telldir_ctx_ = smbc_getFunctionTelldir(context_);
  smbc_unlink_ctx_ = smbc_getFunctionUnlink(context_);
  smbc_write_ctx_ = smbc_getFunctionWrite(context_);

  CHECK(samba_thread_.Start());
}

SmbFilesystem::SmbFilesystem(const std::string& share_path)
    : share_path_(share_path), samba_thread_(kSambaThreadName) {}

SmbFilesystem::~SmbFilesystem() {
  if (context_) {
    // Stop the Samba processing thread before destroying the context to avoid a
    // UAF on the context.
    samba_thread_.Stop();
    smbc_free_context(context_, 1 /* shutdown_ctx */);
  }
}

SmbFilesystem::ConnectError SmbFilesystem::EnsureConnected() {
  SMBCFILE* dir = smbc_opendir_ctx_(context_, resolved_share_path_.c_str());
  if (!dir) {
    int err = errno;
    PLOG(INFO) << "EnsureConnected smbc_opendir_ctx_ failed";
    switch (err) {
      case EPERM:
      case EACCES:
        return ConnectError::kAccessDenied;
      case ENODEV:
      case ENOENT:
      case ETIMEDOUT:
      // This means unable to resolve host, in some, but not necessarily all
      // cases.
      case EINVAL:
      // Host unreachable.
      case EHOSTUNREACH:
      // Host not listening on SMB port.
      case ECONNREFUSED:
        return ConnectError::kNotFound;
      case ECONNABORTED:
        return ConnectError::kSmb1Unsupported;
      default:
        LOG(WARNING) << "Unexpected error code " << err << ": "
                     << base::safe_strerror(err);
        return ConnectError::kUnknownError;
    }
  }

  smbc_closedir_ctx_(context_, dir);
  return ConnectError::kOk;
}

void SmbFilesystem::SetSmbFsImpl(std::unique_ptr<SmbFsImpl> impl) {
  smbfs_impl_ = std::move(impl);
}

void SmbFilesystem::SetResolvedAddress(const std::vector<uint8_t>& ip_address) {
  base::AutoLock l(lock_);

  if (ip_address.empty()) {
    resolved_share_path_ = share_path_;
    return;
  } else if (ip_address.size() != 4) {
    // TODO(crbug.com/1051291): Support IPv6.
    LOG(ERROR) << "Invalid IP address";
    return;
  }

  std::string address_str = IpAddressToString(ip_address);
  DCHECK(!address_str.empty());

  const base::StringPiece prefix(kUrlPrefix);
  DCHECK(base::StartsWith(share_path_, prefix, base::CompareCase::SENSITIVE));
  std::string::size_type host_end = share_path_.find('/', prefix.size());
  DCHECK_NE(host_end, std::string::npos);
  resolved_share_path_ =
      std::string(prefix) + address_str + share_path_.substr(host_end);
}

struct stat SmbFilesystem::MakeStat(ino_t inode,
                                    const struct stat& in_stat) const {
  struct stat stat = {0};
  stat.st_ino = inode;
  stat.st_mode = in_stat.st_mode & kFileModeMask;
  stat.st_uid = uid_;
  stat.st_gid = gid_;
  stat.st_nlink = 1;
  stat.st_size = in_stat.st_size;
  stat.st_atim = in_stat.st_atim;
  stat.st_ctim = in_stat.st_ctim;
  stat.st_mtim = in_stat.st_mtim;
  return stat;
}

std::string SmbFilesystem::MakeShareFilePath(const base::FilePath& path) const {
  std::string base_share_path;
  {
    base::AutoLock l(lock_);
    DCHECK(!resolved_share_path_.empty());
    base_share_path = resolved_share_path_;
  }

  if (path == base::FilePath("/")) {
    return base_share_path;
  }

  // Paths are constructed and not passed directly over FUSE. Therefore, these
  // two properties should always hold.
  DCHECK(path.IsAbsolute());
  DCHECK(!path.EndsWithSeparator());
  return base_share_path + path.value();
}

std::string SmbFilesystem::ShareFilePathFromInode(ino_t inode) const {
  const base::FilePath file_path = inode_map_.GetPath(inode);
  CHECK(!file_path.empty()) << "Path lookup for invalid inode: " << inode;
  return MakeShareFilePath(file_path);
}

uint64_t SmbFilesystem::AddOpenFile(SMBCFILE* file) {
  uint64_t handle = open_files_seq_++;
  // Disallow wrap around.
  CHECK(handle);
  open_files_[handle] = file;
  return handle;
}

void SmbFilesystem::RemoveOpenFile(uint64_t handle) {
  auto it = open_files_.find(handle);
  if (it == open_files_.end()) {
    NOTREACHED() << "File handle not found";
    return;
  }
  open_files_.erase(it);
}

SMBCFILE* SmbFilesystem::LookupOpenFile(uint64_t handle) const {
  const auto it = open_files_.find(handle);
  if (it == open_files_.end()) {
    return nullptr;
  }
  return it->second;
}

// static
void SmbFilesystem::GetUserAuth(SMBCCTX* context,
                                const char* server,
                                const char* share,
                                char* workgroup,
                                int workgroup_len,
                                char* username,
                                int username_len,
                                char* password,
                                int password_len) {
  SmbFilesystem* fs =
      static_cast<SmbFilesystem*>(smbc_getOptionUserData(context));
  DCHECK(fs);
  DCHECK(fs->credentials_);

  CopyCredential(fs->credentials_->workgroup, workgroup, workgroup_len);
  CopyCredential(fs->credentials_->username, username, username_len);
  password[0] = 0;
  if (fs->credentials_->password) {
    CopyPassword(*fs->credentials_->password, password, password_len);
  }
}

void SmbFilesystem::StatFs(std::unique_ptr<StatFsRequest> request,
                           fuse_ino_t inode) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::StatFsInternal, base::Unretained(this),
                     std::move(request), inode));
}

void SmbFilesystem::StatFsInternal(std::unique_ptr<StatFsRequest> request,
                                   fuse_ino_t inode) {
  if (request->IsInterrupted()) {
    return;
  }

  std::string share_file_path = ShareFilePathFromInode(inode);
  struct statvfs smb_statvfs = {0};
  // libsmbclient's statvfs() takes a non-const char* as path, hence the
  // address-of-first-element pattern/hack.
  int error = smbc_statvfs_ctx_(context_, &share_file_path[0], &smb_statvfs);
  if (error < 0) {
    request->ReplyError(errno);
    return;
  }

  if ((smb_statvfs.f_flag & SMBC_VFS_FEATURE_NO_UNIXCIFS) &&
      smb_statvfs.f_frsize) {
    // If the server does not support the UNIX CIFS extensions, libsmbclient
    // incorrectly fills out the value of f_frsize. Instead of providing the
    // size in bytes, it provides it as a multiple of f_bsize. See the
    // implementation of SMBC_fstatvfs_ctx() in the Samba source tree for
    // details.
    smb_statvfs.f_frsize *= smb_statvfs.f_bsize;
  }
  request->ReplyStatFs(smb_statvfs);
}

void SmbFilesystem::Lookup(std::unique_ptr<EntryRequest> request,
                           fuse_ino_t parent_inode,
                           const std::string& name) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::LookupInternal, base::Unretained(this),
                     std::move(request), parent_inode, name));
}

void SmbFilesystem::LookupInternal(std::unique_ptr<EntryRequest> request,
                                   fuse_ino_t parent_inode,
                                   const std::string& name) {
  if (request->IsInterrupted()) {
    return;
  }

  const base::FilePath parent_path = inode_map_.GetPath(parent_inode);
  CHECK(!parent_path.empty())
      << "Lookup on invalid parent inode: " << parent_inode;
  const base::FilePath file_path = parent_path.Append(name);
  const std::string share_file_path = MakeShareFilePath(file_path);

  struct stat smb_stat = {0};
  int error = smbc_stat_ctx_(context_, share_file_path.c_str(), &smb_stat);
  if (error < 0) {
    request->ReplyError(errno);
    return;
  } else if (!IsAllowedFileMode(smb_stat.st_mode)) {
    VLOG(1) << "Disallowed file mode " << smb_stat.st_mode << " for path "
            << share_file_path;
    request->ReplyError(EACCES);
    return;
  }

  ino_t inode = inode_map_.IncInodeRef(file_path);
  struct stat entry_stat = MakeStat(inode, smb_stat);

  fuse_entry_param entry = {0};
  entry.ino = inode;
  entry.generation = 1;
  entry.attr = entry_stat;
  entry.attr_timeout = kAttrTimeoutSeconds;
  entry.entry_timeout = kAttrTimeoutSeconds;
  request->ReplyEntry(entry);
}

void SmbFilesystem::Forget(fuse_ino_t inode, uint64_t count) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SmbFilesystem::ForgetInternal,
                                base::Unretained(this), inode, count));
}

void SmbFilesystem::ForgetInternal(fuse_ino_t inode, uint64_t count) {
  inode_map_.Forget(inode, count);
}

void SmbFilesystem::GetAttr(std::unique_ptr<AttrRequest> request,
                            fuse_ino_t inode) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::GetAttrInternal, base::Unretained(this),
                     std::move(request), inode));
}

void SmbFilesystem::GetAttrInternal(std::unique_ptr<AttrRequest> request,
                                    fuse_ino_t inode) {
  if (request->IsInterrupted()) {
    return;
  }

  const std::string share_file_path = ShareFilePathFromInode(inode);
  struct stat smb_stat = {0};
  int error = smbc_stat_ctx_(context_, share_file_path.c_str(), &smb_stat);
  if (error < 0) {
    request->ReplyError(errno);
    return;
  } else if (!IsAllowedFileMode(smb_stat.st_mode)) {
    VLOG(1) << "Disallowed file mode " << smb_stat.st_mode << " for path "
            << share_file_path;
    request->ReplyError(EACCES);
    return;
  }

  struct stat reply_stat = MakeStat(inode, smb_stat);
  request->ReplyAttr(reply_stat, kAttrTimeoutSeconds);
}

void SmbFilesystem::SetAttr(std::unique_ptr<AttrRequest> request,
                            fuse_ino_t inode,
                            base::Optional<uint64_t> file_handle,
                            const struct stat& attr,
                            int to_set) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&SmbFilesystem::SetAttrInternal,
                                base::Unretained(this), std::move(request),
                                inode, std::move(file_handle), attr, to_set));
}

void SmbFilesystem::SetAttrInternal(std::unique_ptr<AttrRequest> request,
                                    fuse_ino_t inode,
                                    base::Optional<uint64_t> file_handle,
                                    const struct stat& attr,
                                    int to_set) {
  if (request->IsInterrupted()) {
    return;
  }

  // Currently, only setting size is supported (ie. O_TRUC, ftruncate()).
  const int kSupportedAttrs = FUSE_SET_ATTR_SIZE;
  if (to_set & ~kSupportedAttrs) {
    LOG(WARNING) << "Unsupported |to_set| flags on setattr: " << to_set;
    request->ReplyError(ENOTSUP);
    return;
  }
  if (!to_set) {
    VLOG(1) << "No supported |to_set| flags set on setattr: " << to_set;
    request->ReplyError(EINVAL);
    return;
  }

  const std::string share_file_path = ShareFilePathFromInode(inode);

  struct stat smb_stat = {0};
  int error = smbc_stat_ctx_(context_, share_file_path.c_str(), &smb_stat);
  if (error < 0) {
    request->ReplyError(errno);
    return;
  }
  if (smb_stat.st_mode & S_IFDIR) {
    request->ReplyError(EISDIR);
    return;
  } else if (!(smb_stat.st_mode & S_IFREG)) {
    VLOG(1) << "Disallowed file mode " << smb_stat.st_mode << " for path "
            << share_file_path;
    request->ReplyError(EACCES);
    return;
  }
  struct stat reply_stat = MakeStat(inode, smb_stat);

  SMBCFILE* file = nullptr;
  base::ScopedClosureRunner file_closer;
  if (file_handle) {
    file = LookupOpenFile(*file_handle);
    if (!file) {
      request->ReplyError(EBADF);
      return;
    }
  } else {
    file = smbc_open_ctx_(context_, share_file_path.c_str(), O_WRONLY, 0);
    if (!file) {
      int err = errno;
      VPLOG(1) << "smbc_open path: " << share_file_path << " failed";
      request->ReplyError(err);
      return;
    }

    file_closer.ReplaceClosure(base::BindOnce(
        [](SMBCCTX* context, SMBCFILE* file) {
          if (smbc_getFunctionClose(context)(context, file) < 0) {
            PLOG(ERROR) << "smbc_close failed on temporary setattr file";
          }
        },
        context_, file));
  }

  if (smbc_ftruncate_ctx_(context_, file, attr.st_size) < 0) {
    int err = errno;
    VPLOG(1) << "smbc_ftruncate size: " << attr.st_size << " failed";
    request->ReplyError(err);
    return;
  }
  reply_stat.st_size = attr.st_size;
  request->ReplyAttr(reply_stat, kAttrTimeoutSeconds);
}

void SmbFilesystem::Open(std::unique_ptr<OpenRequest> request,
                         fuse_ino_t inode,
                         int flags) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::OpenInternal, base::Unretained(this),
                     std::move(request), inode, flags));
}

void SmbFilesystem::OpenInternal(std::unique_ptr<OpenRequest> request,
                                 fuse_ino_t inode,
                                 int flags) {
  if (request->IsInterrupted()) {
    return;
  }

  if (inode == FUSE_ROOT_ID) {
    request->ReplyError(EISDIR);
    return;
  }

  const std::string share_file_path = ShareFilePathFromInode(inode);
  SMBCFILE* file = smbc_open_ctx_(context_, share_file_path.c_str(), flags, 0);
  if (!file) {
    int err = errno;
    VPLOG(1) << "smbc_open on path " << share_file_path << " failed";
    request->ReplyError(err);
    return;
  }

  request->ReplyOpen(AddOpenFile(file));
}

void SmbFilesystem::Create(std::unique_ptr<CreateRequest> request,
                           fuse_ino_t parent_inode,
                           const std::string& name,
                           mode_t mode,
                           int flags) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::CreateInternal, base::Unretained(this),
                     std::move(request), parent_inode, name, mode, flags));
}

void SmbFilesystem::CreateInternal(std::unique_ptr<CreateRequest> request,
                                   fuse_ino_t parent_inode,
                                   const std::string& name,
                                   mode_t mode,
                                   int flags) {
  if (request->IsInterrupted()) {
    return;
  }

  flags |= O_CREAT;
  mode &= 0777;

  const base::FilePath parent_path = inode_map_.GetPath(parent_inode);
  CHECK(!parent_path.empty())
      << "Lookup on invalid parent inode: " << parent_inode;
  const base::FilePath file_path = parent_path.Append(name);
  const std::string share_file_path = MakeShareFilePath(file_path);

  // NOTE: |mode| appears to be ignored by libsmbclient.
  SMBCFILE* file =
      smbc_open_ctx_(context_, share_file_path.c_str(), flags, mode);
  if (!file) {
    int err = errno;
    VPLOG(1) << "smbc_open path: " << share_file_path << " failed";
    request->ReplyError(err);
    return;
  }

  uint64_t handle = AddOpenFile(file);

  ino_t inode = inode_map_.IncInodeRef(file_path);
  struct stat entry_stat = MakeStat(inode, {0});
  entry_stat.st_mode = S_IFREG | mode;
  fuse_entry_param entry = {0};
  entry.ino = inode;
  entry.generation = 1;
  entry.attr = entry_stat;
  entry.attr_timeout = kAttrTimeoutSeconds;
  entry.entry_timeout = kAttrTimeoutSeconds;
  request->ReplyCreate(entry, handle);
}

void SmbFilesystem::Read(std::unique_ptr<BufRequest> request,
                         fuse_ino_t inode,
                         uint64_t file_handle,
                         size_t size,
                         off_t offset) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::ReadInternal, base::Unretained(this),
                     std::move(request), inode, file_handle, size, offset));
}

void SmbFilesystem::ReadInternal(std::unique_ptr<BufRequest> request,
                                 fuse_ino_t inode,
                                 uint64_t file_handle,
                                 size_t size,
                                 off_t offset) {
  if (request->IsInterrupted()) {
    return;
  }

  SMBCFILE* file = LookupOpenFile(file_handle);
  if (!file) {
    request->ReplyError(EBADF);
    return;
  }

  if (smbc_lseek_ctx_(context_, file, offset, SEEK_SET) < 0) {
    int err = errno;
    VPLOG(1) << "smbc_lseek path: " << ShareFilePathFromInode(inode)
             << ", offset: " << offset << " failed";
    request->ReplyError(err);
    return;
  }

  std::vector<char> buf(size);
  ssize_t bytes_read = smbc_read_ctx_(context_, file, buf.data(), size);
  if (bytes_read < 0) {
    int err = errno;
    VPLOG(1) << "smbc_read path: " << ShareFilePathFromInode(inode)
             << " offset: " << offset << ", size: " << size << " failed";
    request->ReplyError(err);
    return;
  }

  request->ReplyBuf(buf.data(), bytes_read);
}

void SmbFilesystem::Write(std::unique_ptr<WriteRequest> request,
                          fuse_ino_t inode,
                          uint64_t file_handle,
                          const char* buf,
                          size_t size,
                          off_t offset) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::WriteInternal, base::Unretained(this),
                     std::move(request), inode, file_handle,
                     std::vector<char>(buf, buf + size), offset));
}

void SmbFilesystem::WriteInternal(std::unique_ptr<WriteRequest> request,
                                  fuse_ino_t inode,
                                  uint64_t file_handle,
                                  const std::vector<char>& buf,
                                  off_t offset) {
  if (request->IsInterrupted()) {
    return;
  }

  SMBCFILE* file = LookupOpenFile(file_handle);
  if (!file) {
    request->ReplyError(EBADF);
    return;
  }

  if (smbc_lseek_ctx_(context_, file, offset, SEEK_SET) < 0) {
    int err = errno;
    VPLOG(1) << "smbc_lseek path: " << ShareFilePathFromInode(inode)
             << ", offset: " << offset << " failed";
    request->ReplyError(err);
    return;
  }

  ssize_t bytes_written =
      smbc_write_ctx_(context_, file, buf.data(), buf.size());
  if (bytes_written < 0) {
    int err = errno;
    VPLOG(1) << "smbc_write path: " << ShareFilePathFromInode(inode)
             << " offset: " << offset << ", size: " << buf.size() << " failed";
    request->ReplyError(err);
    return;
  }

  request->ReplyWrite(bytes_written);
}

void SmbFilesystem::Release(std::unique_ptr<SimpleRequest> request,
                            fuse_ino_t inode,
                            uint64_t file_handle) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::ReleaseInternal, base::Unretained(this),
                     std::move(request), inode, file_handle));
}

void SmbFilesystem::ReleaseInternal(std::unique_ptr<SimpleRequest> request,
                                    fuse_ino_t inode,
                                    uint64_t file_handle) {
  if (request->IsInterrupted()) {
    return;
  }

  SMBCFILE* file = LookupOpenFile(file_handle);
  if (!file) {
    request->ReplyError(EBADF);
    return;
  }

  if (smbc_close_ctx_(context_, file) < 0) {
    request->ReplyError(errno);
    return;
  }

  RemoveOpenFile(file_handle);
  request->ReplyOk();
}

void SmbFilesystem::Rename(std::unique_ptr<SimpleRequest> request,
                           fuse_ino_t old_parent_inode,
                           const std::string& old_name,
                           fuse_ino_t new_parent_inode,
                           const std::string& new_name) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::RenameInternal, base::Unretained(this),
                     std::move(request), old_parent_inode, old_name,
                     new_parent_inode, new_name));
}

void SmbFilesystem::RenameInternal(std::unique_ptr<SimpleRequest> request,
                                   fuse_ino_t old_parent_inode,
                                   const std::string& old_name,
                                   fuse_ino_t new_parent_inode,
                                   const std::string& new_name) {
  if (request->IsInterrupted()) {
    return;
  }

  const base::FilePath old_parent_path = inode_map_.GetPath(old_parent_inode);
  CHECK(!old_parent_path.empty())
      << "Lookup on invalid old parent inode: " << old_parent_inode;
  const base::FilePath new_parent_path = inode_map_.GetPath(new_parent_inode);
  CHECK(!new_parent_path.empty())
      << "Lookup on invalid new parent inode: " << new_parent_inode;

  const std::string old_share_path =
      MakeShareFilePath(old_parent_path.Append(old_name));
  const std::string new_share_path =
      MakeShareFilePath(new_parent_path.Append(new_name));

  if (smbc_rename_ctx_(context_, old_share_path.c_str(), context_,
                       new_share_path.c_str()) < 0) {
    int err = errno;
    VPLOG(1) << "smbc_rename old_path: " << old_share_path
             << " new_path: " << new_share_path << " failed";
    request->ReplyError(err);
    return;
  }

  request->ReplyOk();
}

void SmbFilesystem::Unlink(std::unique_ptr<SimpleRequest> request,
                           fuse_ino_t parent_inode,
                           const std::string& name) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::UnlinkInternal, base::Unretained(this),
                     std::move(request), parent_inode, name));
}

void SmbFilesystem::UnlinkInternal(std::unique_ptr<SimpleRequest> request,
                                   fuse_ino_t parent_inode,
                                   const std::string& name) {
  if (request->IsInterrupted()) {
    return;
  }

  const base::FilePath parent_path = inode_map_.GetPath(parent_inode);
  CHECK(!parent_path.empty())
      << "Lookup on invalid parent inode: " << parent_inode;
  const std::string share_file_path =
      MakeShareFilePath(parent_path.Append(name));

  if (smbc_unlink_ctx_(context_, share_file_path.c_str()) < 0) {
    int err = errno;
    VPLOG(1) << "smbc_unlink path: " << share_file_path << " failed";
    request->ReplyError(err);
    return;
  }

  request->ReplyOk();
}

void SmbFilesystem::OpenDir(std::unique_ptr<OpenRequest> request,
                            fuse_ino_t inode,
                            int flags) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::OpenDirInternal, base::Unretained(this),
                     std::move(request), inode, flags));
}

void SmbFilesystem::OpenDirInternal(std::unique_ptr<OpenRequest> request,
                                    fuse_ino_t inode,
                                    int flags) {
  if (request->IsInterrupted()) {
    return;
  }

  if ((flags & O_ACCMODE) != O_RDONLY) {
    request->ReplyError(EACCES);
    return;
  }

  const std::string share_dir_path = ShareFilePathFromInode(inode);
  SMBCFILE* dir = smbc_opendir_ctx_(context_, share_dir_path.c_str());
  if (!dir) {
    request->ReplyError(errno);
    return;
  }

  request->ReplyOpen(AddOpenFile(dir));
}

void SmbFilesystem::ReadDir(std::unique_ptr<DirentryRequest> request,
                            fuse_ino_t inode,
                            uint64_t file_handle,
                            off_t offset) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::ReadDirInternal, base::Unretained(this),
                     std::move(request), inode, file_handle, offset));
}

void SmbFilesystem::ReadDirInternal(std::unique_ptr<DirentryRequest> request,
                                    fuse_ino_t inode,
                                    uint64_t file_handle,
                                    off_t offset) {
  if (request->IsInterrupted()) {
    return;
  }

  if (offset < 0) {
    // A previous readdir() returned -1 as the next offset, which implies EOF.
    request->ReplyDone();
    return;
  }

  SMBCFILE* dir = LookupOpenFile(file_handle);
  if (!dir) {
    request->ReplyError(EBADF);
    return;
  }
  const base::FilePath dir_path = inode_map_.GetPath(inode);
  CHECK(!dir_path.empty()) << "Inode not found: " << inode;

  int error = smbc_lseekdir_ctx_(context_, dir, offset);
  if (error < 0) {
    int err = errno;
    VPLOG(1) << "smbc_lseekdir on path " << dir_path.value()
             << ", offset: " << offset << " failed";
    request->ReplyError(err);
    return;
  }

  while (true) {
    // Explicitly set |errno| to 0 to detect EOF vs. error cases.
    errno = 0;
    const smbc_dirent* dirent = smbc_readdir_ctx_(context_, dir);
    if (!dirent) {
      if (!errno) {
        // EOF.
        break;
      }
      int err = errno;
      VPLOG(1) << "smbc_readdir on path " << dir_path.value() << " failed";
      request->ReplyError(err);
      return;
    }
    off_t next_offset = smbc_telldir_ctx_(context_, dir);
    if (next_offset < 0 && errno) {
      int err = errno;
      VPLOG(1) << "smbc_telldir on path " << dir_path.value() << " failed";
      request->ReplyError(err);
      return;
    }

    base::StringPiece filename(dirent->name);
    if (filename == "." || filename == "..") {
      // Ignore . and .. since FUSE already takes care of these.
      continue;
    }
    CHECK(!filename.empty());
    CHECK_EQ(filename.find("/"), base::StringPiece::npos);
    mode_t mode;
    if (dirent->smbc_type == SMBC_FILE) {
      mode = S_IFREG;
    } else if (dirent->smbc_type == SMBC_DIR) {
      mode = S_IFDIR;
    } else {
      VLOG(1) << "Ignoring directory entry of unsupported type: "
              << dirent->smbc_type;
      continue;
    }

    const base::FilePath entry_path = dir_path.Append(filename);
    ino_t entry_inode = inode_map_.IncInodeRef(entry_path);
    if (!request->AddEntry(filename, entry_inode, mode, next_offset)) {
      // Response buffer full.
      inode_map_.Forget(entry_inode, 1);
      break;
    }
  }

  request->ReplyDone();
}

void SmbFilesystem::ReleaseDir(std::unique_ptr<SimpleRequest> request,
                               fuse_ino_t inode,
                               uint64_t file_handle) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::ReleaseDirInternal, base::Unretained(this),
                     std::move(request), inode, file_handle));
}

void SmbFilesystem::ReleaseDirInternal(std::unique_ptr<SimpleRequest> request,
                                       fuse_ino_t inode,
                                       uint64_t file_handle) {
  if (request->IsInterrupted()) {
    return;
  }

  SMBCFILE* dir = LookupOpenFile(file_handle);
  if (!dir) {
    request->ReplyError(EBADF);
    return;
  }

  if (smbc_closedir_ctx_(context_, dir) < 0) {
    request->ReplyError(errno);
    return;
  }

  RemoveOpenFile(file_handle);
  request->ReplyOk();
}

void SmbFilesystem::MkDir(std::unique_ptr<EntryRequest> request,
                          fuse_ino_t parent_inode,
                          const std::string& name,
                          mode_t mode) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::MkDirInternal, base::Unretained(this),
                     std::move(request), parent_inode, name, mode));
}

void SmbFilesystem::MkDirInternal(std::unique_ptr<EntryRequest> request,
                                  fuse_ino_t parent_inode,
                                  const std::string& name,
                                  mode_t mode) {
  if (request->IsInterrupted()) {
    return;
  }

  const base::FilePath parent_path = inode_map_.GetPath(parent_inode);
  CHECK(!parent_path.empty())
      << "Lookup on invalid parent inode: " << parent_inode;
  const base::FilePath file_path = parent_path.Append(name);
  const std::string share_file_path = MakeShareFilePath(file_path);

  if (smbc_mkdir_ctx_(context_, share_file_path.c_str(), mode) < 0) {
    int err = errno;
    VPLOG(1) << "smbc_mkdir path: " << share_file_path << " failed";
    request->ReplyError(err);
    return;
  }

  ino_t inode = inode_map_.IncInodeRef(file_path);
  struct stat entry_stat = MakeStat(inode, {0});
  entry_stat.st_mode = S_IFDIR | mode;
  fuse_entry_param entry = {0};
  entry.ino = inode;
  entry.generation = 1;
  entry.attr = entry_stat;
  entry.attr_timeout = kAttrTimeoutSeconds;
  entry.entry_timeout = kAttrTimeoutSeconds;
  request->ReplyEntry(entry);
}

void SmbFilesystem::RmDir(std::unique_ptr<SimpleRequest> request,
                          fuse_ino_t parent_inode,
                          const std::string& name) {
  samba_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&SmbFilesystem::RmDirinternal, base::Unretained(this),
                     std::move(request), parent_inode, name));
}

void SmbFilesystem::RmDirinternal(std::unique_ptr<SimpleRequest> request,
                                  fuse_ino_t parent_inode,
                                  const std::string& name) {
  if (request->IsInterrupted()) {
    return;
  }

  const base::FilePath parent_path = inode_map_.GetPath(parent_inode);
  CHECK(!parent_path.empty())
      << "Lookup on invalid parent inode: " << parent_inode;
  const base::FilePath file_path = parent_path.Append(name);
  const std::string share_file_path = MakeShareFilePath(file_path);

  if (smbc_rmdir_ctx_(context_, share_file_path.c_str()) < 0) {
    int err = errno;
    VPLOG(1) << "smbc_rmdir path: " << share_file_path << " failed";
    request->ReplyError(err);
    return;
  }

  request->ReplyOk();
}

std::ostream& operator<<(std::ostream& out, SmbFilesystem::ConnectError error) {
  switch (error) {
    case SmbFilesystem::ConnectError::kOk:
      return out << "kOk";
    case SmbFilesystem::ConnectError::kNotFound:
      return out << "kNotFound";
    case SmbFilesystem::ConnectError::kAccessDenied:
      return out << "kAccessDenied";
    case SmbFilesystem::ConnectError::kSmb1Unsupported:
      return out << "kSmb1Unsupported";
    case SmbFilesystem::ConnectError::kUnknownError:
      return out << "kUnknownError";
    default:
      NOTREACHED();
      return out << "INVALID_ERROR";
  }
}

}  // namespace smbfs
