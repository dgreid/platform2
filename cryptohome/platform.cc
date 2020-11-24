// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Platform

#include "cryptohome/platform.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <linux/loop.h>
#include <mntent.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/quota.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <memory>

#include <ios>
#include <limits>
#include <sstream>
#include <utility>

#if USE_SELINUX
#include <selinux/restorecon.h>
#include <selinux/selinux.h>
#endif

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/numerics/safe_conversions.h>
#include <base/posix/eintr_wrapper.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <brillo/file_utils.h>
#include <brillo/files/safe_fd.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>
#include <openssl/rand.h>
#include <secure_erase_file/secure_erase_file.h>

extern "C" {
#include <keyutils.h>
#include <linux/fs.h>
// Uses libvboot_host for accessing crossystem variables.
#include <vboot/crossystem.h>
}

#include "cryptohome/crc32.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/dircrypto_util.h"

using base::FilePath;
using base::SplitString;
using base::StringPrintf;

namespace {

// Log sync(), fsync(), etc. calls that take this many seconds or longer.
const int kLongSyncSec = 10;

class ScopedPath {
 public:
  ScopedPath(cryptohome::Platform* platform, const FilePath& dir)
      : platform_(platform), dir_(dir) {}
  ~ScopedPath() {
    if (!dir_.empty() && !platform_->DeleteFile(dir_, true)) {
      PLOG(WARNING) << "Failed to clean up " << dir_.value();
    }
  }
  void release() { dir_.clear(); }

 private:
  cryptohome::Platform* platform_;
  FilePath dir_;
};

bool IsDirectory(const base::stat_wrapper_t& file_info) {
  return !!S_ISDIR(file_info.st_mode);
}

/*
 * Split a /proc/<id>/mountinfo line in arguments and
 * populate information into |mount_info|.
 * Return true if the line seems valid.
 */
bool DecodeProcInfoLine(const std::string& line,
                        cryptohome::DecodedProcMountInfo* mount_info) {
  auto args =
      SplitString(line, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  size_t fs_idx = 6;
  if (args.size() <= fs_idx) {
    LOG(ERROR) << "Invalid procinfo: too few items: " << line;
    return false;
  }

  while (fs_idx < args.size() && args[fs_idx++] != "-") {
  }
  if (fs_idx + 1 >= args.size()) {
    LOG(ERROR) << "Invalid procinfo: separator or mount_source not found: "
               << line;
    return false;
  } else {
    mount_info->root = args[3];
    mount_info->mount_point = args[4];
    mount_info->filesystem_type = args[fs_idx];
    mount_info->mount_source = args[fs_idx + 1];
    return true;
  }
}

}  // namespace

namespace cryptohome {

const uint32_t kDefaultMountFlags = MS_NOEXEC | MS_NOSUID | MS_NODEV;
const int kDefaultPwnameLength = 1024;
const int kDefaultUmask =
    S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH;
const char kProcDir[] = "/proc";
const char kMountInfoFile[] = "mountinfo";
const char kPathTune2fs[] = "/sbin/tune2fs";
const char kEcryptFS[] = "ecryptfs";
const char kLoopControl[] = "/dev/loop-control";
const char kLoopPrefix[] = "/dev/loop";
const char kSysBlockPath[] = "/sys/block";
const char kDevPath[] = "/dev";
const char kLoopBackingFile[] = "loop/backing_file";
const std::vector<std::string> kDefaultExt4FormatOpts(
    {// Always use 'default' configuration.
     "-T", "default",
     // reserved-blocks-percentage = 0%
     "-m", "0",
     // ^huge_file: Do not allow files larger than 2TB.
     // ^flex_bg: Do not allow per-block group metadata to be placed anywhere.
     // ^has_journal: Do not create journal.
     "-O", "^huge_file,^flex_bg,^has_journal",
     // Attempt to discard blocks at mkfs time.
     "-E", "discard"});

Platform::Platform() {
  pid_t pid = getpid();
  mount_info_path_ =
      FilePath(kProcDir).Append(std::to_string(pid)).Append(kMountInfoFile);
}

Platform::~Platform() {}

std::vector<DecodedProcMountInfo> Platform::ReadMountInfoFile() {
  std::string contents;
  if (!base::ReadFileToString(mount_info_path_, &contents))
    return std::vector<DecodedProcMountInfo>();

  std::vector<std::string> lines = SplitString(
      contents, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  std::vector<DecodedProcMountInfo> mount_info_content;
  for (const auto& line : lines) {
    DecodedProcMountInfo mount_info;
    if (!DecodeProcInfoLine(line, &mount_info))
      return std::vector<DecodedProcMountInfo>();
    mount_info_content.push_back(mount_info);
  }
  return mount_info_content;
}

bool Platform::GetLoopDeviceMounts(
    std::multimap<const FilePath, const FilePath>* mounts) {
  std::vector<DecodedProcMountInfo> proc_mounts = ReadMountInfoFile();

  // Populate all mounts from loop devices "/dev/loop*".
  for (const auto& mount : proc_mounts) {
    if (!base::StartsWith(mount.mount_source, kLoopPrefix,
                          base::CompareCase::SENSITIVE))
      continue;
    mounts->insert(std::pair<const FilePath, const FilePath>(
        FilePath(mount.mount_source), FilePath(mount.mount_point)));
  }
  return mounts && mounts->size() > 0;
}

bool Platform::GetMountsBySourcePrefix(
    const FilePath& from_prefix,
    std::multimap<const FilePath, const FilePath>* mounts) {
  std::vector<DecodedProcMountInfo> proc_mounts = ReadMountInfoFile();

  // When using ecryptfs, we compare the mount device, otherwise,
  // we use the root directory .
  for (const auto& mount : proc_mounts) {
    FilePath root_dir;
    if (mount.filesystem_type == kEcryptFS)
      root_dir = FilePath(mount.mount_source);
    else
      root_dir = FilePath(mount.root);
    if (!from_prefix.IsParent(root_dir))
      continue;
    // If there is no mounts pointer, we can return true right away.
    if (!mounts)
      return true;
    mounts->insert(std::pair<const FilePath, const FilePath>(
        root_dir, FilePath(mount.mount_point)));
  }
  return mounts && mounts->size();
}

bool Platform::IsDirectoryMounted(const FilePath& directory) {
  // Trivial string match from /etc/mtab to see if the cryptohome mount point is
  // listed.  This works because Chrome OS is a controlled environment and the
  // only way /home/chronos/user should be mounted is if cryptohome mounted it.
  auto ret = AreDirectoriesMounted({directory});

  if (!ret)
    return false;

  return ret.value()[0];
}

base::Optional<std::vector<bool>> Platform::AreDirectoriesMounted(
    const std::vector<base::FilePath>& directories) {
  std::string contents;
  if (!base::ReadFileToString(mount_info_path_, &contents)) {
    return base::nullopt;
  }

  std::vector<bool> ret;
  ret.reserve(directories.size());

  for (auto& directory : directories) {
    bool is_mounted =
        contents.find(StringPrintf(" %s ", directory.value().c_str())) !=
        std::string::npos;

    ret.push_back(is_mounted);
  }

  return ret;
}

bool Platform::Mount(const FilePath& from,
                     const FilePath& to,
                     const std::string& type,
                     uint32_t mount_flags,
                     const std::string& mount_options) {
  if (mount(from.value().c_str(), to.value().c_str(), type.c_str(), mount_flags,
            mount_options.c_str())) {
    return false;
  }
  return true;
}

bool Platform::Bind(const FilePath& from, const FilePath& to) {
  // To apply options specific to a bind mount, we have to call mount(2) twice.
  if (mount(from.value().c_str(), to.value().c_str(), nullptr, MS_BIND,
            nullptr))
    return false;
  if (mount(nullptr, to.value().c_str(), nullptr,
            MS_REMOUNT | MS_BIND | kDefaultMountFlags, nullptr))
    return false;
  return true;
}

bool Platform::Unmount(const FilePath& path, bool lazy, bool* was_busy) {
  if (lazy) {
    if (umount2(path.value().c_str(), MNT_DETACH)) {
      if (was_busy) {
        *was_busy = (errno == EBUSY);
      }
      return false;
    }
  } else {
    if (umount(path.value().c_str())) {
      if (was_busy) {
        *was_busy = (errno == EBUSY);
      }
      return false;
    }
  }
  if (was_busy) {
    *was_busy = false;
  }
  return true;
}

void Platform::LazyUnmount(const FilePath& path) {
  if (umount2(path.value().c_str(), MNT_DETACH | UMOUNT_NOFOLLOW)) {
    if (errno != EBUSY) {
      PLOG(ERROR) << "Lazy unmount failed";
    }
  }
}

std::unique_ptr<brillo::Process> Platform::CreateProcessInstance() {
  return std::make_unique<brillo::ProcessImpl>();
}

void Platform::GetProcessesWithOpenFiles(
    const FilePath& path, std::vector<ProcessInformation>* processes) {
  std::vector<pid_t> pids;
  LookForOpenFiles(path, &pids);
  for (auto pid : pids) {
    processes->push_back(ProcessInformation());
    GetProcessOpenFileInformation(pid, path,
                                  &processes->at(processes->size() - 1));
  }
}

void Platform::GetProcessOpenFileInformation(pid_t pid,
                                             const FilePath& file_in,
                                             ProcessInformation* process_info) {
  process_info->set_process_id(pid);
  FilePath pid_path(StringPrintf("/proc/%d", pid));
  FilePath cmdline_file = pid_path.Append("cmdline");
  std::string contents;
  std::vector<std::string> cmd_line;
  if (base::ReadFileToString(cmdline_file, &contents)) {
    // Can't split at null characters, so replace them with \n.
    std::replace(contents.begin(), contents.end(), '\0', '\n');
    cmd_line = SplitString(contents, "\n", base::KEEP_WHITESPACE,
                           base::SPLIT_WANT_ALL);
  }
  process_info->set_cmd_line(&cmd_line);

  // Make sure that if we get a directory, it has a trailing separator
  FilePath file_path = file_in.AsEndingWithSeparator();

  FilePath cwd_path = pid_path.Append("cwd");
  FilePath link_val;
  ReadLink(cwd_path, &link_val);
  std::string value = link_val.value();
  if (IsPathChild(file_path, link_val)) {
    process_info->set_cwd(&value);
  } else {
    link_val.clear();
    process_info->set_cwd(&value);
  }

  // Open /proc/<pid>/fd
  FilePath fd_dirpath = pid_path.Append("fd");

  base::FileEnumerator fd_dir_enum(fd_dirpath, false,
                                   base::FileEnumerator::FILES);

  std::set<FilePath> open_files;
  // List open file descriptors
  for (FilePath fd_path = fd_dir_enum.Next(); !fd_path.empty();
       fd_path = fd_dir_enum.Next()) {
    ReadLink(fd_path, &link_val);
    if (IsPathChild(file_path, link_val)) {
      open_files.insert(link_val);
    }
  }
  process_info->set_open_files(&open_files);
}

void Platform::LookForOpenFiles(const FilePath& path_in,
                                std::vector<pid_t>* pids) {
  // Make sure that if we get a directory, it has a trailing separator
  FilePath file_path = path_in.AsEndingWithSeparator();

  // Open /proc
  base::FileEnumerator proc_dir_enum(FilePath(kProcDir), false,
                                     base::FileEnumerator::DIRECTORIES);

  // List PIDs in /proc
  for (FilePath pid_path = proc_dir_enum.Next(); !pid_path.empty();
       pid_path = proc_dir_enum.Next()) {
    pid_t pid = 0;
    // Ignore PID 1 and errors
    if (!base::StringToInt(pid_path.BaseName().value(), &pid) || pid <= 1) {
      continue;
    }

    FilePath cwd_path = pid_path.Append("cwd");
    FilePath cwd_link;
    ReadLink(cwd_path, &cwd_link);
    if (IsPathChild(file_path, cwd_link)) {
      pids->push_back(pid);
      continue;
    }

    // Open /proc/<pid>/fd
    FilePath fd_dirpath = pid_path.Append("fd");

    base::FileEnumerator fd_dir_enum(fd_dirpath, false,
                                     base::FileEnumerator::FILES);

    // List open file descriptors
    for (FilePath fd_path = fd_dir_enum.Next(); !fd_path.empty();
         fd_path = fd_dir_enum.Next()) {
      FilePath fd_link;
      ReadLink(fd_path, &fd_link);
      if (IsPathChild(file_path, fd_link)) {
        pids->push_back(pid);
        break;
      }
    }
  }
}

bool Platform::IsPathChild(const FilePath& parent_path,
                           const FilePath& child_path) {
  std::string parent = parent_path.value();
  std::string child = child_path.value();
  if (parent.length() == 0 || child.length() == 0) {
    return false;
  }
  if (child.length() >= parent.length()) {
    if (child.compare(0, parent.length(), parent, 0, parent.length()) == 0) {
      return true;
    }
  } else if ((parent[parent.length() - 1] == '/') &&
             (child.length() == (parent.length() - 1))) {
    if (child.compare(0, child.length(), parent, 0, parent.length() - 1) == 0) {
      return true;
    }
  }
  return false;
}

bool Platform::GetOwnership(const FilePath& path,
                            uid_t* user_id,
                            gid_t* group_id,
                            bool follow_links) const {
  struct stat path_status;
  int ret;
  if (follow_links)
    ret = stat(path.value().c_str(), &path_status);
  else
    ret = lstat(path.value().c_str(), &path_status);

  if (ret != 0) {
    PLOG(ERROR) << (follow_links ? "" : "l") << "stat() of " << path.value()
                << " failed.";
    return false;
  }
  if (user_id)
    *user_id = path_status.st_uid;
  if (group_id)
    *group_id = path_status.st_gid;
  return true;
}

bool Platform::SetOwnership(const FilePath& path,
                            uid_t user_id,
                            gid_t group_id,
                            bool follow_links) const {
  int ret;
  if (follow_links)
    ret = chown(path.value().c_str(), user_id, group_id);
  else
    ret = lchown(path.value().c_str(), user_id, group_id);
  if (ret) {
    PLOG(ERROR) << (follow_links ? "" : "l") << "chown() of " << path.value()
                << " to (" << user_id << "," << group_id << ") failed.";
    return false;
  }
  return true;
}

bool Platform::GetPermissions(const FilePath& path, mode_t* mode) const {
  struct stat path_status;
  if (stat(path.value().c_str(), &path_status) != 0) {
    PLOG(ERROR) << "stat() of " << path.value() << " failed.";
    return false;
  }
  *mode = path_status.st_mode;
  return true;
}

bool Platform::SetPermissions(const FilePath& path, mode_t mode) const {
  if (chmod(path.value().c_str(), mode)) {
    PLOG(ERROR) << "chmod() of " << path.value() << " to (" << std::oct << mode
                << ") failed.";
    return false;
  }
  return true;
}

bool Platform::SetGroupAccessible(const FilePath& path,
                                  gid_t group_id,
                                  mode_t group_mode) const {
  uid_t user_id;
  mode_t mode;
  if (!GetOwnership(path, &user_id, NULL, true) ||
      !GetPermissions(path, &mode) ||
      !SetOwnership(path, user_id, group_id, true /* follow_links */) ||
      !SetPermissions(path, (mode & ~S_IRWXG) | (group_mode & S_IRWXG))) {
    LOG(ERROR) << "Couldn't set up group access on directory: " << path.value();
    return false;
  }
  return true;
}

bool Platform::GetUserId(const std::string& user,
                         uid_t* user_id,
                         gid_t* group_id) const {
  // Load the passwd entry
  long user_name_length = sysconf(_SC_GETPW_R_SIZE_MAX);  // NOLINT long
  if (user_name_length == -1) {
    user_name_length = kDefaultPwnameLength;
  }
  struct passwd user_info, *user_infop;
  std::vector<char> user_name_buf(user_name_length);
  if (getpwnam_r(user.c_str(), &user_info, user_name_buf.data(),
                 user_name_length, &user_infop)) {
    return false;
  }
  *user_id = user_info.pw_uid;
  *group_id = user_info.pw_gid;
  return true;
}

bool Platform::GetGroupId(const std::string& group, gid_t* group_id) const {
  // Load the group entry
  long group_name_length = sysconf(_SC_GETGR_R_SIZE_MAX);  // NOLINT long
  if (group_name_length == -1) {
    group_name_length = kDefaultPwnameLength;
  }
  struct group group_info, *group_infop = nullptr;
  std::vector<char> group_name_buf(group_name_length);

  // In getgrgid_r(), err can be 0 even when a group is not found.
  // Only *group_infop matters: if NULL, the group was not found.
  getgrnam_r(group.c_str(), &group_info, group_name_buf.data(),
             group_name_length, &group_infop);

  if (!group_infop)
    return false;

  *group_id = group_infop->gr_gid;
  return true;
}

int64_t Platform::AmountOfFreeDiskSpace(const FilePath& path) const {
  return base::SysInfo::AmountOfFreeDiskSpace(path);
}

int64_t Platform::GetQuotaCurrentSpaceForUid(const base::FilePath& device,
                                             uid_t user_id) const {
  struct dqblk dq = {};
  if (quotactl(QCMD(Q_GETQUOTA, USRQUOTA), device.value().c_str(), user_id,
               reinterpret_cast<char*>(&dq)) != 0) {
    return -1;
  }
  return dq.dqb_curspace;
}

int64_t Platform::GetQuotaCurrentSpaceForGid(const base::FilePath& device,
                                             gid_t group_id) const {
  struct dqblk dq = {};
  if (quotactl(QCMD(Q_GETQUOTA, GRPQUOTA), device.value().c_str(), group_id,
               reinterpret_cast<char*>(&dq)) != 0) {
    return -1;
  }
  return dq.dqb_curspace;
}

int64_t Platform::GetQuotaCurrentSpaceForProjectId(const base::FilePath& device,
                                                   int project_id) const {
  struct dqblk dq = {};
  if (quotactl(QCMD(Q_GETQUOTA, PRJQUOTA), device.value().c_str(), project_id,
               reinterpret_cast<char*>(&dq)) != 0) {
    return -1;
  }
  return dq.dqb_curspace;
}

bool Platform::FileExists(const FilePath& path) {
  return base::PathExists(path);
}

int Platform::Access(const FilePath& path, uint32_t flag) {
  return HANDLE_EINTR(access(path.value().c_str(), flag));
}

bool Platform::DirectoryExists(const FilePath& path) {
  base::stat_wrapper_t buf = {};
  return Stat(path, &buf) && S_ISDIR(buf.st_mode);
}

bool Platform::GetFileSize(const FilePath& path, int64_t* size) {
  return base::GetFileSize(path, size);
}

int64_t Platform::ComputeDirectoryDiskUsage(const FilePath& path) {
  return brillo::ComputeDirectoryDiskUsage(path);
}

FILE* Platform::CreateAndOpenTemporaryFile(FilePath* path) {
  return base::CreateAndOpenTemporaryStream(path).release();
}

FILE* Platform::OpenFile(const FilePath& path, const char* mode) {
  return base::OpenFile(path, mode);
}

bool Platform::CloseFile(FILE* fp) {
  return base::CloseFile(fp);
}

void Platform::InitializeFile(base::File* file,
                              const base::FilePath& path,
                              uint32_t flags) {
  return file->Initialize(path, flags);
}

bool Platform::LockFile(int fd) {
  return HANDLE_EINTR(flock(fd, LOCK_EX)) == 0;
}

bool Platform::WriteOpenFile(FILE* fp, const brillo::Blob& blob) {
  return (fwrite(static_cast<const void*>(&blob.at(0)), 1, blob.size(), fp) !=
          blob.size());
}

bool Platform::WriteFile(const FilePath& path, const brillo::Blob& blob) {
  return brillo::WriteBlobToFile<brillo::Blob>(path, blob);
}

bool Platform::WriteSecureBlobToFile(const FilePath& path,
                                     const brillo::SecureBlob& blob) {
  return brillo::WriteBlobToFile<brillo::SecureBlob>(path, blob);
}

bool Platform::WriteStringToFile(const FilePath& path,
                                 const std::string& data) {
  return brillo::WriteStringToFile(path, data);
}

bool Platform::WriteArrayToFile(const FilePath& path,
                                const char* data,
                                size_t size) {
  return brillo::WriteToFile(path, data, size);
}

std::string Platform::GetRandomSuffix() {
  const int kBufferSize = 6;
  unsigned char buffer[kBufferSize];
  if (RAND_bytes(buffer, kBufferSize) < 0) {
    return std::string();
  }
  std::string suffix;
  for (int i = 0; i < kBufferSize; ++i) {
    int random_value = buffer[i] % (2 * 26 + 10);
    if (random_value < 26) {
      suffix.push_back('a' + random_value);
    } else if (random_value < 2 * 26) {
      suffix.push_back('A' + random_value - 26);
    } else {
      suffix.push_back('0' + random_value - 2 * 26);
    }
  }
  return suffix;
}

bool Platform::WriteFileAtomic(const FilePath& path,
                               const brillo::Blob& blob,
                               mode_t mode) {
  return brillo::WriteBlobToFileAtomic<brillo::Blob>(path, blob, mode);
}

bool Platform::WriteSecureBlobToFileAtomic(const FilePath& path,
                                           const brillo::SecureBlob& blob,
                                           mode_t mode) {
  return brillo::WriteBlobToFileAtomic<brillo::SecureBlob>(path, blob, mode);
}

bool Platform::WriteStringToFileAtomic(const FilePath& path,
                                       const std::string& data,
                                       mode_t mode) {
  return brillo::WriteToFileAtomic(path, data.data(), data.size(), mode);
}

bool Platform::WriteFileAtomicDurable(const FilePath& path,
                                      const brillo::Blob& blob,
                                      mode_t mode) {
  const std::string data(reinterpret_cast<const char*>(blob.data()),
                         blob.size());
  return WriteStringToFileAtomicDurable(path, data, mode);
}

bool Platform::WriteSecureBlobToFileAtomicDurable(
    const FilePath& path, const brillo::SecureBlob& blob, mode_t mode) {
  if (!WriteSecureBlobToFileAtomic(path, blob, mode))
    return false;

  WriteChecksum(path, blob.data(), blob.size(), mode);
  return SyncDirectory(FilePath(path).DirName());
}

bool Platform::WriteStringToFileAtomicDurable(const FilePath& path,
                                              const std::string& data,
                                              mode_t mode) {
  if (!WriteStringToFileAtomic(path, data, mode))
    return false;
  WriteChecksum(path, data.data(), data.size(), mode);
  return SyncDirectory(FilePath(path).DirName());
}

bool Platform::TouchFileDurable(const FilePath& path) {
  brillo::Blob empty_blob(0);
  if (!WriteFile(path, empty_blob))
    return false;
  return SyncDirectory(FilePath(path).DirName());
}

bool Platform::ReadFile(const FilePath& path, brillo::Blob* blob) {
  return ReadFileToBlob<brillo::Blob>(path, blob);
}

bool Platform::ReadFileToString(const FilePath& path, std::string* string) {
  if (!base::ReadFileToString(path, string)) {
    return false;
  }
  VerifyChecksum(path, string->data(), string->size());
  return true;
}

bool Platform::ReadFileToSecureBlob(const FilePath& path,
                                    brillo::SecureBlob* sblob) {
  return ReadFileToBlob<brillo::SecureBlob>(path, sblob);
}

bool Platform::CreateDirectory(const FilePath& path) {
  return base::CreateDirectory(path);
}

bool Platform::SafeCreateDirAndSetOwnership(const base::FilePath& path,
                                            uid_t user_id,
                                            gid_t group_id) {
  auto root_fd_result = brillo::SafeFD::Root();
  if (root_fd_result.second != brillo::SafeFD::Error::kNoError) {
    return false;
  }

  auto path_result = root_fd_result.first.MakeDir(
      path, brillo::SafeFD::kDefaultDirPermissions, user_id, group_id);
  return path_result.second == brillo::SafeFD::Error::kNoError;
}

bool Platform::DeleteFile(const FilePath& path, bool is_recursive) {
  if (is_recursive)
    return base::DeletePathRecursively(path);
  return base::DeleteFile(path);
}

bool Platform::DeleteFileDurable(const FilePath& path, bool is_recursive) {
  if (is_recursive && !base::DeletePathRecursively(path))
    return false;
  if (!is_recursive && !base::DeleteFile(path))
    return false;
  return SyncDirectory(path.DirName());
}

bool Platform::DeleteFileSecurely(const FilePath& path) {
  return secure_erase_file::SecureErase(path) &&
         secure_erase_file::DropCaches();
}

bool Platform::Move(const FilePath& from, const FilePath& to) {
  return base::Move(from, to);
}

bool Platform::EnumerateDirectoryEntries(const FilePath& path,
                                         bool recursive,
                                         std::vector<FilePath>* ent_list) {
  auto ft = static_cast<base::FileEnumerator::FileType>(
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
      base::FileEnumerator::SHOW_SYM_LINKS);
  base::FileEnumerator ent_enum(path, recursive, ft);
  for (FilePath path = ent_enum.Next(); !path.empty(); path = ent_enum.Next())
    ent_list->push_back(path);
  return true;
}

base::Time Platform::GetCurrentTime() const {
  return base::Time::Now();
}

bool Platform::Stat(const FilePath& path, base::stat_wrapper_t* buf) {
  return base::File::Lstat(path.value().c_str(), buf) == 0;
}

bool Platform::HasExtendedFileAttribute(const FilePath& path,
                                        const std::string& name) {
  ssize_t sz = lgetxattr(path.value().c_str(), name.c_str(), nullptr, 0);
  if (sz < 0) {
    if (errno != ENODATA) {
      PLOG(ERROR) << "lgetxattr: " << path.value();
    }
    return false;
  }
  return true;
}

bool Platform::ListExtendedFileAttributes(const FilePath& path,
                                          std::vector<std::string>* attr_list) {
  ssize_t sz = llistxattr(path.value().c_str(), nullptr, 0);
  if (sz < 0) {
    PLOG(ERROR) << "llistxattr: " << path.value();
    return false;
  }
  std::vector<char> names(sz);
  if (llistxattr(path.value().c_str(), names.data(), sz) < 0) {
    PLOG(ERROR) << "llistxattr: " << path.value();
    return false;
  }
  int pos = 0;
  while (pos < sz) {
    attr_list->emplace_back(names.data() + pos);
    pos += attr_list->back().length() + 1;
  }
  return true;
}

bool Platform::GetExtendedFileAttributeAsString(const base::FilePath& path,
                                                const std::string& name,
                                                std::string* value) {
  ssize_t sz = lgetxattr(path.value().c_str(), name.c_str(), nullptr, 0);
  if (sz < 0) {
    PLOG(ERROR) << "lgetxattr: " << path.value();
    return false;
  }
  std::vector<char> value_vector(sz);
  if (!GetExtendedFileAttribute(path, name, value_vector.data(), sz)) {
    return false;
  }
  value->assign(value_vector.data(), sz);
  return true;
}

bool Platform::GetExtendedFileAttribute(const base::FilePath& path,
                                        const std::string& name,
                                        char* value,
                                        ssize_t size) {
  if (lgetxattr(path.value().c_str(), name.c_str(), value, size) != size) {
    PLOG(ERROR) << "lgetxattr: " << path.value();
    return false;
  }
  return true;
}

bool Platform::SetExtendedFileAttribute(const base::FilePath& path,
                                        const std::string& name,
                                        const char* value,
                                        size_t size) {
  if (lsetxattr(path.value().c_str(), name.c_str(), value, size, 0) != 0) {
    PLOG(ERROR) << "lsetxattr: " << path.value();
    return false;
  }
  return true;
}

bool Platform::RemoveExtendedFileAttribute(const base::FilePath& path,
                                           const std::string& name) {
  if (lremovexattr(path.value().c_str(), name.c_str()) != 0) {
    PLOG(ERROR) << "lremovexattr: " << path.value();
    return false;
  }
  return true;
}

bool Platform::GetExtFileAttributes(const FilePath& path, int* flags) {
  int fd = HANDLE_EINTR(open(path.value().c_str(), O_RDONLY));
  if (fd < 0) {
    PLOG(ERROR) << "open: " << path.value();
    return false;
  }
  // FS_IOC_GETFLAGS actually takes int*
  // though the signature suggests long*.
  // https://lwn.net/Articles/575846/
  if (ioctl(fd, FS_IOC_GETFLAGS, flags) < 0) {
    PLOG(ERROR) << "ioctl: " << path.value();
    IGNORE_EINTR(close(fd));
    return false;
  }
  IGNORE_EINTR(close(fd));
  return true;
}

bool Platform::SetExtFileAttributes(const FilePath& path, int flags) {
  int fd = HANDLE_EINTR(open(path.value().c_str(), O_RDONLY));
  if (fd < 0) {
    PLOG(ERROR) << "open: " << path.value();
    return false;
  }
  // FS_IOC_SETFLAGS actually takes int*
  // though the signature suggests long*.
  // https://lwn.net/Articles/575846/
  int current_flag;
  if (ioctl(fd, FS_IOC_GETFLAGS, &current_flag) < 0) {
    PLOG(ERROR) << "ioctl GETFLAGS: " << path.value();
    IGNORE_EINTR(close(fd));
    return false;
  }
  flags |= current_flag;
  if (ioctl(fd, FS_IOC_SETFLAGS, &flags) < 0) {
    PLOG(ERROR) << "ioctl SETFLAGS: " << path.value();
    IGNORE_EINTR(close(fd));
    return false;
  }
  IGNORE_EINTR(close(fd));
  return true;
}

bool Platform::HasNoDumpFileAttribute(const FilePath& path) {
  int flags;
  return GetExtFileAttributes(path, &flags) &&
         (flags & FS_NODUMP_FL) == FS_NODUMP_FL;
}

bool Platform::Rename(const FilePath& from, const FilePath& to) {
  return base::ReplaceFile(from, to, /*error=*/nullptr);
}

bool Platform::Copy(const FilePath& from, const FilePath& to) {
  return base::CopyDirectory(from, to, true);
}

bool Platform::CopyPermissionsCallback(const FilePath& old_base,
                                       const FilePath& new_base,
                                       const FilePath& file_path,
                                       const base::stat_wrapper_t& file_info
) {
  // Find the new path that corresponds with the old path given by file_info.
  FilePath old_path = file_path;
  FilePath new_path = new_base;
  if (old_path != old_base) {
    if (old_path.IsAbsolute()) {
      if (!old_base.AppendRelativePath(old_path, &new_path)) {
        LOG(ERROR) << "AppendRelativePath failed: parent=" << old_base.value()
                   << ", child=" << old_path.value();
        return false;
      }
    } else {
      new_path = new_base.Append(old_path);
    }
  }
  if (!SetOwnership(new_path, file_info.st_uid, file_info.st_gid,
                    true /* follow_links */)) {
    PLOG(ERROR) << "Failed to set ownership for " << new_path.value();
    return false;
  }
  const mode_t permissions_mask = 07777;
  if (!SetPermissions(new_path, file_info.st_mode & permissions_mask)) {
    PLOG(ERROR) << "Failed to set permissions for " << new_path.value();
    return false;
  }
  return true;
}

bool Platform::CopyWithPermissions(const FilePath& from_path,
                                   const FilePath& to_path) {
  if (!Copy(from_path, to_path)) {
    PLOG(ERROR) << "Failed to copy " << from_path.value();
    return false;
  }

  // If something goes wrong we want to blow away the half-baked path.
  ScopedPath scoped_new_path(this, to_path);

  // Unfortunately, ownership and permissions are not always retained.
  // Apply the old ownership / permissions on a per-file basis.
  FileEnumeratorCallback callback =
      base::Bind(&Platform::CopyPermissionsCallback, base::Unretained(this),
                 from_path, to_path);
  if (!WalkPath(from_path, callback))
    return false;

  // The copy is done, keep the new path.
  scoped_new_path.release();
  return true;
}

bool Platform::ApplyPermissionsCallback(
    const Permissions& default_file_info,
    const Permissions& default_dir_info,
    const std::map<FilePath, Permissions>& special_cases,
    const FilePath& file_path,
    const base::stat_wrapper_t& file_info
) {
  Permissions expected;
  std::map<FilePath, Permissions>::const_iterator it =
      special_cases.find(file_path);
  if (it != special_cases.end()) {
    expected = it->second;
  } else if (IsDirectory(file_info)) {
    expected = default_dir_info;
  } else {
    expected = default_file_info;
  }
  if (expected.user != file_info.st_uid || expected.group != file_info.st_gid) {
    LOG(WARNING) << "Unexpected user/group for " << file_path.value();
    if (!SetOwnership(file_path, expected.user, expected.group, true)) {
      PLOG(ERROR) << "Failed to fix user/group for " << file_path.value();
      return false;
    }
  }
  const mode_t permissions_mask = 07777;
  if ((expected.mode & permissions_mask) !=
      (file_info.st_mode & permissions_mask)) {
    LOG(WARNING) << "Unexpected permissions for " << file_path.value();
    if (!SetPermissions(file_path, expected.mode & permissions_mask)) {
      PLOG(ERROR) << "Failed to set permissions for " << file_path.value();
      return false;
    }
  }
  return true;
}

bool Platform::ApplyPermissionsRecursive(
    const FilePath& path,
    const Permissions& default_file_info,
    const Permissions& default_dir_info,
    const std::map<FilePath, Permissions>& special_cases) {
  FileEnumeratorCallback callback =
      base::Bind(&Platform::ApplyPermissionsCallback, base::Unretained(this),
                 default_file_info, default_dir_info, special_cases);
  return WalkPath(path, callback);
}

bool Platform::StatVFS(const FilePath& path, struct statvfs* vfs) {
  return statvfs(path.value().c_str(), vfs) == 0;
}

bool Platform::SameVFS(const base::FilePath& mnt_a,
                       const base::FilePath& mnt_b) {
  struct stat stat_a, stat_b;

  if (lstat(mnt_a.value().c_str(), &stat_a)) {
    PLOG(ERROR) << "lstat: " << mnt_a.value().c_str();
    return false;
  }
  if (lstat(mnt_b.value().c_str(), &stat_b)) {
    PLOG(ERROR) << "lstat: " << mnt_b.value().c_str();
    return false;
  }
  return (stat_a.st_dev == stat_b.st_dev);
}

bool Platform::FindFilesystemDevice(const FilePath& filesystem_in,
                                    std::string* device) {
  /* Clear device to indicate failure case. */
  device->clear();

  /* Removing trailing slashes. */
  FilePath filesystem = filesystem_in.StripTrailingSeparators();

  std::vector<DecodedProcMountInfo> proc_mounts = ReadMountInfoFile();

  for (const auto& mount : proc_mounts) {
    if (mount.mount_point != filesystem.value())
      continue;

    *device = mount.mount_source;
  }
  return (device->length() > 0);
}

bool Platform::ReportFilesystemDetails(const FilePath& filesystem,
                                       const FilePath& logfile) {
  brillo::ProcessImpl process;
  int rc;
  std::string device;
  if (!FindFilesystemDevice(filesystem, &device)) {
    LOG(ERROR) << "Failed to find device for " << filesystem.value();
    return false;
  }

  process.RedirectOutput(logfile.value());
  process.AddArg(kPathTune2fs);
  process.AddArg("-l");
  process.AddArg(device);

  rc = process.Run();
  if (rc == 0)
    return true;
  LOG(ERROR) << "Failed to run tune2fs on " << device << " ("
             << filesystem.value() << ", exit " << rc << ")";
  return false;
}

bool Platform::FirmwareWriteProtected() {
  return VbGetSystemPropertyInt("wpsw_cur") != 0;
}

bool Platform::SyncFileOrDirectory(const FilePath& path,
                                   bool is_directory,
                                   bool data_sync) {
  return brillo::SyncFileOrDirectory(path, is_directory, data_sync);
}

bool Platform::DataSyncFile(const FilePath& path) {
  return SyncFileOrDirectory(path, false /* directory */, true /* data_sync */);
}

bool Platform::SyncFile(const FilePath& path) {
  return SyncFileOrDirectory(path, false /* directory */,
                             false /* data_sync */);
}

bool Platform::SyncDirectory(const FilePath& path) {
  return SyncFileOrDirectory(path, true /* directory */, false /* data_sync */);
}

void Platform::Sync() {
  const base::TimeTicks start = base::TimeTicks::Now();
  sync();
  const base::TimeDelta delta = base::TimeTicks::Now() - start;
  if (delta > base::TimeDelta::FromSeconds(kLongSyncSec)) {
    LOG(WARNING) << "Long sync(): " << delta.InSeconds() << " seconds";
  }
}

std::string Platform::GetHardwareID() {
  char buffer[VB_MAX_STRING_PROPERTY];
  const char* rc =
      VbGetSystemPropertyString("hwid", buffer, base::size(buffer));

  if (rc != nullptr) {
    return std::string(rc);
  }

  LOG(WARNING) << "Could not read hwid property";
  return std::string();
}

bool Platform::CreateSymbolicLink(const base::FilePath& path,
                                  const base::FilePath& target) {
  if (!base::CreateSymbolicLink(target, path)) {
    PLOG(ERROR) << "Failed to create link " << path.value();
    return false;
  }
  return true;
}

bool Platform::ReadLink(const base::FilePath& path, base::FilePath* target) {
  if (!base::ReadSymbolicLink(path, target)) {
    PLOG(ERROR) << "Failed to read link " << path.value();
    return false;
  }
  return true;
}

bool Platform::SetFileTimes(const base::FilePath& path,
                            const struct timespec& atime,
                            const struct timespec& mtime,
                            bool follow_links) {
  const struct timespec times[2] = {atime, mtime};
  if (utimensat(AT_FDCWD, path.value().c_str(), times,
                follow_links ? 0 : AT_SYMLINK_NOFOLLOW)) {
    PLOG(ERROR) << "Failed to update times for file " << path.value();
    return false;
  }
  return true;
}

bool Platform::SendFile(int fd_to, int fd_from, off_t offset, size_t count) {
  while (count > 0) {
    ssize_t written = sendfile(fd_to, fd_from, &offset, count);
    if (written < 0) {
      PLOG(ERROR) << "sendfile failed to copy data";
      return false;
    }
    if (written == 0) {
      LOG(ERROR) << "Attempting to read past the end of the file";
      return false;
    }
    count -= written;
  }
  return true;
}

bool Platform::CreateSparseFile(const base::FilePath& path, int64_t size) {
  base::File file;
  InitializeFile(&file, path,
                 base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!file.IsValid()) {
    PLOG(ERROR) << "open sparse file " << path.value();
    return false;
  }
  return file.SetLength(size);
}

bool Platform::GetBlkSize(const base::FilePath& device, uint64_t* size) {
  base::ScopedFD fd(HANDLE_EINTR(
      open(device.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "open " << device.value();
    return false;
  }
  if (ioctl(fd.get(), BLKGETSIZE64, size)) {
    PLOG(ERROR) << "ioctl(BLKGETSIZE): " << device.value();
    return false;
  }
  return true;
}

base::FilePath Platform::AttachLoop(const base::FilePath& path) {
  base::ScopedFD control_fd(
      HANDLE_EINTR(open(kLoopControl, O_RDONLY | O_CLOEXEC)));
  if (!control_fd.is_valid()) {
    PLOG(ERROR) << "open loop control";
    return base::FilePath();
  }
  std::string loopback;

  while (true) {
    int num = ioctl(control_fd.get(), LOOP_CTL_GET_FREE);
    if (num < 0) {
      PLOG(ERROR) << "ioctl(LOOP_CTL_GET_FREE)";
      return base::FilePath();
    }
    loopback = kLoopPrefix + base::NumberToString(num);
    base::ScopedFD loop_fd(
        HANDLE_EINTR(open(loopback.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC)));
    if (!loop_fd.is_valid()) {
      PLOG(ERROR) << "open " + loopback;
      return base::FilePath();
    }
    base::ScopedFD fd(
        HANDLE_EINTR(open(path.value().c_str(), O_RDWR | O_CLOEXEC)));
    if (!fd.is_valid()) {
      PLOG(ERROR) << "open " + path.value();
      return base::FilePath();
    }
    if (ioctl(loop_fd.get(), LOOP_SET_FD, fd.get()) == 0)
      break;
    // Retry on LOOP_SET_FD coming back with EBUSY.
    if (errno != EBUSY) {
      PLOG(ERROR) << "LOOP_SET_FD";
      return base::FilePath();
    }
  }
  return base::FilePath(loopback);
}

bool Platform::DetachLoop(const base::FilePath& device) {
  base::ScopedFD loop_fd(HANDLE_EINTR(
      open(device.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC)));
  if (!loop_fd.is_valid()) {
    PLOG(ERROR) << "open " + device.value();
    return false;
  }
  if (ioctl(loop_fd.get(), LOOP_CLR_FD, 0)) {
    PLOG(ERROR) << "LOOP_CLR_FD";
    return false;
  }
  return true;
}

std::vector<Platform::LoopDevice> Platform::GetAttachedLoopDevices() {
  // Read /sys/block to discover all loop devices.
  std::vector<FilePath> sysfs_block_devices;
  EnumerateDirectoryEntries(FilePath(kSysBlockPath), false /* is_recursive */,
                            &sysfs_block_devices);
  std::vector<LoopDevice> devices;
  for (const auto& sysfs_block_device : sysfs_block_devices) {
    FilePath device = FilePath(kDevPath).Append(sysfs_block_device.BaseName());
    // Backing file contains path to associated source for loop devices.
    FilePath sysfs_backing_file = sysfs_block_device.Append(kLoopBackingFile);
    std::string backing_file_content;
    // If the backing file doesn't exist, it's not an attached loop device.
    if (!ReadFileToString(sysfs_backing_file, &backing_file_content))
      continue;
    FilePath backing_file(
        base::TrimWhitespaceASCII(backing_file_content, base::TRIM_ALL));
    devices.push_back({backing_file, device});
  }
  return devices;
}

bool Platform::FormatExt4(const base::FilePath& file,
                          const std::vector<std::string>& opts,
                          uint64_t blocks) {
  brillo::ProcessImpl format_process;
  format_process.AddArg("/sbin/mkfs.ext4");

  for (const auto& arg : opts)
    format_process.AddArg(arg);

  format_process.AddArg(file.value());
  if (blocks != 0)
    format_process.AddArg(std::to_string(blocks));

  // No need to emit output.
  format_process.AddArg("-q");

  // Close unused file descriptors in child process.
  format_process.SetCloseUnusedFileDescriptors(true);

  // Avoid polluting the parent process' stdout.
  format_process.RedirectOutput("/dev/null");

  int rc = format_process.Run();
  if (rc != 0) {
    LOG(ERROR) << "Can't format '" << file.value()
               << "' as ext4, exit status: " << rc;
    return false;
  }

  brillo::ProcessImpl tune_process;
  tune_process.AddArg("/sbin/tune2fs");
  // Disable max mount count checking.
  tune_process.AddArg("-c");
  tune_process.AddArg("0");
  // Disable filesystem checking.
  tune_process.AddArg("-i");
  tune_process.AddArg("0");
  tune_process.AddArg(file.value());

  // Close unused file descriptors in child process.
  tune_process.SetCloseUnusedFileDescriptors(true);

  // Avoid polluting the parent process' stdout.
  tune_process.RedirectOutput("/dev/null");

  rc = tune_process.Run();
  if (rc != 0) {
    LOG(ERROR) << "Can't tune ext4: " << file.value() << ", error: " << rc;
    return false;
  }
  return true;
}

bool Platform::ResizeFilesystem(const base::FilePath& file, uint64_t blocks) {
  brillo::ProcessImpl resize_process;
  resize_process.AddArg("/sbin/resize2fs");
  resize_process.AddArg("-f");
  resize_process.AddArg(file.value().c_str());
  resize_process.AddArg(std::to_string(blocks));

  // Close unused file descriptors in child process.
  resize_process.SetCloseUnusedFileDescriptors(true);

  // Start the process and return.
  LOG(INFO) << "Resizing filesystem on " << file.value() << " to " << blocks;
  int rc = resize_process.Run();
  if (rc != 0)
    return false;

  LOG(INFO) << "Resizing process started.";
  return true;
}

bool Platform::RestoreSELinuxContexts(const base::FilePath& path,
                                      bool recursive) {
#if USE_SELINUX
  LOG(INFO) << "Restoring SELinux contexts for: " << path.value()
            << ", recursive=" << std::boolalpha << recursive;
  int restorecon_flag = 0;
  if (recursive)
    restorecon_flag |= SELINUX_RESTORECON_RECURSE;
  if (selinux_restorecon(path.value().c_str(), restorecon_flag) != 0) {
    PLOG(ERROR) << "restorecon(" << path.value() << ") failed";
    return false;
  }
#endif
  return true;
}

bool Platform::SetSELinuxContext(const base::FilePath& path,
                                 const std::string& context) {
#if USE_SELINUX
  int result = setfilecon(path.value().c_str(), context.c_str());
  if (result != 0) {
    LOG(ERROR) << "Failed to set SELinux context for " << path
               << ", errno = " << errno;
    return false;
  }
#else
  LOG(WARNING)
      << "Try to set SELinux context when SELinux is disabled at compile time.";
#endif
  return true;
}

bool Platform::SetupProcessKeyring() {
  // We have patched upstart to set up a session keyring in init.
  // This results in the user keyring not present under the session keyring and
  // it breaks eCryptfs. Set up a process keyring and link the user keyring to
  // it to fix this.
  if (keyctl_link(KEY_SPEC_USER_KEYRING, KEY_SPEC_PROCESS_KEYRING)) {
    PLOG(ERROR) << "Failed to link the user keyring to the process keyring.";
    return false;
  }
  // When we have a process keyring, it hides the session keyring and it breaks
  // ext4 encryption.
  // Link the session keyring to the process keyring so that request_key() can
  // find keys under the session keyring too.
  if (keyctl_link(KEY_SPEC_SESSION_KEYRING, KEY_SPEC_PROCESS_KEYRING)) {
    PLOG(ERROR) << "Failed to link the session keyring to the process keyring.";
    return false;
  }
  return true;
}

// Encapsulate these helpers to avoid include conflicts.
namespace ecryptfs {
extern "C" {
#include <ecryptfs.h>  // NOLINT(build/include_alpha)
}

long AddEcryptfsAuthToken(  // NOLINT(runtime/int)
    const brillo::SecureBlob& key,
    const std::string& key_sig,
    const brillo::SecureBlob& salt) {
  DCHECK_EQ(static_cast<size_t>(ECRYPTFS_MAX_KEY_BYTES), key.size());
  DCHECK_EQ(static_cast<size_t>(ECRYPTFS_SIG_SIZE) * 2, key_sig.length());
  DCHECK_EQ(static_cast<size_t>(ECRYPTFS_SALT_SIZE), salt.size());

  struct ecryptfs_auth_tok auth_token;

  generate_payload(&auth_token, const_cast<char*>(key_sig.c_str()),
                   const_cast<char*>(salt.char_data()),
                   const_cast<char*>(key.char_data()));

  return ecryptfs_add_auth_tok_to_keyring(&auth_token,
                                          const_cast<char*>(key_sig.c_str()));
}
}  // namespace ecryptfs

dircrypto::KeyState Platform::GetDirCryptoKeyState(const FilePath& dir) {
  return dircrypto::GetDirectoryKeyState(dir);
}

bool Platform::SetDirCryptoKey(const FilePath& dir,
                               const dircrypto::KeyReference& key_reference) {
  return dircrypto::SetDirectoryKey(dir, key_reference);
}

bool Platform::AddDirCryptoKeyToKeyring(
    const brillo::SecureBlob& key, dircrypto::KeyReference* key_reference) {
  return dircrypto::AddDirectoryKey(key, key_reference);
}

bool Platform::InvalidateDirCryptoKey(
    const dircrypto::KeyReference& key_reference, const FilePath& shadow_root) {
  return dircrypto::RemoveDirectoryKey(key_reference, shadow_root);
}

bool Platform::ClearUserKeyring() {
  /* Flush cache to prevent corruption */
  return (keyctl(KEYCTL_CLEAR, KEY_SPEC_USER_KEYRING) == 0);
}

bool Platform::AddEcryptfsAuthToken(const brillo::SecureBlob& key,
                                    const std::string& key_sig,
                                    const brillo::SecureBlob& salt) {
  return (ecryptfs::AddEcryptfsAuthToken(key, key_sig, salt) >= 0);
}

FileEnumerator* Platform::GetFileEnumerator(const FilePath& root_path,
                                            bool recursive,
                                            int file_type) {
  return new FileEnumerator(root_path, recursive, file_type);
}

bool Platform::WalkPath(const FilePath& path,
                        const FileEnumeratorCallback& callback) {
  base::stat_wrapper_t base_entry_info;
  if (!Stat(path, &base_entry_info)) {
    PLOG(ERROR) << "Failed to stat " << path.value();
    return false;
  }
  if (!callback.Run(path, base_entry_info))
    return false;
  if (IsDirectory(base_entry_info)) {
    int file_types =
        base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES;
    std::unique_ptr<FileEnumerator> file_enumerator(
        GetFileEnumerator(path, true, file_types));
    FilePath entry_path;
    while (!(entry_path = file_enumerator->Next()).empty()) {
      if (!callback.Run(entry_path, file_enumerator->GetInfo().stat()))
        return false;
    }
  }
  return true;
}

template <class T>
bool Platform::ReadFileToBlob(const FilePath& path, T* blob) {
  int64_t file_size;
  if (!base::PathExists(path)) {
    return false;
  }
  if (!base::GetFileSize(path, &file_size)) {
    LOG(ERROR) << "Could not get size of " << path.value();
    return false;
  }
  // Compare to the max of a signed integer.
  if (file_size > static_cast<int64_t>(std::numeric_limits<int>::max())) {
    LOG(ERROR) << "File " << path.value() << " is too large: " << file_size
               << " bytes.";
    return false;
  }
  blob->resize(file_size);
  int data_read =
      base::ReadFile(path, reinterpret_cast<char*>(blob->data()), blob->size());
  // Cast is okay because of comparison to INT_MAX above.
  if (data_read != static_cast<int>(file_size)) {
    LOG(ERROR) << "Only read " << data_read << " of " << file_size << " bytes"
               << " from " << path.value() << ".";
    return false;
  }
  VerifyChecksum(path, blob->data(), blob->size());
  return true;
}

std::string Platform::GetChecksum(const void* input, size_t input_size) {
  uint32_t sum = Crc32(input, input_size);
  return base::HexEncode(&sum, 4);
}

void Platform::WriteChecksum(const FilePath& path,
                             const void* content,
                             const size_t content_size,
                             mode_t mode) {
  FilePath name = path.AddExtension("sum");
  WriteStringToFileAtomic(name, GetChecksum(content, content_size), mode);
}

void Platform::VerifyChecksum(const FilePath& path,
                              const void* content,
                              const size_t content_size) {
  // Exclude some system paths.
  std::string path_value = path.value();
  if (base::StartsWith(path_value, "/etc", base::CompareCase::SENSITIVE) ||
      base::StartsWith(path_value, "/dev", base::CompareCase::SENSITIVE) ||
      base::StartsWith(path_value, "/sys", base::CompareCase::SENSITIVE) ||
      base::StartsWith(path_value, "/proc", base::CompareCase::SENSITIVE)) {
    return;
  }
  FilePath name = path.AddExtension("sum");
  if (!FileExists(name)) {
    ReportChecksum(kChecksumDoesNotExist);
    return;
  }
  std::string saved_sum;
  if (!base::ReadFileToString(name, &saved_sum)) {
    LOG(ERROR) << "CHECKSUM: Failed to read checksum for " << path.value();
    ReportChecksum(kChecksumReadError);
    return;
  }
  if (saved_sum != GetChecksum(content, content_size)) {
    // Check if the last modified time is out-of-sync for the two files. If they
    // weren't written together they can't be expected to match.
    base::File::Info content_file_info;
    base::File::Info checksum_file_info;
    if (!base::GetFileInfo(path, &content_file_info) ||
        !base::GetFileInfo(name, &checksum_file_info)) {
      LOG(ERROR) << "CHECKSUM: Failed to read file info for " << path.value();
      ReportChecksum(kChecksumReadError);
      return;
    }
    base::TimeDelta checksum_timestamp_diff =
        checksum_file_info.last_modified - content_file_info.last_modified;
    if (checksum_timestamp_diff.magnitude().InSeconds() > 1) {
      LOG(ERROR) << "CHECKSUM: Checksum out-of-sync for " << path.value();
      ReportChecksum(kChecksumOutOfSync);
    } else {
      LOG(ERROR) << "CHECKSUM: Failed to verify checksum for " << path.value();
      ReportChecksum(kChecksumMismatch);
    }
    // Attempt to update the checksum to match the current content.
    mode_t current_mode;
    if (GetPermissions(name, &current_mode)) {
      WriteChecksum(path, content, content_size, current_mode);
    }
    return;
  }
  ReportChecksum(kChecksumOK);
}

FileEnumerator::FileInfo::FileInfo(
    const base::FileEnumerator::FileInfo& file_info) {
  Assign(file_info);
}

FileEnumerator::FileInfo::FileInfo(const FilePath& name,
                                   const base::stat_wrapper_t& stat
                                   )
    : name_(name), stat_(stat) {
}

FileEnumerator::FileInfo::FileInfo(const FileEnumerator::FileInfo& other) {
  if (other.info_.get()) {
    Assign(*other.info_);
  } else {
    info_.reset();
    name_ = other.name_;
    stat_ = other.stat_;
  }
}

FileEnumerator::FileInfo::FileInfo() {
  Assign(base::FileEnumerator::FileInfo());
}

FileEnumerator::FileInfo::~FileInfo() {}

FileEnumerator::FileInfo& FileEnumerator::FileInfo::operator=(
    const FileEnumerator::FileInfo& other) {
  if (other.info_.get()) {
    Assign(*other.info_);
  } else {
    info_.reset();
    name_ = other.name_;
    stat_ = other.stat_;
  }
  return *this;
}

bool FileEnumerator::FileInfo::IsDirectory() const {
  if (info_.get())
    return info_->IsDirectory();
  return ::IsDirectory(stat_);
}

FilePath FileEnumerator::FileInfo::GetName() const {
  if (info_.get())
    return info_->GetName();
  return name_;
}

int64_t FileEnumerator::FileInfo::GetSize() const {
  if (info_.get())
    return info_->GetSize();
  return stat_.st_size;
}

base::Time FileEnumerator::FileInfo::GetLastModifiedTime() const {
  if (info_.get())
    return info_->GetLastModifiedTime();
  return base::Time::FromTimeT(stat_.st_mtime);
}

const base::stat_wrapper_t& FileEnumerator::FileInfo::stat() const {
  if (info_.get())
    return info_->stat();
  return stat_;
}

void FileEnumerator::FileInfo::Assign(
    const base::FileEnumerator::FileInfo& file_info) {
  info_.reset(new base::FileEnumerator::FileInfo(file_info));
  memset(&stat_, 0, sizeof(stat_));
}

FileEnumerator::FileEnumerator(const FilePath& root_path,
                               bool recursive,
                               int file_type) {
  enumerator_.reset(new base::FileEnumerator(root_path, recursive, file_type));
}

FileEnumerator::FileEnumerator(const FilePath& root_path,
                               bool recursive,
                               int file_type,
                               const std::string& pattern) {
  enumerator_.reset(
      new base::FileEnumerator(root_path, recursive, file_type, pattern));
}

FileEnumerator::FileEnumerator() {}
FileEnumerator::~FileEnumerator() {}

FilePath FileEnumerator::Next() {
  if (!enumerator_.get())
    return FilePath();
  return enumerator_->Next();
}

FileEnumerator::FileInfo FileEnumerator::GetInfo() {
  return FileInfo(enumerator_->GetInfo());
}

}  // namespace cryptohome
